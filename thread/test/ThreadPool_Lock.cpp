#include "ThreadPool_Lock.h"
#include "thread/Gate.h"
#include "rdestl/fixed_vector.h"
#include "core/Atomic.h"
#include "core/BitMath.h"
#include "core/Console.h"
#include "core/CPU.h"
#include "core/LockGuard.h"
#include "core/Mutex.h"
#include "core/Thread.h"

namespace
{
const rde::Gate::State kStateEmpty(0);
const rde::Gate::State kStateFull(-1);
const rde::Gate::State kStateOpen(-2);

class WorkerThread : public rde::Thread
{
public:
	explicit WorkerThread(rde::ThreadPool_Lock::Impl* tp)
	:	m_threadPool(tp),
		m_totalPopOverhead(0),
		m_numPopCalls(0)
	{}
	void ProcessTasks();

	rde::ThreadPool_Lock::Impl*	m_threadPool;

	rde::uint64					m_totalPopOverhead;
	int							m_numPopCalls;
};

class TaskQueue
{
public:
	TaskQueue()
	:	m_tasks(0),
		m_numTasks(0),
		m_popIndex(0),
		m_capacity(0)
	{
	}

	void Push(rde::TaskData* task)
	{
		if (m_numTasks == m_capacity)
			Grow();
		m_tasks[m_numTasks++] = task;
	}
	rde::TaskData* Pop()
	{
		rde::TaskData* retTask(0);
		if (m_popIndex < m_numTasks)
			retTask = m_tasks[m_popIndex++];
		return retTask;
	}
	void Reset()
	{
		m_popIndex = m_numTasks = 0;
	}
	bool HasTask() const
	{
		return m_popIndex < m_numTasks;
	}

private:
	void Grow()
	{
		const int newCapacity = (m_capacity == 0 ? 16 : m_capacity * 2);
		rde::TaskData** newTasks = new rde::TaskData*[newCapacity];
		rde::Sys::MemCpy(newTasks, m_tasks, m_numTasks * sizeof(rde::TaskData*));
		delete[] m_tasks;
		m_tasks = newTasks;
		m_capacity = newCapacity;
	}
	rde::TaskData**	m_tasks;
	volatile int	m_numTasks;
	volatile int	m_popIndex;
	int				m_capacity;
};

}

namespace rde
{
struct ThreadPool_Lock::Impl
{
	static const int32	kMaxTaskGroups	= 64;

	struct TaskGroup
	{
		TaskGroup()
		:	m_name(0),
			m_func(0)
		{
		}
		void Init(const char* name, ThreadPool_Lock::TaskFunc func)
		{
			m_name = name;
			m_func = func;
		}
		const char*					m_name;
		ThreadPool_Lock::TaskFunc	m_func;
	};

	Impl()
	:	m_numTaskGroups(0),
		m_taskQueueMutex(2000),
		m_stopped(0)
	{
		m_numPendingTasks = 0;
	}
	~Impl()
	{
		DeleteWorkerThreads();
	}

	// !Shouldn't change during run-time.
	void SetNumThreads(int numThreads)
	{
		if (numThreads == m_workerThreads.size())
			return;

		DeleteWorkerThreads();
		m_stopped = 0;
		LockGuard<Mutex> lock(m_workerThreadListMutex);
		m_workerThreads.reserve(numThreads);

		while (m_workerThreads.size() < numThreads)
		{
			WorkerThread* worker = new WorkerThread(this);
			m_workerThreads.push_back(worker);
			worker->Start<WorkerThread, &WorkerThread::ProcessTasks>(worker);
			const uint32 workerAffinityMask = RDE_BIT(m_workerThreads.size() - 1);
			worker->SetAffinityMask(workerAffinityMask);
		}
	}

	void DeleteWorkerThreads()
	{
		LockGuard<Mutex> lock(m_workerThreadListMutex);
		m_stopped = 1;
		m_taskQueueGate.UpdateIfStateNotEqual(kStateOpen, kStateOpen);
		for (WorkerThreads::iterator it = m_workerThreads.begin();
			it != m_workerThreads.end(); ++it)
		{
			delete *it;
		}
		m_workerThreads.clear();
	}

	TaskGroupID CreateTaskGroup(const char* name, TaskFunc func)
	{
		const TaskGroupID id = m_numTaskGroups++;
		RDE_ASSERT(id < kMaxTaskGroups);
		m_taskGroups[id].Init(name, func);
		return id;
	}

	RDE_FORCEINLINE void AddTask(TaskGroupID groupID, TaskData* data)
	{
		RDE_ASSERT(groupID < m_numTaskGroups);
		TaskGroup& group = m_taskGroups[groupID];
		data->m_groupID = groupID;
		if (m_workerThreads.empty())
		{
			group.m_func(data);
		}
		else
		{
			++m_numPendingTasks;
			{
				LockGuard<Mutex> lock(m_taskQueueMutex);
				m_taskQueue.Push(data);
			}
			SignalWork();
		}
	}

	void WaitForAllTasks()
	{
		while (m_numPendingTasks != 0)
			ProcessTasks();
	}

	void ProcessTasks()
	{
		TaskData* task(0);
		do
		{
			task = PopTask();
			if (task)
			{
				m_taskGroups[task->m_groupID].m_func(task);
				--m_numPendingTasks;
			}
		}
		while (task);
	}

	void SignalWork()
	{
		const Gate::State state = m_taskQueueGate.GetState();
		if (state == kStateEmpty)
			m_taskQueueGate.UpdateIfStateNotEqual(kStateFull, kStateOpen);
	}

