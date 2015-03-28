#define TEST_TBB	0

#include "KDTree.h"
#include "ReflectionHelpers.h"
#include "SimpleModel.h"
#include "ThreadPool_DEQueue.h"
#include "ThreadPool_Lock.h"
#include "ThreadPool_MPMCQueue.h"
#include "ThreadPool_BoundedMPMCQueue.h"
#include "ThreadPool_MPMCStack.h"
#include "ThreadPool_SPMC.h"
#include "ThreadPool_SPSC.h"
#include "reflection/TypeRegistry.h"
#include "rdestl/pair.h"
#include "rdestl/vector.h"
#include "core/Atomic.h"
#include "core/BitMath.h"
#include "core/Console.h"
#include "core/CPU.h"
#include "core/Random.h"
#include "core/Timer.h"
#include "core/Thread.h"
#include "core/ThreadProfiler.h"
#include <cstdio>
#include <cstdlib>	// atoi

#if TEST_TBB
#	include <external/tbb/blocked_range.h>
#	include <external/tbb/parallel_for.h>
#	include <external/tbb/task_scheduler_init.h>
#endif

namespace 
{
#define RAYCAST_TEST	1

#define TEST_SINGLE_THREADED	1
#define TEST_LOCK_BASED			1
#define TEST_SPSC				1
#define TEST_MPMC_STACK			1
#define TEST_MPMC_QUEUE			1
#define TEST_MPMC_BOUNDED_QUEUE 1
#define TEST_DEQUEUE			1
#define TEST_SPMC				1

void BuildKDTree(const SimpleModel& model, rde::KDTree& tree)
{
	const SimpleModel::MeshData& mesh = model.GetMeshData();
	const rde::uint32* indices = mesh.m_indices.Get();
	const rde::Vector* vertexData = (const rde::Vector*)mesh.m_vertexData.Get();
	for (long triIndex = 0; triIndex < mesh.m_numTris; ++triIndex)
	{
		const rde::uint32 iv0 = indices[triIndex * 3 + 0];
		const rde::uint32 iv1 = indices[triIndex * 3 + 1];
		const rde::uint32 iv2 = indices[triIndex * 3 + 2];

		tree.AddTriangle(vertexData[iv0], vertexData[iv1], vertexData[iv2]);
	}
	tree.Build();
}

void GenerateSpherePoints(int n, rde::Vector* points)
{
	static rde::KISSRandom rand((rde::uint32)(rde::GetCPUTicks() & 0xFFFFFFFF));
	for (int i = 0; i < n; ++i)
	{
		const float z = 2.f * rand.NextFloat() - 1.f;
		const float t = 2.f * 3.14159265f * rand.NextFloat();
		const float w = rde::M_Sqrt(1.f - z*z);
		const float x = w * rde::M_Cos(t);
		const float y = w * rde::M_Sin(t);
		points[i][0] = x;
		points[i][1] = y;
		points[i][2] = z;
	}
}

struct Ray
{
	rde::Vector	origin;
	rde::Vector direction;
};

#define COMPUTE_STAT_PROPERTIES	0

static void LongTaskFunc(long* out)
{
    long k = rand();
    int i;
    for (i = 0; i < 2000; i++ )
        k += (i*i*i) % 15;
    *out = k;
}

long TraceRaysSingleThreaded(int numRays, const Ray* rays, rde::KDTree& kdTree)
{
	long numHits(0);

#if COMPUTE_STAT_PROPERTIES
	rde::vector<rde::uint64> ticks(numRays);
#endif
	for (int i = 0; i < numRays; ++i)
	{
		float hitDist(0.f);
		const rde::uint64 t = rde::GetCPUTicks();
		const bool hit = kdTree.RayCast(rays[i].origin, rays[i].direction, 1000.f, hitDist);
#if COMPUTE_STAT_PROPERTIES
		ticks[i] = rde::GetCPUTicks() - t;
#else
		(void)sizeof(t);
#endif
		if (hit)
			++numHits;
	}

	// Calc mean & std deviation.
#if COMPUTE_STAT_PROPERTIES
	rde::uint64 totalTicks(0);
	rde::accumulate(ticks.begin(), ticks.end(), totalTicks);
	const double avgTicks = double(totalTicks) / ticks.size();
	double variance(0.0);
	for (int i = 0; i < numRays; ++i)
	{
		const double x = double(ticks[i]) - avgTicks;
		variance += (x * x);
	}
	variance /= ticks.size();
	const double stdDeviation = rde::M_Sqrt(variance);

	rde::Console::Printf("Avg ticks/ray: %f (%f us), std deviation: %f tick(s)\n", avgTicks, 
		avgTicks / (rde::GetCPUTicksPerSecond() / 1e+6), stdDeviation);
#endif
	return numHits;
}

struct TraceRayData : public rde::TaskData
{
	rde::KDTree*	m_tree;
	const Ray*		m_firstRay;
	int				m_numRays;
};
void TraceRay(TraceRayData* data)
{
	const Ray* rays = data->m_firstRay;
	for (int i = 0; i < data->m_numRays; ++i)
	{
		float hitDist(0);
		data->m_tree->RayCast(rays->origin, rays->direction, 1000.f, hitDist);
		++rays;
	}
}
void TraceRay_SPMC(TraceRayData* data, int index)
{
	const Ray* rays = data[index].m_firstRay;
	for (int i = 0; i < data[index].m_numRays; ++i)
	{
		float hitDist(0);
		data[index].m_tree->RayCast(rays->origin, rays->direction, 1000.f, hitDist);
		++rays;
	}
}

struct TimingInfo : public rde::pair<double, double>
{
	TimingInfo(double first_ = 0.0, double second_ = 0.0)
	:	rde::pair<double, double>(first_, second_),
		popOverhead(0.0),
		mainThreadTime(0.0)
	{}
	TimingInfo& operator+=(const TimingInfo& rhs)
	{
		first += rhs.first;
		second += rhs.second;
		popOverhead += rhs.popOverhead;
		mainThreadTime += rhs.mainThreadTime;
		return *this;
	}
	virtual void Print(const char* info, int iterations) const
	{
		const double perc = second * 100.0 / first;
		rde::Console::Printf("%s: %f ms (%f ms/iteration), %f%% spent on init\n", info,
			first, first / iterations, perc);
		const double avgOverhead = popOverhead / iterations;
		rde::Console::Printf("Average pop overhead: %f ticks\n", avgOverhead);
		rde::Console::Printf("Main thread work: %f ms/iteration\n", mainThreadTime / iterations);
	}

