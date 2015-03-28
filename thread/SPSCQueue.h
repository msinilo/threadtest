#ifndef THREAD_SPSC_QUEUE_H
#define THREAD_SPSC_QUEUE_H

#include "thread/RelacyWrappers.h"

namespace rde
{
template<typename T, size_t TCapacity>
struct SPSCQueueStaticStorage
{
	T	m_data[TCapacity];
};
template<typename T, size_t TCapacity>
struct SPSCQueueDynamicStorage
{
	SPSCQueueDynamicStorage()
	:	m_data(new T[TCapacity])
	{
	}
	~SPSCQueueDynamicStorage()
	{
		delete[] m_data;
	}
	T*	m_data;
};
template<typename T, size_t TCapacity, bool TDynamic>
struct SPSCStorageSelector
{
	typedef SPSCQueueStaticStorage<T, TCapacity>	Storage;
};
template<typename T, size_t TCapacity>
struct SPSCStorageSelector<T, TCapacity, true>
{
	typedef SPSCQueueDynamicStorage<T, TCapacity>	Storage;
};

// Single producer/single consumer, bounded FIFO queue.
template<typename T, size_t TCapacity, bool TDynamicStorage = true>
class SPSCQueue
{
	// TCapacity must be power-of-two
	typedef char ERR_CapacityNotPowerOfTwo[((TCapacity & (TCapacity - 1)) == 0 ? 1 : -1)];

public:
	SPSCQueue()
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
		RDE_ASSERT(!IsFull());
		const uint32 pushIndex = Load_Acquire(m_pushIndex);
		const uint32 index = pushIndex & (TCapacity - 1);
		m_buffer.m_data[index] = t;
		Store_Release(m_pushIndex, pushIndex + 1);
	}
	// Returns true if item popped successfully.
	bool Pop(T& t)
	{
		if (!IsEmpty())
		{
			const uint32 popIndex = Load_Acquire(m_popIndex);
			const uint32 index = popIndex & (TCapacity - 1);
			t = m_buffer.m_data[index];
			Store_Release(m_popIndex, popIndex + 1);
			return true;
		}
		return false;
	}


private:
	SPSCQueue(const SPSCQueue&);
	SPSCQueue& operator=(const SPSCQueue&);

	static const int kCacheLineSize = 32;
	typedef uint8	PadBuffer[kCacheLineSize - 4];
#if RDE_DBG_RELACY_TEST
	typedef std::atomic<uint32>	IndexType;
#else
	typedef uint32				IndexType;
#endif

	typedef SPSCStorageSelector<T, TCapacity, TDynamicStorage>	StorageSelector;
	typedef typename StorageSelector::Storage					Storage;

	IndexType	m_pushIndex;
	PadBuffer	m_padding0;
	IndexType	m_popIndex;
	PadBuffer	m_padding1;
	Storage		m_buffer;
};

} // rde

#endif