	inline TaskData* PopTask()
	{
		LockGuard<Mutex> lock(m_taskQueueMutex);
		return m_taskQueue.Pop();
	}
	inline TaskGroup& GetTaskGroup(int32 id)
	{
		RDE_ASSERT(id < m_numTaskGroups);
		return m_taskGroups[id];
	}
	int NumWorkerThreads() const
	{
		return m_workerThreads.size();
	}
	bool WaitForWork()
	{
		while (true)
		{
			Gate::State state = m_taskQueueGate.GetState();
			switch (state)
			{
			case kStateEmpty:
				m_taskQueueGate.Wait();
				return true;

			case kStateFull:
				{
					const Gate::State busy = Gate::State(this);
					m_taskQueueGate.UpdateIfStateEqual(busy, kStateFull);
					if (m_taskQueueGate.GetState() == busy)
					{
						m_taskQueueGate.UpdateIfStateEqual(kStateEmpty, busy);
						continue;
					}
					m_taskQueueGate.UpdateIfStateEqual(kStateFull, busy);
				}
				return false;
				// Open
			default:
				return false;
			}
		}
	}
	float GetAveragePopOverhead() const
	{
		double avgOverhead(0.0);
		for (WorkerThreads::const_iterator it = m_workerThreads.begin(); it != m_workerThreads.end(); ++it)
		{
			if ((*it)->m_numPopCalls)
				avgOverhead += double((*it)->m_totalPopOverhead) / (*it)->m_numPopCalls;
		}
		if(!m_workerThreads.empty())
			avgOverhead /= double(m_workerThreads.size());

		return float(avgOverhead);
	}

	typedef rde::fixed_vector<WorkerThread*, 8, false>	WorkerThreads;

	TaskQueue			m_taskQueue;
	int32				m_numTaskGroups;
	Gate				m_taskQueueGate;
	Mutex				m_taskQueueMutex;
	volatile uint8		m_stopped; 

	Atomic<Atomic32>	m_numPendingTasks;
	TaskGroup			m_taskGroups[kMaxTaskGroups];
	WorkerThreads		m_workerThreads;
	Mutex				m_workerThreadListMutex;
}; 

ThreadPool_Lock::ThreadPool_Lock(int numThreads)
:	m_impl(new Impl)
{
	m_impl->SetNumThreads(numThreads);
}
ThreadPool_Lock::~ThreadPool_Lock()
{
}

TaskGroupID ThreadPool_Lock::CreateTaskGroup(const char* name, TaskFunc func)
{
	RDE_ASSERT(name);
	RDE_ASSERT(func);
	return m_impl->CreateTaskGroup(name, func);
}
void ThreadPool_Lock::AddTask(TaskGroupID groupID, TaskData* data)
{
	m_impl->AddTask(groupID, data);
}
void ThreadPool_Lock::WaitForAllTasks()
{
	m_impl->WaitForAllTasks();
}
float ThreadPool_Lock::GetAveragePopOverhead() const
{
	return m_impl->GetAveragePopOverhead();
}

} // rde


namespace
{
void WorkerThread::ProcessTasks()
{
	rde::Thread::SetName("WorkerThread");
	rde::TaskData* task(0);

	// Possible race, but not critical. This can only change to true.
	// If not now, we'll leave in next iteration/from failure loop.
	while (!m_threadPool->m_stopped)
	{
		while (task)
		{
			rde::ThreadPool_Lock::Impl::TaskGroup& taskGroup = m_threadPool->GetTaskGroup(task->m_groupID);
			rde::uint64 taskTicks = __rdtsc();
			taskGroup.m_func(task);
			taskTicks = __rdtsc() - taskTicks;
			--m_threadPool->m_numPendingTasks;

			rde::uint64 t = rde::GetCPUTicks();
			task = m_threadPool->PopTask();
			m_totalPopOverhead += rde::GetCPUTicks() - t;
			++m_numPopCalls;
		}
		
		// Idle spinning. We spin a little, yield from time to time and
		// finally go to sleep if no new task arrives.
		for (int failureCount = 0; /**/; ++failureCount)
		{
			if (m_threadPool->m_stopped)
				break;

			rde::uint64 t = rde::GetCPUTicks();
			task = m_threadPool->PopTask();
			m_totalPopOverhead += rde::GetCPUTicks() - t;
			++m_numPopCalls;
			if (task)
				break;

			rde::Thread::MachinePause(80);
			// Try few times before yielding, it's relatively cheap.
			// Reading num workers may mean a data race, but it's not 
			// dangerous, so let it be.
			const int yieldThreshold = m_threadPool->NumWorkerThreads() * 2;
			if (failureCount >= yieldThreshold)
			{
				rde::Thread::YieldCurrentThread();
				if (failureCount >= yieldThreshold + 100)
				{
					m_threadPool->WaitForWork();
					failureCount = 0;
				}
			}
		} // failure loop
	}
//#if TRACE_CONTAINER_OVERHEAD
//	if (popCalls)
//	{
//		double avgOverhead = double(overhead) / popCalls;
//		double avgOverheadUs = avgOverhead / (rde::GetCPUTicksPerSecond() / 1e+6);
//		rde::Console::Profilef("Pop overhead: %f us (%f ticks) per operation (%d)\n", avgOverheadUs, 
//			avgOverhead, popCalls);
//	}
//#endif
}

} // anonymous namespace