	double popOverhead;
	double mainThreadTime;
};

TimingInfo TraceRays_MT_Lock(int numRays, const Ray* rays, rde::KDTree& kdTree, int numWorkers,
						 bool mainThreadBusy)
{
	rde::ThreadPool_Lock threadPool(numWorkers);
	const rde::TaskGroupID taskGroup = threadPool.CreateTaskGroup("RayTracer", TraceRay);
	static const int kRaysPerTask = 20;
	const int numTasks = numRays / kRaysPerTask;
	rde::vector<TraceRayData> tasks(numTasks);

#if COMPUTE_STAT_PROPERTIES
	rde::vector<rde::uint64> ticks(numTasks);
	rde::uint64 maxTicks(0), minTicks(100000000000000);
#endif
	const rde::uint64 ticksStart = rde::GetCPUTicks();
	for (int i = 0; i < numTasks; ++i)
	{
		TraceRayData* task = &tasks[i];
		task->m_tree = &kdTree;
		task->m_firstRay = rays;
		task->m_numRays = kRaysPerTask;

#if COMPUTE_STAT_PROPERTIES
		(void)taskGroup;
		rde::uint64 t = rde::GetCPUTicks();
		TraceRay(task);
		t = rde::GetCPUTicks() - t;
		ticks[i] = t;

		if (t > maxTicks)
			maxTicks = t;
		if (t < minTicks)
			minTicks = t;
#else
		threadPool.AddTask(taskGroup, task);
#endif
		rays += kRaysPerTask;
	}
	const rde::uint64 ticksSetup = rde::GetCPUTicks() - ticksStart;

	if (mainThreadBusy)
	{
		long i(0);
		LongTaskFunc(&i);
	}

	threadPool.WaitForAllTasks();

#if COMPUTE_STAT_PROPERTIES
	// Calc mean & std deviation.
	rde::uint64 totalTicks(0);
	rde::accumulate(ticks.begin(), ticks.end(), totalTicks);
	const double avgTicks = double(totalTicks) / ticks.size();
	double variance(0.0);
	for (int i = 0; i < ticks.size(); ++i)
	{
		const double x = double(ticks[i]) - avgTicks;
		variance += (x * x);
	}
	variance /= ticks.size();
	const double stdDeviation = rde::M_Sqrt(variance);
	rde::Console::Printf("Max ticks: %f, min ticks: %f\n", double(maxTicks), double(minTicks));
	rde::Console::Printf("Avg ticks/ray: %f (%f us), std deviation: %f tick(s)\n", avgTicks, 
		avgTicks / (rde::GetCPUTicksPerSecond() / 1e+6), stdDeviation);
#endif
	const rde::uint64 ticksTotal = rde::GetCPUTicks() - ticksStart;
	const double ticksPerMs = rde::GetCPUTicksPerSecond() / 1000.0;
	TimingInfo timingInfo(double(ticksTotal) / ticksPerMs, double(ticksSetup) / ticksPerMs);
	timingInfo.popOverhead = threadPool.GetAveragePopOverhead();
	return timingInfo;
}

struct DEQueueTimingInfo : public TimingInfo
{
	DEQueueTimingInfo(double first_ = 0.0, double second_ = 0.0)
	:	TimingInfo(first_, second_),
		stealOverhead(0.0)
	{
	}
	DEQueueTimingInfo& operator+=(const DEQueueTimingInfo& rhs)
	{
		TimingInfo::operator+=(rhs);
		stealOverhead += rhs.stealOverhead;
		return *this;
	}
	virtual void Print(const char* info, int iterations) const
	{
		TimingInfo::Print(info, iterations);
		rde::Console::Printf("Average steal overhead: %f ticks\n", stealOverhead/iterations);
	}
	double stealOverhead;
};

template<class TThreadPoolClass>
TimingInfo TraceRays_MT_Generic(int numRays, const Ray* rays, rde::KDTree& kdTree, int numWorkers,
								bool mainThreadBusy)
{
	TThreadPoolClass threadPool(numWorkers);
	const rde::TaskGroupID taskGroup = threadPool.CreateTaskGroup("RayTracer", TraceRay);

	static const int kRaysPerTask = 20;
	const int numTasks = numRays / kRaysPerTask;
	rde::vector<TraceRayData> tasks(numTasks);
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 2);
	const rde::uint64 ticksStart = rde::GetCPUTicks();
	for (int i = 0; i < numTasks; ++i)
	{
		TraceRayData* task = &tasks[i];
		task->m_tree = &kdTree;
		task->m_firstRay = rays;
		task->m_numRays = kRaysPerTask;
		threadPool.AddTask(taskGroup, task);

		rays += kRaysPerTask;
	}
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 3);
	const rde::uint64 ticksSetup = rde::GetCPUTicks() - ticksStart;

	rde::uint64 ticksMainThread = rde::GetCPUTicks();
	if (mainThreadBusy)
	{
		long i(0);
		LongTaskFunc(&i);
	}
	ticksMainThread = rde::GetCPUTicks() - ticksMainThread;

	threadPool.WaitForAllTasks();
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 4);
	const rde::uint64 ticksTotal = rde::GetCPUTicks() - ticksStart;
	const double ticksPerMs = rde::GetCPUTicksPerSecond() / 1000.0;
	TimingInfo timingInfo(double(ticksTotal) / ticksPerMs, double(ticksSetup) / ticksPerMs);
	timingInfo.popOverhead = threadPool.GetAveragePopOverhead();
	timingInfo.mainThreadTime = double(ticksMainThread) / ticksPerMs;
	return timingInfo;
}

