#ifndef CORE_THREAD_H
#define CORE_THREAD_H

#include "core/Config.h"
#include "core/MaxAlign.h"
#include <external/srutil/delegate.hpp>

namespace rde
{
class Thread
{
public:
	struct Impl;
	// Thread main pump function, which will be called when it starts.
	// No arguments, no return.
	typedef srutil::delegate0<void>	Delegate;

	enum Priority
	{
		PRIORITY_LOW,
		PRIORITY_NORMAL,
		PRIORITY_HIGH
	};

	Thread();
	~Thread();

	// @pre	!IsRunning()
	bool Start(const Delegate& delegate, unsigned int stackSize = 4096, Priority priority = PRIORITY_NORMAL);
	template<class T, void (T::*Func)()>
	bool Start(T* obj, unsigned int stackSize = 4096, Priority priority = PRIORITY_NORMAL)
	{
		return Start(Delegate::from_method<T, Func>(obj), stackSize, priority);
	}

	// @pre	IsRunning()
	void Stop();
	// Waits for the thread to terminate.
	// It's OK to call it on thread that's not running, it'll just
	// return immediately.
	void Wait();
	bool IsRunning() const;

	// Selects CPUs for this thread (bitmask).
	void SetAffinityMask(uint32 affinityMask);

	// To be called from thread function.
	static void SetName(const char* name);
	static const char* GetCurrentThreadName();
	static int GetCurrentThreadId();
	static void Sleep(long millis);
	// I'd actually prefer simply "Yield", but stupid winbase.h redefines it :/
	static void YieldCurrentThread();
	// Low-level "machine" pause.
	// Spins for a number of loops.
	static void MachinePause(long loops);

	// CPUs available for our process (bitmask).
	static uint32 GetProcessAffinityMask();

private:
	RDE_FORBID_COPY(Thread);
	union
	{
		MaxAlign	m_aligner;
		uint8		m_implMem[16];
	};
	Impl*	m_impl;
};

#if RDE_COMPILER_MSVC
RDE_FORCEINLINE void Thread::MachinePause(long loops)
{
	_asm
	{
		mov	eax, loops
	waitlabel:
		pause
		add	eax, -1
		jne	waitlabel
	}
}
#else
#	error "Thread::MachinePause not implemented for this platform"
#endif

}

#endif // CORE_THREAD_H
