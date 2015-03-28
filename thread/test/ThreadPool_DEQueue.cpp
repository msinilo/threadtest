#include "ThreadPool_DEQueue.h"
#include "thread/Gate.h"
#include "thread/LockFreeDEQueue.h"
#include "rdestl/fixed_vector.h"
#include "core/Atomic.h"
#include "core/BitMath.h"
#include "core/Console.h"
#include "core/CPU.h"
#include "core/LockGuard.h"
#include "core/Mutex.h"
#include "core/Random.h"
#include "core/Thread.h"

namespace
{
const rde::Gate::State kStateEmpty(0);
const rde::Gate::State kStateFull(-1);
const rde::Gate::State kStateOpen(-2);

RDE_THREADLOCAL int t_schedulerIndex = -1;

// Very simple, single threaded mempool.
template<typename T>
class FreeList
{
public:
	explicit FreeList(int capacity)
	:	m_capacity(capacity),
		m_freeList(0)
	{
		PrepareNewChunk();
	}
	~FreeList()
	{
		for (int i = 0; i < m_chunks.size(); ++i)
			delete[] m_chunks[i];
		m_chunks.clear();
	}

	T* Allocate()
	{
		if (m_freeList == 0)
			PrepareNewChunk();

		MemElement* ret = m_freeList;
		m_freeList = m_freeList->next;
		return (T*)(&ret->rawData[0]);
	}
	void Free(void* ptr)
	{
		RDE_ASSERT(ptr);
		MemElement* elem = (MemElement*)ptr;
		elem->next = m_freeList;
		m_freeList = elem;
	}

private:
	RDE_FORBID_COPY(FreeList);

	union MemElement
	{
		typedef typename rde::aligned_as<T>::res	ElemType;
		MemElement* next;
		ElemType	rawData[sizeof(T) / sizeof(ElemType)];
	};

	void PrepareNewChunk()
	{
		MemElement* chunk = new MemElement[m_capacity];
		m_chunks.push_back(chunk);
		for (int i = 0; i < m_capacity; ++i, ++chunk)
			Free(chunk);
	}
	int							m_capacity;
	MemElement*					m_freeList;
	rde::vector<MemElement*>	m_chunks;
};

} // namespace

namespace rde
{
class Scheduler
{
public:
	static const size_t MAX_FREE_LIST_TASK_SIZE	= 128;
	typedef rde::uint8	TaskBuffer[MAX_FREE_LIST_TASK_SIZE];

	Scheduler()
	:	m_localTaskFreeList(1024),
		m_tasks(16384),
		m_totalPopOverhead(0),
		m_totalStealOverhead(0),
		m_numPopCalls(0),
		m_numStealCalls(0)
	{
	}

	void* AllocateTask(size_t bytes)
	{
		RDE_ASSERT(bytes < MAX_FREE_LIST_TASK_SIZE);
		void* ret = m_localTaskFreeList.Allocate();
		return ret;
	}
	void DeleteTask(rde::TaskData_DEQueue* task)
	{
		task->~TaskData_DEQueue();
		FreeTask(task);
	}
	void FreeTask(rde::TaskData_DEQueue* task)
	{
		m_localTaskFreeList.Free(task);
	}

	void WaitForGroup(rde::TaskGroup_DEQueue&);
	bool IsWorkerThread() const	{ return m_index > 0; }

	inline rde::TaskData_DEQueue* StealTask()
	{
		return m_tasks.IsEmpty() ? 0 : m_tasks.PopTop();
	}

	typedef rde::LockFreeDEQueue<rde::TaskData_DEQueue>				TaskQueue;

	FreeList<TaskBuffer>			m_localTaskFreeList;
	rde::ThreadPool_DEQueue::Impl*	m_threadPool;
	int								m_index;
	//rde::KISSRandom					m_rng;
	TaskQueue						m_tasks;

	rde::uint64						m_totalPopOverhead;
	rde::uint64						m_totalStealOverhead;
	int								m_numPopCalls;
	int								m_numStealCalls;
};
} // rde

namespace
{
class WorkerThread : public rde::Thread
{
public:
	explicit WorkerThread(rde::ThreadPool_DEQueue::Impl* tp)
	:	m_threadPool(tp)
	{}
	~WorkerThread();
	void ProcessTasks();

	rde::ThreadPool_DEQueue::Impl*	m_threadPool;
	rde::Scheduler					m_scheduler;
};

} // namespace

namespace rde
{
void* internal::TaskAllocatorProxy::Allocate(size_t bytes) const
{
	return m_scheduler.AllocateTask(bytes);
}
void internal::TaskAllocatorProxy::Free(void* ptr) const
{
	return m_scheduler.FreeTask((TaskData_DEQueue*)ptr);
}

struct TaskGroup_DEQueue
{
	TaskGroup_DEQueue()
	:	m_name(0),
		m_func(0)
	{
		m_numActiveTasks = 0;
	}
	void Init(const char* name, ThreadPool_DEQueue::TaskFunc func)
	{
		m_name = name;
		m_func = func;
	}