TimingInfo TraceRays_MT_SPMC(int numRays, const Ray* rays, rde::KDTree& kdTree, int numWorkers,
							 bool mainThreadBusy)
{
	rde::ThreadPool_SPMC threadPool(numWorkers);
	const rde::TaskGroupID taskGroup = threadPool.CreateTaskGroup("RayTracer", TraceRay_SPMC);

	static const int kRaysPerTask = 20;
	const int numTasks = numRays / kRaysPerTask;
	rde::vector<TraceRayData> tasks(numTasks);
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 2);
	const rde::uint64 ticksStart = rde::GetCPUTicks();
	for (int i = 0; i < numTasks; ++i)
	{
		TraceRayData* task = &tasks[i];
		task->m_tree = &kdTree;
		task->m_firstRay = rays;
		task->m_numRays = kRaysPerTask;

		rays += kRaysPerTask;
	}
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 3);
	const rde::uint64 ticksSetup = rde::GetCPUTicks() - ticksStart;

	rde::uint64 ticksMainThread = rde::GetCPUTicks();
	if (mainThreadBusy)
	{
		long i(0);
		LongTaskFunc(&i);
	}
	ticksMainThread = rde::GetCPUTicks() - ticksMainThread;

	threadPool.WaitForAllTasks(taskGroup, &tasks[0], numTasks);
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 4);
	const rde::uint64 ticksTotal = rde::GetCPUTicks() - ticksStart;
	const double ticksPerMs = rde::GetCPUTicksPerSecond() / 1000.0;
	TimingInfo timingInfo(double(ticksTotal) / ticksPerMs, double(ticksSetup) / ticksPerMs);
	timingInfo.popOverhead = threadPool.GetAveragePopOverhead();
	timingInfo.mainThreadTime = double(ticksMainThread) / ticksPerMs;
	return timingInfo;
}

