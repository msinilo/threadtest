#include "core/ThreadProfiler.h"

#if RDE_THREAD_PROFILER_ENABLED

#pragma message("INFO: Thread profiler enabled")

#include "core/LockGuard.h"
#include "core/Mutex.h"
#include "core/Thread.h"
#include "core/Timer.h"
#include "core/win32/Windows.h"
#include "rdestl/sort.h"
#include "rdestl/vector.h"
#include <cstdio>

namespace
{
// RDTSC is quicker, but less reliable, not recommended.
#define USE_RDTSC	1

	rde::uint64 RDE_FORCEINLINE GetTicks()
	{
#if USE_RDTSC
		return __rdtsc();
#else
		return rde::Timer::Now();
#endif
	}
	double GetTicksPerSecond()
	{
		static double clockSpeed(0.0);
		if (clockSpeed > 0.0)
			return clockSpeed;
#if USE_RDTSC
		__int64 countsPerSecond(0);
		QueryPerformanceFrequency(reinterpret_cast< LARGE_INTEGER* >(&countsPerSecond));
		LARGE_INTEGER timeStart;
		QueryPerformanceCounter(&timeStart);

		LARGE_INTEGER timeEnd;
		timeEnd.QuadPart = 0;
		static const LONGLONG kWaitCounts = 500000;
		unsigned __int64 ticksStart = GetTicks();
		while (timeEnd.QuadPart < timeStart.QuadPart + kWaitCounts)
		{
			QueryPerformanceCounter(&timeEnd);
		}
		unsigned __int64 ticksTaken = GetTicks() - ticksStart;
		const double secondsPassed = double(countsPerSecond) / kWaitCounts;
		clockSpeed = double(ticksTaken) * secondsPassed;
		
		return clockSpeed;
#else
		LARGE_INTEGER freq;
		const bool ok = QueryPerformanceFrequency(&freq) != FALSE;
		RDE_ASSERT(ok);
		clockSpeed = double(freq.QuadPart);
		return clockSpeed;
#endif
	}
	#pragma pack(push, 1)
	struct ThreadProfileEvent
	{
		bool operator<(const ThreadProfileEvent& rhs) const
		{
			return ticks < rhs.ticks;
		}

		rde::uint32	type;
		const void*	userData;
		int			threadId;
		rde::uint64	ticks;
	};
	#pragma pack(pop)
	struct ThreadDesc
	{
		int		id;
		char	name[64];
	};

	template<typename T, int TMaxSize>
	struct GrowingArray
	{
		T& operator[](int i)
		{
			return m_items[i];
		}
		const T& operator[](int i) const
		{
			return m_items[i];
		}
		RDE_FORCEINLINE void PushBack(const T& t)
		{
			if (m_size >= m_capacity)
				Grow();
			m_items[m_size++] = t;
			// Wrap around. We record time moments, so it's safe.
			if (m_size >= TMaxSize)
			{
				m_maxSize = m_size;
				m_size = 0;
			}
		}
		// @note	Assumes data can be copied with memcpy!
		void Grow()
		{
			const int newCapacity = (m_capacity == 0 ? 4096 : m_capacity * 2);
			T* newItems = new T[newCapacity];
			rde::Sys::MemCpy(newItems, m_items, m_capacity * sizeof(T));
			delete[] m_items;
			m_items = newItems;
			m_capacity = newCapacity;
		}
		void Reset()
		{
			m_size = 0;
		}

		T*		m_items;
		int		m_size;
		int		m_maxSize;
		int		m_capacity;
	};

	typedef GrowingArray<ThreadProfileEvent, 512 * 1024 * 1024>	ThreadProfileEventArray;
	RDE_THREADLOCAL ThreadProfileEventArray	t_events = { 0, 0, 0, 0 };
	rde::uint64								s_baseTicks(0);
	bool									s_firstRun(true);
	rde::vector<ThreadProfileEvent>			s_allEvents;
	rde::vector<ThreadDesc>					s_allThreads;
	rde::Mutex								s_allThreadsMutex;
	rde::Mutex								s_allEventsMutex;
} // <anonymous> 

namespace rde
{
void ThreadProfiler::AddEvent(uint32 type, const void* userData /*= 0*/)
{
	if (s_firstRun) // possible race, but very unlikely
	{
		s_baseTicks = GetTicks();
		s_firstRun = false;
	}
	// Thread ID will be filled later (when submitting), calling this too often is a no-no.
	::ThreadProfileEvent e = { type, userData, -1, GetTicks() - s_baseTicks };
	t_events.PushBack(e);
}

void ThreadProfiler::SubmitEvents()
{
	const char* threadName = Thread::GetCurrentThreadName();
	const int threadId = Thread::GetCurrentThreadId();
	::ThreadDesc desc;
	strcpy_s(desc.name, threadName && *threadName ? threadName : "<no name>");
	desc.id = threadId;
	{
		LockGuard<Mutex> lock(s_allThreadsMutex);
		s_allThreads.push_back(desc);
	}

	const int numEvents = 
		t_events.m_maxSize > t_events.m_size ? t_events.m_maxSize : t_events.m_size;
	for (int i = 0; i < numEvents; ++i)
	{
		t_events[i].threadId = threadId;
		LockGuard<Mutex> lock(s_allEventsMutex);
		s_allEvents.push_back(t_events[i]);
	}
}

bool ThreadProfiler::SaveEvents(const char* fileName)
{
	FILE* f = fopen(fileName, "wb");
	if (!f)
		return false;

	const double ticksPerSecond = ::GetTicksPerSecond();
	::fwrite(&ticksPerSecond, sizeof(ticksPerSecond), 1, f);

	{
		LockGuard<Mutex> lock(s_allThreadsMutex);
		const int numThreads = s_allThreads.size();
		::fwrite(&numThreads, sizeof(numThreads), 1, f);
		for (int i = 0; i < numThreads; ++i)
		{
			const ::ThreadDesc& desc = s_allThreads[i];
			::fwrite(&desc.id, sizeof(desc.id), 1, f);
			::fwrite(&desc.name[0], sizeof(desc.name), 1, f);
		}
	}
	{
		LockGuard<Mutex> lock(s_allEventsMutex);
		rde::quick_sort(s_allEvents.begin(), s_allEvents.end());
		const int numEvents = s_allEvents.size();
		::fwrite(&numEvents, sizeof(numEvents), 1, f);
		::fwrite(s_allEvents.begin(), sizeof(ThreadProfileEvent), numEvents, f);
	}
	::fclose(f);
	return true;
} // SaveEvents

void ThreadProfiler::Reset()
{
	t_events.m_size = 0;
}

} // rde

#endif // RDE_THREAD_PROFILER_ENABLED
