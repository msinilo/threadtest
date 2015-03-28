#include "ThreadPool_MPMCQueue.h"
#include "ThreadPool_Profiler.h"
#include "thread/Gate.h"
#include "thread/LockFreeQueue.h"
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
	explicit WorkerThread(rde::ThreadPool_MPMC_Queue::Impl* tp)
	:	m_threadPool(tp),
		m_totalPopOverhead(0),
		m_numPopCalls(0)
	{}
	void ProcessTasks();

	rde::ThreadPool_MPMC_Queue::Impl*	m_threadPool;

	rde::uint64									m_totalPopOverhead;
	int											m_numPopCalls;
};

} // namespace

namespace rde
{
struct ThreadPool_MPMC_Queue::Impl
{
	static const int32	kMaxTaskGroups	= 64;

	struct TaskGroup
	{
		TaskGroup()
		:	m_name(0),
			m_func(0)
		{
		}
		void Init(const char* name, ThreadPool_MPMC_Queue::TaskFunc func)
		{
			m_name = name;
			m_func = func;
		}
		const char*						m_name;
		ThreadPool_MPMC_Queue::TaskFunc	m_func;
	};

	Impl()
	:	m_numTaskGroups(0),
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
			m_tasks.Enqueue(data);
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
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_START);
				m_taskGroups[task->m_groupID].m_func(task);
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_END);
				--m_numPendingTasks;
			}
		}
		while (task);
	}

	void SignalWork()
	{
		rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::SIGNAL_WORK);
		const Gate::State state = m_taskQueueGate.GetState();
		if (state == kStateEmpty)
			m_taskQueueGate.UpdateIfStateNotEqual(kStateFull, kStateOpen);
	}

	inline TaskData* PopTask()
	{
		return m_tasks.Dequeue();
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
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::WAIT_FOR_WORK_START);
				m_taskQueueGate.Wait();
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::WAIT_FOR_WORK_END);
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
	typedef rde::LockFreeQueue<TaskData>				TaskQueue;

	TaskQueue			m_tasks;
	int32				m_numTaskGroups;
	Gate				m_taskQueueGate;
	volatile uint8		m_stopped; 

	Atomic<Atomic32>	m_numPendingTasks;
	TaskGroup			m_taskGroups[kMaxTaskGroups];
	WorkerThreads		m_workerThreads;
	Mutex				m_workerThreadListMutex;
}; 

ThreadPool_MPMC_Queue::ThreadPool_MPMC_Queue(int numThreads)
:	m_impl(new Impl)
{
	m_impl->SetNumThreads(numThreads);
}
ThreadPool_MPMC_Queue::~ThreadPool_MPMC_Queue()
{
}

TaskGroupID ThreadPool_MPMC_Queue::CreateTaskGroup(const char* name, TaskFunc func)
{
	RDE_ASSERT(name);
	RDE_ASSERT(func);
	return m_impl->CreateTaskGroup(name, func);
}
void ThreadPool_MPMC_Queue::AddTask(TaskGroupID groupID, TaskData* data)
{
	m_impl->AddTask(groupID, data);
}
void ThreadPool_MPMC_Queue::WaitForAllTasks()
{
	m_impl->WaitForAllTasks();
}
float ThreadPool_MPMC_Queue::GetAveragePopOverhead() const
{
	return m_impl->GetAveragePopOverhead();
}


} // rde


namespace
{
void WorkerThread::ProcessTasks()
{
	rde::Thread::SetName("WorkerThread_MPMC_Queue");
	rde::TaskData* task(0);

	rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::WORKER_THREAD_START);

	// Possible race, but not critical. This can only change to true.
	// If not now, we'll leave in next iteration/from failure loop.
	while (!m_threadPool->m_stopped)
	{
		while (task)
		{
			rde::ThreadPool_MPMC_Queue::Impl::TaskGroup& taskGroup = m_threadPool->GetTaskGroup(task->m_groupID);
			rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_START);
			taskGroup.m_func(task);
			rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_END);
			--m_threadPool->m_numPendingTasks;

#if TRACE_CONTAINER_OVERHEAD
			rde::uint64 t = rde::GetCPUTicks();
#endif
			task = m_threadPool->PopTask();
#if TRACE_CONTAINER_OVERHEAD
			m_totalPopOverhead += rde::GetCPUTicks() - t;
			++m_numPopCalls;
#endif
		}

		// Idle spinning. We spin a little, yield from time to time and
		// finally go to sleep if no new task arrives.
		for (int failureCount = 0; /**/; ++failureCount)
		{
			if (m_threadPool->m_stopped)
				break;

#if TRACE_CONTAINER_OVERHEAD
			rde::uint64 t = rde::GetCPUTicks();
#endif
			task = m_threadPool->PopTask();
#if TRACE_CONTAINER_OVERHEAD
			m_totalPopOverhead += rde::GetCPUTicks() - t;
			++m_numPopCalls;
#endif
			if (task)
			{
				break;
			}

			if ((failureCount & 0xF) == 0)
				rde::Thread::MachinePause(80);

			// Try few times before yielding, it's relatively cheap.
			// Reading num workers may mean a data race, but it's not 
			// dangerous, so let it be.
			const int yieldThreshold = m_threadPool->NumWorkerThreads() * 2;
			if (failureCount >= yieldThreshold && m_threadPool->m_numPendingTasks == 0)
			{
				if (failureCount >= yieldThreshold + 100)
				{
					m_threadPool->WaitForWork();
					failureCount = 0;
				}
			}
		} // failure loop
	}
}

} // anonymous namespace