typedef rde::pair<int, int> Range;
template<typename TBody>
class ForTaskData : public rde::TaskData_DEQueue
{
public:
	explicit ForTaskData(const Range& range, const TBody& body, rde::ThreadPool_DEQueue* threadPool,
		int granularity = 1)
	:	m_range(range),
		m_granularity(granularity),
		m_body(body),
		m_threadPool(threadPool)
	{
	}
	RDE_FORCEINLINE void Execute()
	{
		m_body(m_range);
	}
	static void Start(rde::ThreadPool_DEQueue& threadPool,
		rde::TaskGroupID groupID, const Range& range, const TBody& body, int granularity)
	{
		rde::TaskData_DEQueue* forTask = 
			new (threadPool.AllocateTask()) ForTaskData<TBody>(range, body, &threadPool, granularity);
		threadPool.AddTask(groupID, forTask);
	}

	Range						m_range;
	int							m_granularity;
	TBody						m_body;
	rde::ThreadPool_DEQueue*	m_threadPool;
};

template<typename TBody>
rde::TaskData_DEQueue* ExecuteForTask(ForTaskData<TBody>* data)
{
	const Range& range = data->m_range;
	if (range.second - range.first <= data->m_granularity)
	{
		data->Execute();
		return 0;
	}
	const int mid = (range.first + range.second) / 2;
	Range newRange = range;
	newRange.first = mid;

	rde::TaskData_DEQueue* newTask = 
		new (data->m_threadPool->AllocateTask()) ForTaskData<TBody>(newRange, data->m_body, 
			data->m_threadPool, data->m_granularity);
	data->m_threadPool->AddTask(data->m_groupID, newTask);

	data->m_range.second = mid;
	newTask = 
		new (data->m_threadPool->AllocateTask()) ForTaskData<TBody>(data->m_range, data->m_body, 
			data->m_threadPool, data->m_granularity);
	// "this" task processes first->mid, new spawned task: mid->end
	return data;
}

struct DEQueue_RayTracer
{
	DEQueue_RayTracer(const Ray* rays, rde::KDTree& kdTree): m_rays(rays), m_tree(&kdTree) {}

