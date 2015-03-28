#include "ThreadPool_SPMC.h"
#include "thread/Gate.h"
#include "thread/SPMCQueue.h"
#include "rdestl/fixed_vector.h"
#include "core/Atomic.h"
#include "core/BitMath.h"
#include "core/Console.h"
#include "core/CPU.h"
#include "core/LockGuard.h"
#include "core/Mutex.h"
#include "core/Thread.h"
#include "core/ThreadEvent.h"
#include "core/ThreadProfiler.h"

namespace
{
const rde::Gate::State kStateEmpty(0);
const rde::Gate::State kStateFull(-1);
const rde::Gate::State kStateOpen(-2);

class WorkerThread : public rde::Thread
{
public:
	explicit WorkerThread(rde::ThreadPool_SPMC::Impl* tp)
	:	m_threadPool(tp),
		m_totalPopOverhead(0),
		m_numPopCalls(0)
	{}
	void ProcessTasks();

	rde::ThreadPool_SPMC::Impl*	m_threadPool;

	rde::uint64					m_totalPopOverhead;
	int							m_numPopCalls;
};

} // namespace

namespace rde
{
struct ThreadPool_SPMC::Impl
{
	static const int32	kMaxTaskGroups	= 64;

	struct TaskGroup
	{
		TaskGroup()
		:	m_name(0),
			m_func(0)
		{
		}
		void Init(const char* name, ThreadPool_SPMC::TaskFunc func)
		{
			m_name = name;
			m_func = func;
		}
		const char*					m_name;
		ThreadPool_SPMC::TaskFunc	m_func;
	};

	Impl()
	:	m_numTaskGroups(0),
		m_taskQueueGate(rde::ThreadEvent::MANUAL_RESET),
		m_stopped(0)
	{
		m_numPendingTasks = 0;
		m_numTasksToPop = 0;
		m_hasWork = 0;
	}
	~Impl()
	{
		DeleteWorkerThreads();
	}

	// !Shouldn't change during run-time.
	void SetNumThreads(int numThreads)
	{
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
		m_hasWork = 1;

		SignalWork();
		for (WorkerThreads::iterator it = m_workerThreads.begin();
			it != m_workerThreads.end(); ++it)
		{
			delete *it;
		}
		m_taskQueueGate.Reset();
		m_workerThreads.clear();
	}

	TaskGroupID CreateTaskGroup(const char* name, TaskFunc func)
	{
		const TaskGroupID id = m_numTaskGroups++;
		RDE_ASSERT(id < kMaxTaskGroups);
		m_taskGroups[id].Init(name, func);
		return id;
	}

	void WaitForAllTasks(TaskGroupID group, TaskData* data, int numTasks)
	{
		m_taskGroup = &m_taskGroups[group];
		m_data = data;
		m_numTasksTotal = numTasks;
		m_numPendingTasks = numTasks;
		m_numTasksToPop = numTasks;
		SignalWork();
		while (m_numPendingTasks > 0)
			ProcessTasks(group, data);
		m_numPendingTasks = 0;
		m_taskQueueGate.Reset();
	}

	void ProcessTasks(TaskGroupID group, TaskData* data)
	{
		do
		{
			const Atomic32 n = --m_numTasksToPop;
			if (n >= 0)
			{
				ThreadProfiler::AddEvent(ThreadProfiler::Event::USER_EVENT0);
				m_taskGroups[group].m_func(data, m_numTasksTotal - n - 1);
				ThreadProfiler::AddEvent(ThreadProfiler::Event::USER_EVENT0 + 1);
				--m_numPendingTasks;
			}
		}
		while (m_numPendingTasks > 0);
	}

	void SignalWork()
	{
		m_taskQueueGate.Signal();
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
	void WaitForWork()
	{
		m_taskQueueGate.WaitInfinite();
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

	typedef rde::fixed_vector<WorkerThread*, 8, false>	WorkerThreads;

	int32				m_numTaskGroups;
	//Gate				m_taskQueueGate;
	ThreadEvent			m_taskQueueGate;
	volatile uint8		m_stopped; 

	Atomic<Atomic32>	m_numPendingTasks;
	Atomic<Atomic32>	m_numTasksToPop;
	int					m_numTasksTotal;
	TaskGroup			m_taskGroups[kMaxTaskGroups];
	WorkerThreads		m_workerThreads;
	Mutex				m_workerThreadListMutex;

	volatile uint8		m_hasWork;

	TaskData*			m_data;
	TaskGroup*			m_taskGroup;
}; 

ThreadPool_SPMC::ThreadPool_SPMC(int numThreads)
:	m_impl(new Impl)
{
	m_impl->SetNumThreads(numThreads);
}
ThreadPool_SPMC::~ThreadPool_SPMC()
{
}

TaskGroupID ThreadPool_SPMC::CreateTaskGroup(const char* name, TaskFunc func)
{
	RDE_ASSERT(name);
	RDE_ASSERT(func);
	return m_impl->CreateTaskGroup(name, func);
}
void ThreadPool_SPMC::WaitForAllTasks(TaskGroupID group, TaskData* data, int numTasks)
{
	m_impl->WaitForAllTasks(group, data, numTasks);
}
float ThreadPool_SPMC::GetAveragePopOverhead() const
{
	return m_impl->GetAveragePopOverhead();
}

} // rde


namespace
{
void WorkerThread::ProcessTasks()
{
	rde::Thread::SetName("WorkerThread_SPMC");

	// Possible race, but not critical. This can only change to true.
	// If not now, we'll leave in next iteration/from failure loop.
	while (!m_threadPool->m_stopped)
	{
		rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 7);
		m_threadPool->WaitForWork();
		rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 8);

		if (m_threadPool->m_stopped)
			break;

		do
		{
#if TRACE_CONTAINER_OVERHEAD
			const rde::uint64 t = __rdtsc();
#endif
			const rde::Atomic32 n = --m_threadPool->m_numTasksToPop;
#if TRACE_CONTAINER_OVERHEAD
			const rde::uint64 dt = __rdtsc() - t;
#endif
			if (n >= 0)
			{
#if TRACE_CONTAINER_OVERHEAD
				m_totalPopOverhead += dt;
				++m_numPopCalls;
#endif
				rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0);
				m_threadPool->m_taskGroup->m_func(m_threadPool->m_data, m_threadPool->m_numTasksTotal - n - 1);
				rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 1);
				--m_threadPool->m_numPendingTasks;
			}
		}
		while (m_threadPool->m_numPendingTasks > 0);
	} // while (!m_threadPool->m_stopped)
}

} // anonymous namespace

