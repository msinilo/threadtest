#ifndef THREAD_LOCKFREE_DE_QUEUE_H
#define THREAD_LOCKFREE_DE_QUEUE_H

#include "thread/RelacyWrappers.h"
#include "thread/StampedVar.h"

namespace rde
{

// Double-ended queue, bounded.
// Single producer, multiple consumers.
// Holds pointers to T, not Ts themselves.
template<typename T>
class LockFreeDEQueue
{
public:
	// Creates queue able to hold up to 
	// "maxCapacity" items at given moment.
	explicit LockFreeDEQueue(int maxCapacity)
    {
		m_data = new T*[maxCapacity];
#if !RDE_DBG_RELACY_TEST
		RDE_COMPILE_CHECK(sizeof(m_top) == 8);
		Sys::MemSet(m_data, 0, sizeof(T*) * maxCapacity);
#endif

		StampedInt top(0, 0);
		Store_Relaxed(m_top, top);
        Store_Relaxed(m_bottom, int32(0));
#if RDE_DEBUG
		m_producerThreadId = -1;
#endif
	}
    ~LockFreeDEQueue()
    {
		delete[] m_data;
	}
    void PushBottom(T* t)
    {
#if RDE_DEBUG && !RDE_DBG_RELACY_TEST
		if (m_producerThreadId == -1)
			m_producerThreadId = Thread::GetCurrentThreadId();
		RDE_ASSERT(m_producerThreadId == Thread::GetCurrentThreadId());
#endif
		RDE_ASSERT(t);
		const int32 bottom = Load_Acquire(m_bottom);
		m_data[bottom] = t;
		Store_Release(m_bottom, bottom + 1);
	}
    // Can be called from multiple threads.
    T* PopTop()
    {
#if RDE_DEBUG && !RDE_DBG_RELACY_TEST
		RDE_ASSERT(m_producerThreadId == -1 || 
			m_producerThreadId != Thread::GetCurrentThreadId());
#endif

		StampedInt top = LoadStampedVar64(m_top);
		const int32 topIndex = top.m_v32.var;
		StampedInt newTop(topIndex + 1, top.m_v32.count + 1);

		MemoryBarrier();
		const int32 bottom = Load_Acquire(m_bottom);
        if (bottom <= topIndex)
			return 0;
		T* t = m_data[topIndex];
		if (CAS64(&m_top, top, newTop))
		{
			RDE_ASSERT(t);
			return t;
		}
		// CAS failed.
        return 0;
	}
    // Only called from one thread.
    T* PopBottom()
    {
		int32 bottom = Load_Acquire(m_bottom);
		if (bottom == 0)      // Empty queue
			return 0;

#if RDE_DEBUG && !RDE_DBG_RELACY_TEST
		RDE_ASSERT(m_producerThreadId == -1 || 
			m_producerThreadId == Thread::GetCurrentThreadId());
#endif

		--bottom;
		Store_Release(m_bottom, bottom);
		MemoryBarrier();
        T* t = m_data[bottom];
		StampedInt top = LoadStampedVar64(m_top);
		StampedInt newTop(0, top.m_v32.count + 1);

        // No danger of conflict, just return.
		const int32 topIndex = top.m_v32.var;
        if (bottom > topIndex)
		{
			RDE_ASSERT(t);
			return t;
		}

		Store_Release(m_bottom, int32(0));
        // Possible conflict, slow-path.
        if (bottom == topIndex)
        {
			if (CAS64(&m_top, top, newTop))
			{
				RDE_ASSERT(t);
				return t;
			}
        }
        FetchAndStore64(&m_top, newTop);
        return 0;
    }
	// Called by thieves to determine if there is anything to steal.
	bool IsEmpty() const
	{
		const StampedInt top = Load_Acquire(m_top);
		const int32 bottom = Load_Acquire(m_bottom);
		return bottom <= top.m_v32.var;
	}

private:
	RDE_FORBID_COPY(LockFreeDEQueue);

#if RDE_DBG_RELACY_TEST
	typedef std::atomic<int32>	IndexType;
#else
	typedef int32				IndexType;
#endif
	typedef StampedVar<int32>	StampedInt;

	T**							m_data;
    IndexType					m_bottom; 
	RDE_ATOMIC_VAR(StampedInt)	m_top;
#if RDE_DEBUG
	int							m_producerThreadId;
#endif
};

} // rde

#endif 