	void operator()(const Range& range)
	{
		for (int i = range.first; i < range.second; ++i)
		{
			float hitDist(0);
			m_tree->RayCast(m_rays[i].origin, m_rays[i].direction, 1000.f, hitDist);
		}
	}
	const Ray*		m_rays;
	rde::KDTree*	m_tree;
};
DEQueueTimingInfo TraceRays_MT_DEQueue(int numRays, const Ray* rays, rde::KDTree& kdTree, int numWorkers,
									   bool mainThreadBusy)
{
	rde::ThreadPool_DEQueue threadPool(numWorkers);
	const rde::TaskGroupID taskGroup = threadPool.CreateTaskGroup("RayTracer", ExecuteForTask<DEQueue_RayTracer>);

	DEQueue_RayTracer tracer(rays, kdTree);
	Range range(0, numRays);
	const rde::uint64 ticksStart = rde::GetCPUTicks();
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 2);
	ForTaskData<DEQueue_RayTracer>::Start(threadPool, taskGroup, range, tracer, 20);

	if (mainThreadBusy)
	{
		long i(0);
		LongTaskFunc(&i);
	}

	threadPool.WaitForGroup(taskGroup);
	rde::ThreadProfiler::AddEvent(rde::ThreadProfiler::Event::USER_EVENT0 + 3);
	const rde::uint64 ticksTotal = rde::GetCPUTicks() - ticksStart;
	const double ticksPerMs = rde::GetCPUTicksPerSecond() / 1000.0;

	DEQueueTimingInfo timingInfo(double(ticksTotal) / ticksPerMs, 0.0);
	rde::pair<float, float> overheads = threadPool.GetAveragePopStealOverhead();
	timingInfo.popOverhead = overheads.first;
	timingInfo.stealOverhead = overheads.second;
	return timingInfo;
}

#if TEST_TBB
struct TBB_RayTracer
{
	TBB_RayTracer(const Ray* rays, rde::KDTree& kdTree): m_rays(rays), m_tree(&kdTree) {}

	void operator()(const tbb::blocked_range<size_t>& range) const
	{
		for (size_t i = range.begin(); i < range.end(); ++i)
		{
			float hitDist(0);
			m_tree->RayCast(m_rays[i].origin, m_rays[i].direction, 1000.f, hitDist);
		}
	}
	const Ray*		m_rays;
	rde::KDTree*	m_tree;
};

double TraceRays_TBB(int numRays, const Ray* rays, rde::KDTree& kdTree, int numWorkers)
{
	tbb::task_scheduler_init init(numWorkers + 1);

	TBB_RayTracer tracer(rays, kdTree);
	tbb::blocked_range<size_t> range(0, numRays, 20);
	const rde::uint64 ticksStart = rde::GetCPUTicks();
	tbb::parallel_for(range, tracer);
	const rde::uint64 ticksTotal = rde::GetCPUTicks() - ticksStart;
	return double(ticksTotal) / (rde::GetCPUTicksPerSecond() / 1000.0);
}
#endif

