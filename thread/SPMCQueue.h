#ifndef THREAD_SPMC_QUEUE_H
#define THREAD_SPMC_QUEUE_H

#include "thread/RelacyWrappers.h"

namespace rde
{
template<typename T, size_t TCapacity>
struct SPMCQueueStaticStorage
{
	T	m_data[TCapacity];
};
template<typename T, size_t TCapacity>
struct SPMCQueueDynamicStorage
{
	SPMCQueueDynamicStorage()
	:	m_data(new T[TCapacity])
	{
	}
	~SPMCQueueDynamicStorage()
	{
		delete[] m_data;
	}
	T*	m_data;
};
template<typename T, size_t TCapacity, bool TDynamic>
struct SPMCStorageSelector
{
	typedef SPMCQueueStaticStorage<T, TCapacity>	Storage;
};
template<typename T, size_t TCapacity>
struct SPMCStorageSelector<T, TCapacity, true>
{
	typedef SPMCQueueDynamicStorage<T, TCapacity>	Storage;
};

// Single producer/multiple consumer, bounded, lock-free, FIFO queue.
template<typename T, size_t TCapacity, bool TDynamicStorage = true>
class SPMCQueue
{
	// TCapacity must be power-of-two
	typedef char ERR_CapacityNotPowerOfTwo[((TCapacity & (TCapacity - 1)) == 0 ? 1 : -1)];

public:
	SPMCQueue()
	:	m_pushIndex(0),
		m_popIndex(0)
	{
	}
	
	uint32 Size() const
	{
		// Possible races should not be harmful.
		//	- if called from producer thread - only popIndex can change. Producer thread
		//	  checks if queue is full in 90% of cases. Increasing popIndex cannot make it
		//	  "more" full. It checks for emptiness mainly to see if it should add some items,
		//    false signal is not fatal.
		//	- if called from consumer thread - only pushIndex can change. Consumer thread
		//	  checks if queue is empty in 90% of cases. Increasing pushIndex cannot make it
		//	  empty (if non-empty before). 

		uint32 pushed = Load_Relaxed(m_pushIndex);
		uint32 popped = Load_Relaxed(m_popIndex);
		return pushed - popped;
	}
	bool IsFull() const
	{
		const uint32 s = Size();
		// It's OK on overflow -> it's not full in such case.
		// (more pushed than popped if it overflows).
		return s >= TCapacity;
	}
	bool IsEmpty() const
	{
		const uint32 s = Size();
		return s == 0;
	}

	// It's caller responsibility to make sure queue is not full at this moment.
	void Push(const T& t)
	{
		const Atomic32 pushIndex = Load_Acquire(m_pushIndex);
		const uint32 index = pushIndex & (TCapacity - 1);
		m_buffer.m_data[index] = t;
		Store_SeqCst(m_pushIndex, pushIndex + 1);
	}
	// Returns true if item popped successfully.
	RDE_FORCEINLINE bool Pop(T& t)
	{
		if (Load_Acquire(m_popIndex) >= Load_Relaxed(m_pushIndex))
			return false;
#if !RDE_DBG_RELACY_TEST
		Atomic32 index = Interlocked::Increment(&m_popIndex) - 1;
#else
		uint32 index = m_popIndex.fetch_add(1, rl::memory_order_release);
#endif
		if (index < Load_Relaxed(m_pushIndex))
		{
			t = m_buffer.m_data[index];
			return true;
		}
		return false;
	}

	void Reset()
	{
		Store_Relaxed(m_pushIndex, 0);
		m_popIndex = 0;
	}
	int NumPopped() const
	{
		return Load_Acquire(m_popIndex);
	}

private:
	SPMCQueue(const SPMCQueue&);
	SPMCQueue& operator=(const SPMCQueue&);

#if RDE_DBG_RELACY_TEST
	typedef std::atomic<uint32>	IndexType;
#else
	typedef Atomic32			IndexType;
#endif
	static const int kCacheLineSize = 64;
	typedef uint8	PadBuffer[kCacheLineSize - 4];

	typedef SPMCStorageSelector<T, TCapacity, TDynamicStorage>	StorageSelector;
	typedef typename StorageSelector::Storage					Storage;

	IndexType	m_pushIndex;
	PadBuffer	m_padding0;
	IndexType	m_popIndex;
	PadBuffer	m_padding1;
	Storage		m_buffer;
};

} // rde

#endif

