#include "ThreadPool_SPSC.h"
#include "ThreadPool_Profiler.h"
#include "thread/Gate.h"
#include "thread/SPSCQueue.h"
#include "rdestl/fixed_vector.h"
#include "core/Atomic.h"
#include "core/Console.h"
#include "core/CPU.h"
#include "core/Thread.h"

namespace
{
#define GLOBAL_TASK_QUEUE	0

const rde::Gate::State kStateEmpty(0);
const rde::Gate::State kStateFull(-1);
const rde::Gate::State kStateOpen(-2);

class WorkerThread : public rde::Thread
{
public:
	typedef rde::SPSCQueue<rde::TaskData*, 16384, false>			TaskQueue;

	explicit WorkerThread(rde::ThreadPool_SPSC::Impl* tp, int workerIndex)
	:	m_threadPool(tp),
		m_workerIndex(workerIndex),
		m_tasksDone(0),
		m_totalPopOverhead(0),
		m_numPopCalls(0)
	{
		m_needsWork = 0;
	}
	void ProcessTasks();

	RDE_FORCEINLINE void AddTask(rde::TaskData* task)
	{
		m_taskQueue.Push(task);
	}
	RDE_FORCEINLINE rde::TaskData* PopTask()
	{
		rde::TaskData* ret(0);
		m_taskQueue.Pop(ret);
		return ret;
	}

	rde::ThreadPool_SPSC::Impl*	m_threadPool;
	int							m_workerIndex;
	TaskQueue					m_taskQueue;
	volatile rde::uint32 		m_needsWork;

	int							m_tasksDone;
	rde::uint64					m_totalTaskTicks;

	rde::uint64					m_totalPopOverhead;
	int							m_numPopCalls;
};

} // anonymous

namespace rde
{
struct ThreadPool_SPSC::Impl
{
	static const int32	kMaxTaskGroups	= 64;

	struct TaskGroup
	{
		TaskGroup()
		:	m_name(0),
			m_func(0)
		{
		}
		void Init(const char* name, ThreadPool_SPSC::TaskFunc func)
		{
			m_name = name;
			m_func = func;
		}
		const char*					m_name;
		ThreadPool_SPSC::TaskFunc	m_func;
	};

	Impl()
	:	m_numTaskGroups(0),
		m_nextActiveWorkerIndex(0),
		m_stopped(0)
	{
		m_numPendingTasks = 0;
		m_allTasksDone = 1;
	}
	~Impl()
	{
		DeleteWorkerThreads();
	}

	// !Shouldn't change during run-time.
	void SetNumThreads(int numThreads)
	{
		m_stopped = 0;
		m_workerThreads.reserve(numThreads);
		while (m_workerThreads.size() < numThreads)
		{
			WorkerThread* worker = new WorkerThread(this, m_workerThreads.size());
			m_workerThreads.push_back(worker);
			worker->Start<WorkerThread, &WorkerThread::ProcessTasks>(worker);
		}
	}

	void DeleteWorkerThreads()
	{
		m_stopped = 1;
		m_workGate.UpdateIfStateNotEqual(kStateOpen, kStateOpen);
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

	void AddTask(TaskGroupID groupID, TaskData* data)
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
			m_allTasksDone = 0;
			++m_numPendingTasks;
			// TODO: smarter way of distributing tasks.
#if GLOBAL_TASK_QUEUE
			m_globalTaskQueue.push_back(data);
#else
			m_workerThreads[m_nextActiveWorkerIndex]->AddTask(data);
			++m_nextActiveWorkerIndex;
			if (m_nextActiveWorkerIndex >= m_workerThreads.size())
				m_nextActiveWorkerIndex = 0;
#endif
			//m_distributor->AddTask(data);
			SignalWork();
		}
	}