void RayTest(rde::KDTree& kdTree, int numWorkers, bool mainThreadBusy)
{
	const int kNumRays = 10000;
	const float kRadius = 0.5f;
	using rde::Vector;
	rde::ScopedPtr<Ray, rde::ArrayDeleter> srays(new Ray[kNumRays]);
	Ray* rays = srays.Get();
	const Vector center(-0.016840f, 0.110154f, -0.001537f);
	for (int i = 0; i < kNumRays; ++i)
	{
		Vector pt[2];
		GenerateSpherePoints(2, pt);
		pt[0] *= kRadius; 
		pt[1] *= kRadius;
		pt[0] += center;
		pt[1] += center;
		Vector dir(pt[1] - pt[0]);
		dir.Normalize();
		rays[i].origin = pt[0];
		rays[i].direction = dir;
	}

	const int ITERS = 5000;
#if TEST_SINGLE_THREADED
	rde::uint64 singleThreadedTicks(0);
	for (int i = 0; i < ITERS; ++i)
	{
		const rde::uint64 t = rde::GetCPUTicks();
		TraceRaysSingleThreaded(kNumRays, rays, kdTree);
		singleThreadedTicks += rde::GetCPUTicks() - t;
	}
	const double singleThreadedMs = double(singleThreadedTicks) / (rde::GetCPUTicksPerSecond() / 1000.0);
	rde::Console::Printf("Single threaded: %f ms (%f ms avg)\n", singleThreadedMs,
		double(singleThreadedMs) / ITERS);
#endif

#if TEST_LOCK_BASED
	TimingInfo mtLockedMs(0.0);
	for (int i = 0; i < ITERS; ++i)
		mtLockedMs += TraceRays_MT_Lock(kNumRays, rays, kdTree, numWorkers, mainThreadBusy);
	mtLockedMs.Print("Multi threaded, locks", ITERS);
#endif

#if TEST_SPSC
	TimingInfo mtSPSCMs;
	for (int i = 0; i < ITERS; ++i)
		mtSPSCMs += TraceRays_MT_Generic<rde::ThreadPool_SPSC>(kNumRays, rays, kdTree, numWorkers, mainThreadBusy);
	rde::ThreadProfiler::SubmitEvents();
	rde::ThreadProfiler::SaveEvents("spsc.prf");
	mtSPSCMs.Print("Multi threaded, SPSC queue", ITERS);
#endif

#if TEST_MPMC_STACK
	TimingInfo mtMPMC_StackMs;
	for (int i = 0; i < ITERS; ++i)
		mtMPMC_StackMs += TraceRays_MT_Generic<rde::ThreadPool_MPMC_Stack>(kNumRays, rays, kdTree, numWorkers, mainThreadBusy);
	rde::ThreadProfiler::SubmitEvents();
	rde::ThreadProfiler::SaveEvents("mpmcstack.prf");
	mtMPMC_StackMs.Print("Multi threaded, MPMC stack", ITERS);
#endif

#if TEST_MPMC_QUEUE
	TimingInfo mtMPMC_QueueMs;
	for (int i = 0; i < ITERS; ++i)
		mtMPMC_QueueMs += TraceRays_MT_Generic<rde::ThreadPool_MPMC_Queue>(kNumRays, rays, kdTree, numWorkers, mainThreadBusy);
	rde::ThreadProfiler::SubmitEvents();
	rde::ThreadProfiler::SaveEvents("mpmcqueue.prf");
	mtMPMC_QueueMs.Print("Multi threaded, MPMC queue", ITERS);
#endif

#if TEST_MPMC_BOUNDED_QUEUE
	TimingInfo mtBoundedMPMC_QueueMs;
	for (int i = 0; i < ITERS; ++i)
		mtBoundedMPMC_QueueMs += TraceRays_MT_Generic<rde::ThreadPool_BoundedMPMC_Queue>(kNumRays, rays, kdTree, numWorkers, mainThreadBusy);
	rde::ThreadProfiler::SubmitEvents();
	rde::ThreadProfiler::SaveEvents("boundedmpmcqueue.prf");
	mtBoundedMPMC_QueueMs.Print("Multi threaded, Bounded MPMC queue", ITERS);
#endif

#if TEST_DEQUEUE
	(void)mainThreadBusy;
	DEQueueTimingInfo mtDEQueueMs;
	for (int i = 0; i < ITERS; ++i)
		mtDEQueueMs += TraceRays_MT_DEQueue(kNumRays, rays, kdTree, numWorkers, mainThreadBusy);
	rde::ThreadProfiler::SubmitEvents();
	rde::ThreadProfiler::SaveEvents("dequeue.prf");
	mtDEQueueMs.Print("DEQueue", ITERS);
#endif

#if TEST_SPMC
	TimingInfo mtSPMCMs;
	for (int i = 0; i < ITERS; ++i)
		mtSPMCMs += TraceRays_MT_SPMC(kNumRays, rays, kdTree, numWorkers, mainThreadBusy);
		
		//TraceRays_MT_Generic<rde::ThreadPool_SPMC>(kNumRays, rays, kdTree, numWorkers);
	rde::ThreadProfiler::SubmitEvents();
	rde::ThreadProfiler::SaveEvents("spmc.prf");
	mtSPMCMs.Print("Multi threaded, SPMC queue", ITERS);
#endif

#if TEST_TBB
	double tbbMs(0.0);
	for (int i = 0; i < ITERS; ++i)
		tbbMs += TraceRays_TBB(kNumRays, rays, kdTree, numWorkers);
	rde::Console::Printf("TBB %f ms (%f avg)\n", tbbMs, double(tbbMs) / ITERS);
#endif
}

} // <anonymous> namespace

