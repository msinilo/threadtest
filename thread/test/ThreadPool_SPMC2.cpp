#include "ThreadPool_SPMC2.h"
#include "thread/Gate.h"
#include "rdestl/fixed_vector.h"
#include "core/Atomic.h"
#include "core/BitMath.h"
#include "core/Console.h"
#include "core/CPU.h"
#include "core/LockGuard.h"
#include "core/Mutex.h"
#include "core/Thread.h"

#define TRACE_CONTAINER_OVERHEAD	1
namespace
{
const rde::Gate::State kStateEmpty(0);
const rde::Gate::State kStateFull(-1);
const rde::Gate::State kStateOpen(-2);

class WorkerThread : public rde::Thread
{
public:
	explicit WorkerThread(rde::ThreadPool_SPMC2::Impl* tp):	m_threadPool(tp) {}
	void ProcessTasks();

	rde::ThreadPool_SPMC2::Impl*	m_threadPool;
};

class SPMCQueue2
{
public:
	SPMCQueue2()
	:	m_pushIndex(0),
		m_popIndex(0)
	{
	}
	~SPMCQueue2()
	{
	}
	
	RDE_FORCEINLINE rde::uint8* Pop()
	{
		/*Atomic32 index = Interlocked::Increment(&m_popIndex) - 1;
		if (index < Load_Relaxed(m_pushIndex))
		{
			t = m_buffer.m_data[index];
			return true;
		}
		return false;*/

		rde::Atomic32 index = rde::Interlocked::Increment(&m_popIndex) - 1;
			//rde::Interlocked::FetchAndAdd(&m_popIndex, m_itemSize);
		if (index < rde::Load_Relaxed(m_pushIndex))
		{
			return m_buffer + (index * m_itemSize);
		}
		return 0;
	}
	void SetBuffer(rde::uint8* buffer, int numItems, int itemSize)
	{
		m_buffer = buffer;
		m_pushIndex = numItems;// * itemSize;
		m_itemSize = itemSize;
	}

private:
	SPMCQueue2(const SPMCQueue2&);
	SPMCQueue2& operator=(const SPMCQueue2&);

	long			m_pushIndex;
	rde::Atomic32	m_popIndex;
	long			m_itemSize;
	rde::uint8*		m_buffer;
};


} // namespace

namespace rde
{
struct ThreadPool_SPMC2::Impl
{
	static const int32	kMaxTaskGroups	= 64;

	struct TaskGroup
	{
		TaskGroup()
		:	m_name(0),
			m_func(0)
		{
		}
		void Init(const char* name, ThreadPool_SPMC2::TaskFunc func)
		{
			m_name = name;
			m_func = func;
		}
		const char*					m_name;
		ThreadPool_SPMC2::TaskFunc	m_func;
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

		//DeleteWorkerThreads();
		m_stopped = 0;
		LockGuard<Mutex> lock(m_workerThreadListMutex);
		m_workerThreads.reserve(numThreads);

		//m_taskQueueGate.UpdateIfStateNotEqual(kStateEmpty, kStateEmpty);
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

	void StartTasks(TaskGroupID /*groupID*/, TaskData* data, int numTasks, int dataPitch)
	{
		m_tasks.SetBuffer((uint8*)data, numTasks, dataPitch);
		m_numPendingTasks = numTasks;
	}
	void WaitForAllTasks()
	{
		SignalWork();
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

	RDE_FORCEINLINE TaskData* PopTask()
	{
		return (TaskData*)m_tasks.Pop();
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

	typedef rde::fixed_vector<WorkerThread*, 8, false>	WorkerThreads;
	typedef SPMCQueue2									Tasks;

	Tasks				m_tasks;
	int32				m_numTaskGroups;
	Gate				m_taskQueueGate;
	volatile uint8		m_stopped; 

	Atomic<Atomic32>	m_numPendingTasks;
	TaskGroup			m_taskGroups[kMaxTaskGroups];
	WorkerThreads		m_workerThreads;
	Mutex				m_workerThreadListMutex;
}; 

ThreadPool_SPMC2::ThreadPool_SPMC2(int numThreads)
:	m_impl(new Impl)
{
	m_impl->SetNumThreads(numThreads);
}
ThreadPool_SPMC2::~ThreadPool_SPMC2()
{
}

TaskGroupID ThreadPool_SPMC2::CreateTaskGroup(const char* name, TaskFunc func)
{
	RDE_ASSERT(name);
	RDE_ASSERT(func);
	return m_impl->CreateTaskGroup(name, func);
}
void ThreadPool_SPMC2::StartTasks(TaskGroupID groupID, TaskData* data, int numTasks, int dataPitch)
{
	m_impl->StartTasks(groupID, data, numTasks, dataPitch);
}
void ThreadPool_SPMC2::WaitForAllTasks()
{
	m_impl->WaitForAllTasks();
}

} // rde


namespace
{
void WorkerThread::ProcessTasks()
{
	rde::Thread::SetName("WorkerThread_SPMC");
	rde::TaskData* task(0);

	m_threadPool->WaitForWork();

//	double totalMs(0.0);
//	int numProcessedTasks(0);

//	rde::Console::Printf("Popped so far: %d\n", m_threadPool->m_tasks.NumPopped());

	int popCalls(0);
	rde::uint64 overhead(0);

	// Possible race, but not critical. This can only change to true.
	// If not now, we'll leave in next iteration/from failure loop.
	while (!m_threadPool->m_stopped)
	{
		while (task)
		{
			rde::ThreadPool_SPMC2::Impl::TaskGroup& taskGroup = m_threadPool->GetTaskGroup(task->m_groupID);
			rde::uint64 taskTicks = __rdtsc();
			taskGroup.m_func(task);
			taskTicks = __rdtsc() - taskTicks;
			--m_threadPool->m_numPendingTasks;
//			totalMs += double(taskTicks);
//			++numProcessedTasks;

			rde::uint64 t = rde::GetCPUTicks();
			task = m_threadPool->PopTask();
			overhead += rde::GetCPUTicks() - t;
			++popCalls;
		}
		
		// Idle spinning. We spin a little, yield from time to time and
		// finally go to sleep if no new task arrives.
		for (int failureCount = 0; /**/; ++failureCount)
		{
			if (m_threadPool->m_stopped)
				break;

			rde::uint64 t = rde::GetCPUTicks();
			task = m_threadPool->PopTask();
			overhead += rde::GetCPUTicks() - t;
			++popCalls;
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
	} // while (!m_threadPool->m_stopped)
//	rde::Console::Profilef("Total ms for tasks: %f\n", totalMs);

#if TRACE_CONTAINER_OVERHEAD
	if (popCalls)
	{
		double avgOverhead = double(overhead) / popCalls;
		double avgOverheadUs = avgOverhead / (rde::GetCPUTicksPerSecond() / 1e+6);
		rde::Console::Profilef("Pop overhead: %f us (%f ticks) per operation (%d)\n", avgOverheadUs, 
			avgOverhead, popCalls);
	}
#endif
}

} // anonymous namespace

