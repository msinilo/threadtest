#include "thread/SpinMutex.h"
#include "thread/Backoff.h"
#include "core/Atomic.h"
#include "core/RdeAssert.h"
#include "core/Thread.h"

namespace rde
{
SpinMutex::SpinMutex()
:	m_locked(0)
{
}

SpinMutex::~SpinMutex()
{
	RDE_ASSERT(!m_locked && "Trying to destroy mutex that has not been released");
	if (m_locked)
		Release();
}

void SpinMutex::Acquire()
{
	if (!TryAcquire())
	{
		Backoff backoff;
		do
		{
			backoff.Wait();
		}
		while (!TryAcquire());
	}
}

bool SpinMutex::TryAcquire()
{
	MemoryBarrier();
	return Interlocked::CompareAndSwap(&m_locked, 0, 1) == 0;
}

void SpinMutex::Release()
{
	RDE_ASSERT(m_locked);
	Interlocked::FetchAndStore(&m_locked, 0);
	MemoryBarrier();
}

} // rde
