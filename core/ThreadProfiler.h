#ifndef CORE_THREAD_PROFILER_H
#define CORE_THREAD_PROFILER_H

#include "core/Config.h"

#ifndef RDE_THREAD_PROFILER_ENABLED
#	define RDE_THREAD_PROFILER_ENABLED	0
#endif

namespace rde
{
namespace ThreadProfiler
{
	namespace Event
	{
		enum Enum
		{
			THREAD_STARTED,
			THREAD_STOPPED,
			MUTEX_WAIT_START,
			MUTEX_WAIT_END,
			MUTEX_RELEASED,
			MUTEX_DESTROYED,
			SEMAPHORE_WAIT_START,
			SEMAPHORE_WAIT_END,
			SEMAPHORE_SIGNALLED,
			SEMAPHORE_DESTROYED,
			EVENT_WAIT_START,
			EVENT_WAIT_END,
			SLEEP,
			FRAME_START,
			FRAME_END,
			// User events
			USER_EVENT0,
			USER_EVENT_LAST	= USER_EVENT0 + 99,
		};
	};

#if RDE_THREAD_PROFILER_ENABLED
	void AddEvent(uint32, const void* userData = 0);
	void SubmitEvents();
	// Call from main thread only!
	bool SaveEvents(const char* fileName);
	void Reset();
#else
	RDE_FORCEINLINE void AddEvent(uint32 /*eventId*/, const void* = 0) {}
	RDE_FORCEINLINE void SubmitEvents() {}
	RDE_FORCEINLINE bool SaveEvents(const char*) { return true; }
	RDE_FORCEINLINE void Reset() {}
#endif
} // ThreadProfiler
} // rde

#endif 