	void AddTask()
	{
		RDE_ASSERT(m_numActiveTasks >= 0);
		++m_numActiveTasks;
	}
	void OnTaskDone()
	{
		RDE_ASSERT(m_numActiveTasks > 0);
		--m_numActiveTasks;
	}
	bool IsDone() const { return m_numActiveTasks == 0; }

	const char*						m_name;
	ThreadPool_DEQueue::TaskFunc	m_func;
	Atomic<Atomic32>				m_numActiveTasks;
};

struct ThreadPool_DEQueue::Impl
{
	static const int32	kMaxTaskGroups	= 64;

	Impl()
	:	m_numTaskGroups(0),
		m_stopped(0)
	{
	}
	~Impl()
	{
		DeleteWorkerThreads();
	}

	// !Shouldn't change during run-time.
	void SetNumThreads(int numThreads)
	{	
		t_schedulerIndex = 0;
		m_schedulers.resize(numThreads + 1);
		m_schedulers[0] = &m_mainScheduler;
		m_mainScheduler.m_index = 0;
		m_mainScheduler.m_threadPool = this;
		m_roundRobinIndex = 0;

		if (numThreads == m_workerThreads.size())
			return;

		DeleteWorkerThreads();
		m_stopped = 0;
		LockGuard<Mutex> lock(m_workerThreadListMutex);
		m_workerThreads.reserve(numThreads);

		while (m_workerThreads.size() < numThreads)
		{
			WorkerThread* worker = new WorkerThread(this);
			const uint32 workerAffinityMask = RDE_BIT(m_workerThreads.size() - 1);
			worker->SetAffinityMask(workerAffinityMask);
			m_workerThreads.push_back(worker);
			const int index = m_workerThreads.size();
			m_schedulers[index] = &worker->m_scheduler;
			m_schedulers[index]->m_index = index;
			m_schedulers[index]->m_threadPool = this;
		}
		for (WorkerThreads::iterator worker = m_workerThreads.begin(); worker != m_workerThreads.end(); ++worker)
		{
			(*worker)->Start<WorkerThread, &WorkerThread::ProcessTasks>(*worker);
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

	RDE_FORCEINLINE void AddTask(TaskGroupID groupID, TaskData_DEQueue* data)
	{
		RDE_ASSERT(groupID < m_numTaskGroups);
		TaskGroup_DEQueue& group = m_taskGroups[groupID];
		data->m_group = &group;
		data->m_groupID = groupID;
		group.AddTask();
		m_schedulers[t_schedulerIndex]->m_tasks.PushBottom(data);
		SignalWork();
	}

	void SignalWork()
	{
		const Gate::State state = m_taskQueueGate.GetState();
		if (state == kStateEmpty)
			m_taskQueueGate.UpdateIfStateNotEqual(kStateFull, kStateOpen);
	}

	void WaitForGroup(TaskGroupID groupID)
	{
		RDE_ASSERT(groupID < m_numTaskGroups);
		TaskGroup_DEQueue& group = m_taskGroups[groupID];
		m_mainScheduler.WaitForGroup(group);
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
	Scheduler* GetVictim(int indexToIgnore) 
	{
		bool indexOK(false);
		const int numVictims = m_schedulers.size();
		Atomic32 retIndex(0);
		while (!indexOK)
		{
			retIndex = m_roundRobinIndex++;
			if (m_roundRobinIndex > 100000)
				m_roundRobinIndex = 0;
			retIndex %= numVictims;
			indexOK = (retIndex != indexToIgnore);
		}
		return m_schedulers[retIndex];
	}

	rde::pair<float, float> GetAveragePopStealOverhead() const
	{
		double avgPopOverhead(0.0);			
		double avgStealOverhead(0.0);
		for (WorkerThreads::const_iterator it = m_workerThreads.begin(); it != m_workerThreads.end(); ++it)
		{
			if ((*it)->m_scheduler.m_numPopCalls)
				avgPopOverhead += double((*it)->m_scheduler.m_totalPopOverhead) / (*it)->m_scheduler.m_numPopCalls;

			if ((*it)->m_scheduler.m_numStealCalls)
				avgStealOverhead += double((*it)->m_scheduler.m_totalStealOverhead) / (*it)->m_scheduler.m_numStealCalls;
		}
		if(!m_workerThreads.empty())
		{
			avgPopOverhead /= double(m_workerThreads.size());
			avgStealOverhead /= double(m_workerThreads.size());
		}
		return rde::pair<float, float>(float(avgPopOverhead), float(avgStealOverhead));
	}


	typedef rde::fixed_vector<WorkerThread*, 8, false>	WorkerThreads;
	typedef rde::fixed_vector<Scheduler*, 8, false>		Schedulers;

	int32				m_numTaskGroups;
	Gate				m_taskQueueGate;
	volatile uint8		m_stopped; 

	TaskGroup_DEQueue	m_taskGroups[kMaxTaskGroups];
	WorkerThreads		m_workerThreads;
	Mutex				m_workerThreadListMutex;
	Scheduler			m_mainScheduler;
	Schedulers			m_schedulers;
	Atomic32			m_roundRobinIndex;
}; 

ThreadPool_DEQueue::ThreadPool_DEQueue(int numThreads)
:	m_impl(new Impl)
{
	m_impl->SetNumThreads(numThreads);
}
ThreadPool_DEQueue::~ThreadPool_DEQueue()
{
}

TaskGroupID ThreadPool_DEQueue::CreateTaskGroup(const char* name, TaskFunc func)
{
	RDE_ASSERT(name);
	RDE_ASSERT(func);
	return m_impl->CreateTaskGroup(name, func);
}
void ThreadPool_DEQueue::AddTask(TaskGroupID groupID, TaskData_DEQueue* data)
{
	m_impl->AddTask(groupID, data);
}
void ThreadPool_DEQueue::WaitForGroup(TaskGroupID groupID)
{
	m_impl->WaitForGroup(groupID);
}
internal::TaskAllocatorProxy ThreadPool_DEQueue::AllocateTask()
{
	return internal::TaskAllocatorProxy(*m_impl->m_schedulers[t_schedulerIndex]);
}
rde::pair<float, float> ThreadPool_DEQueue::GetAveragePopStealOverhead() const
{
	return m_impl->GetAveragePopStealOverhead();
}

void Scheduler::WaitForGroup(rde::TaskGroup_DEQueue& group)
{
	rde::TaskData_DEQueue* task(0);

	const bool isWorker = IsWorkerThread();
	// Possible race, but not critical. This can only change to true.
	// If not now, we'll leave in next iteration/from failure loop.
	while (!m_threadPool->m_stopped)
	{
		if (!isWorker && group.IsDone())
			break;

		// Process local task queue 
		do
		{
			while (task)
			{
				rde::TaskData_DEQueue* nextTask = task->m_group->m_func(task);
				if (nextTask == 0)
				{
					task->m_group->OnTaskDone(); 
					DeleteTask(task);
				}
				if (!isWorker && group.IsDone())
					return;

				task = nextTask;
			}
			rde::uint64 t = rde::GetCPUTicks();
			task = m_tasks.PopBottom();
			m_totalPopOverhead += rde::GetCPUTicks() - t;
			++m_numPopCalls;
		} while (task);

		// Work stealing loop.
		const int numVictims = m_threadPool->m_schedulers.size();
		for (int failureCount = 0; ; ++failureCount)
		{
			if (m_threadPool->m_stopped || (!isWorker && group.IsDone()))
				break;

			Scheduler* victim = m_threadPool->GetVictim(m_index);

			rde::uint64 t = rde::GetCPUTicks();
			rde::TaskData_DEQueue* stolenTask = victim->StealTask();
			m_totalStealOverhead += rde::GetCPUTicks() - t;
			++m_numStealCalls;
			if (stolenTask)
			{
				task = stolenTask;
				break;
			}

			rde::Thread::MachinePause(80);
			// Try few times before yielding, it's relatively cheap.
			// Reading num workers may mean a data race, but it's not 
			// dangerous, so let it be.
			const int yieldThreshold = numVictims * 2;
			if (failureCount >= yieldThreshold)
			{
				rde::Thread::YieldCurrentThread();
				if (failureCount >= yieldThreshold + 100)
				{
					// Main thread should never sleep.
					if (isWorker && m_threadPool->WaitForWork())
						failureCount = 0;
					else
						failureCount = yieldThreshold;
				}
			}
		} // failure loop
	}

#if TRACE_CONTAINER_OVERHEAD
//	if (popCalls || stealCalls)
//	{
//		double avgPopOverhead = (popCalls ? (double(popOverhead) / popCalls) : 0.0);
//		double avgPopOverheadUs = avgPopOverhead / (rde::GetCPUTicksPerSecond() / 1e+6);
//		double avgStealOverhead = (stealCalls ? (double(stealOverhead) / stealCalls) : 0.0);
//		double avgStealOverheadUs = avgStealOverhead / (rde::GetCPUTicksPerSecond() / 1e+6);
//		rde::Console::Profilef("Pop overhead: %f us (%f ticks) per operation [%d], steal: %f us (%f ticks) [%d]\n", 
//			avgPopOverheadUs, avgPopOverhead, popCalls, avgStealOverheadUs, avgStealOverhead, stealCalls);
//		m_averagePopOverhead = float(avgPopOverhead);
//		m_averageStealOverhead = float(avgStealOverhead);
//	}
#endif
}

} // rde


namespace
{
void WorkerThread::ProcessTasks()
{
	rde::Thread::SetName("WorkerThread_DEQueue");
	t_schedulerIndex = m_scheduler.m_index;

	rde::TaskGroup_DEQueue dummyGroup;
	dummyGroup.AddTask();	// dummy task, to make worker loop until application stops.
	m_scheduler.WaitForGroup(dummyGroup);
}

WorkerThread::~WorkerThread()
{
	Stop();
}

} // anonymous namespace