void SPSCQueue_Test_Run();
void SPMCQueue_Test_Run();
void LockFreeQueue_Test_Run();
void LockFreeStack_Test_Run();
void LockFreeDEQueue_Test_Run();
void BoundedMPMCQueue_Test_Run();
void ProfileThreadingPrimitives(int numThreads);
int __cdecl main(int argc, char const* argv[])
{
	const rde::uint32 processAffinityMask = rde::Thread::GetProcessAffinityMask();
	rde::Console::Printf("Process affinity mask: %d, core(s): %d\n", processAffinityMask,
		rde::NumBits(processAffinityMask));

	int numWorkers = rde::NumBits(processAffinityMask) - 1;
	bool mainThreadBusy = false;
	if (argc >= 2)
	{
		numWorkers = atoi(argv[1]);
		if (argc >= 3)
		{
			mainThreadBusy = true;
		}
	}
	rde::Console::Printf("Test with %d worker(s), %s\n", numWorkers, 
		(mainThreadBusy ? "main thread busy" : "main thread helping"));

	SPSCQueue_Test_Run();
	SPMCQueue_Test_Run();
	LockFreeQueue_Test_Run();
	LockFreeStack_Test_Run();
	LockFreeDEQueue_Test_Run();
	BoundedMPMCQueue_Test_Run();

	rde::TypeRegistry typeRegistry;
	if (!LoadReflectionInfo("threadtest.ref", typeRegistry))
	{
		rde::Console::Errorf("Couldn't load reflection info.\n");
		return 1;
	}

	rde::KDTree kdTree;
	if (!kdTree.Load("bunny.kdtree", typeRegistry))
	{
		SimpleModel model;
		if (!model.LoadPLY("bun_zipper.ply"))
		{
			rde::Console::Errorf("Couldn't load model.\n");
			return 1;
		}

		BuildKDTree(model, kdTree);
		if (!kdTree.Save("bunny.kdtree", typeRegistry))
			rde::Console::Errorf("Couldn't save KD tree file!\n");
	}
	{
		// Test KD tree
#if 0
		rde::Vector c(-0.016840f, 0.110154f, -0.001537f);
		rde::Vector pt(2.f, 2.f, 2.f);
		pt += c;
		rde::Vector dir;
		dir.Set(-1.f, -1.f, -1.f);
		dir.Normalize();
		float tt;
		const bool hit = kdTree.RayCast(pt, dir, 1000.f, tt);
		rde::Console::Printf("T: %f\n", tt);
		(void)hit;

		const int N = 100 * 1000;
		const float kRadius = 0.2f;
		rde::Vector* rays = new rde::Vector[N * 2];
		for (int i = 0; i < N; ++i)
		{
			rde::Vector pt[2];
			SpherePoints(2, pt);
			pt[0] *= kRadius; 
			pt[1] *= kRadius;
			pt[0] += c;
			pt[1] += c;
			dir = (pt[1] - pt[0]);
			dir.Normalize();
			rays[i * 2 + 0] = pt[0];
			rays[i * 2 + 1] = dir;
		}

		long numHits(0);
		for (int i = 0; i < N; ++i)
		{
			float hitDist(0.f);
			const bool hit = kdTree.RayCast(rays[i * 2], rays[i * 2 + 1], 1000.f, hitDist);
			if (hit)
				++numHits;
		}
		rde::Console::Printf("Fraction hit: %f\n", double(numHits) / N);			
#endif
	}

#if RAYCAST_TEST
	RayTest(kdTree, numWorkers, mainThreadBusy);
#endif
#if MANDELBROT_TEST
	MandelbrotTest(numWorkers);
#endif
	return 0;
}