	void WaitForAllTasks()
	{
		while (m_numPendingTasks != 0)
		{
#if GLOBAL_TASK_QUEUE
			// First, distribute amongst threads.
			bool workersActive(true);
			while (workersActive)
			{
				for (WorkerThreads::const_iterator it = m_workerThreads.begin(); it != m_workerThreads.end(); ++it)
				{
					if (m_globalTaskQueue.empty())
					{
						workersActive = false;
						break;
					}

					if ((*it)->m_needsWork)
					{
						TaskData* task = m_globalTaskQueue.back();
						m_globalTaskQueue.pop_back();
						(*it)->AddTask(task);
					}
					else
						workersActive = false;
				}
			}
			if (!m_globalTaskQueue.empty())
			{
				TaskData* task = m_globalTaskQueue.back();
				m_globalTaskQueue.pop_back();
				TaskGroup& taskGroup = GetTaskGroup(task->m_groupID);
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_START);
				taskGroup.m_func(task);
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_END);
				--m_numPendingTasks;
			}
#endif
			Thread::MachinePause(20);
		}
		m_allTasksDone = 1;
		m_nextActiveWorkerIndex = 0;
	}

	void SignalWork()
	{
		const Gate::State state = m_workGate.GetState();
		if (state == kStateEmpty)
			m_workGate.UpdateIfStateNotEqual(kStateFull, kStateOpen);
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
			Gate::State state = m_workGate.GetState();
			switch (state)
			{
			case kStateEmpty:
				m_workGate.Wait();
				return true;

			case kStateFull:
				{
					const Gate::State busy = Gate::State(this);
					m_workGate.UpdateIfStateEqual(busy, kStateFull);
					if (m_workGate.GetState() == busy)
					{
						m_workGate.UpdateIfStateEqual(kStateEmpty, busy);
						continue;
					}
					m_workGate.UpdateIfStateEqual(kStateFull, busy);
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

		return static_cast<float>(avgOverhead);
	}
	WorkerThread* GetWorker(int index) const
	{
		return m_workerThreads[index];
	}

	typedef rde::fixed_vector<WorkerThread*, 32, false>	WorkerThreads;

	int32				m_numTaskGroups;
	Gate				m_workGate;
	uint8				m_nextActiveWorkerIndex;
	volatile uint8		m_stopped;

	Atomic<Atomic32>	m_numPendingTasks;
	Atomic32			m_allTasksDone;
	TaskGroup			m_taskGroups[kMaxTaskGroups];
	WorkerThreads		m_workerThreads;
#if GLOBAL_TASK_QUEUE
	vector<TaskData*>	m_globalTaskQueue;
#endif
}; 

ThreadPool_SPSC::ThreadPool_SPSC(int numThreads)
:	m_impl(new Impl)
{
	m_impl->SetNumThreads(numThreads);
}
ThreadPool_SPSC::~ThreadPool_SPSC()
{
}

TaskGroupID ThreadPool_SPSC::CreateTaskGroup(const char* name, TaskFunc func)
{
	RDE_ASSERT(name);
	RDE_ASSERT(func);
	return m_impl->CreateTaskGroup(name, func);
}
void ThreadPool_SPSC::AddTask(TaskGroupID groupID, TaskData* data)
{
	RDE_ASSERT(data);
	m_impl->AddTask(groupID, data);
}
void ThreadPool_SPSC::WaitForAllTasks()
{
	m_impl->WaitForAllTasks();
}
float ThreadPool_SPSC::GetAveragePopOverhead() const
{
	return m_impl->GetAveragePopOverhead();
}


} // rde

namespace
{

void WorkerThread::ProcessTasks()
{
	rde::Thread::SetName("WorkerThread_SPSC");
	rde::TaskData* task(0);

	rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::WORKER_THREAD_START);

	m_totalTaskTicks = 0;
	// Possible race, but not critical. This can only change to true.
	// If not now, we'll leave in next iteration/from failure loop.
	while (!m_threadPool->m_stopped)
	{
		if (task == 0)
		{
			if (m_threadPool->m_allTasksDone)
			{
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::WAIT_FOR_WORK_START);
				m_threadPool->WaitForWork();
				rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::WAIT_FOR_WORK_END);
			}
#if TRACE_CONTAINER_OVERHEAD
			rde::uint64 ticks = __rdtsc();
#endif
			task = PopTask();
#if TRACE_CONTAINER_OVERHEAD
			m_totalPopOverhead += __rdtsc() - ticks;
			++m_numPopCalls;
#endif
		}
		while (task)
		{
			rde::ThreadPool_SPSC::Impl::TaskGroup& taskGroup = m_threadPool->GetTaskGroup(task->m_groupID);
			rde::uint64 taskTicks = __rdtsc();
			rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_START);
			taskGroup.m_func(task);
			rde::ThreadProfiler::AddEvent(rde::ThreadPoolEvent::TASK_FUNC_END);
			m_totalTaskTicks += __rdtsc() - taskTicks;
			--m_threadPool->m_numPendingTasks;
			++m_tasksDone;

#if TRACE_CONTAINER_OVERHEAD
			taskTicks = __rdtsc();
#endif
			task = PopTask();
#if TRACE_CONTAINER_OVERHEAD
			m_totalPopOverhead += __rdtsc() - taskTicks;
			++m_numPopCalls;
#endif
		}

		if (!m_threadPool->m_allTasksDone)
		{
			// Out of tasks. Signalize we need more.
			m_needsWork = 1;
			for (int i = 0; /**/; ++i)
			{
				task = PopTask();
				if (task)
				{
					m_needsWork = 0;
					break;
				}
				if (m_threadPool->m_allTasksDone != 0)
					break;

				if ((i & 0xF) == 0)
					rde::Thread::MachinePause(80);
			}
		}
	}

#if TRACE_CONTAINER_OVERHEAD
	if (m_numPopCalls)
	{
//		double avgOverhead = double(m_totalPopOverhead) / m_numPopCalls;
//		double avgOverheadUs = avgOverhead / (rde::GetCPUTicksPerSecond() / 1e+6);
//		rde::Console::Profilef("Pop overhead: %f us (%f ticks) per operation (%d)\n", avgOverheadUs, 
//			avgOverhead, m_numPopCalls);
	}
#endif
	//rde::Console::Printf("Worker thread %d, tasks handled: %d, ticks: %I64d (%f ms)\n", GetCurrentThreadId(), 
	//	m_tasksDone, m_totalTaskTicks, double(m_totalTaskTicks) / GetTicksPerSecond() * 1000.0);
}


} // anonymous namespace


