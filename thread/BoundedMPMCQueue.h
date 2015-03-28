#ifndef THREAD_BOUNDEDMPMCQUEUE_H
#define THREAD_BOUNDEDMPMCQUEUE_H

#include "thread/RelacyWrappers.h"
#if !RDE_DBG_RELACY_TEST
#	include "core/BitMath.h"
#endif

namespace rde
{
template<typename T>
class BoundedMPMCQueue
{
public:
	explicit BoundedMPMCQueue(uint32 maxCapacity)
	:	m_items(new Item[maxCapacity]),
		m_mask(maxCapacity - 1)
	{
#if !RDE_DBG_RELACY_TEST
		RDE_ASSERT(IsPowerOfTwo(maxCapacity));
#endif

		for (uint32 i = 0; i < maxCapacity; ++i)
			Store_Relaxed(m_items[i].m_sequence, i);

		Store_Relaxed(m_enqueuePos, 0ul);
		Store_Relaxed(m_dequeuePos, 0ul);
	}
	~BoundedMPMCQueue()
	{
		delete[] m_items;
	}

	bool Enqueue(const T& data)
	{
		uint32 pos = Load_Relaxed(m_enqueuePos);
		Item* item;
		while (true)
		{
			item = m_items + (pos & m_mask);
			const uint32 seq = Load_Acquire(item->m_sequence);
			const int32 diff = (int32)(seq - pos);
			if (diff == 0)
			{
				if (CAS(&m_enqueuePos, pos, pos + 1))
					break;
			}
			else if (diff < 0)
			{
				return false;
			}
			else	// diff > 0
			{
				pos = Load_Relaxed(m_enqueuePos);
			}
		}
		item->m_data = data;
		Store_Release(item->m_sequence, pos + 1);
		return true;
	}

	bool Dequeue(T& data)
	{
		Item* item;
		uint32 pos = Load_Relaxed(m_dequeuePos);
		while (true)
		{
			item = m_items + (pos & m_mask);
			const uint32 seq = Load_Acquire(item->m_sequence);
			const int32 diff = (int32)(seq - (pos + 1));
			if (diff == 0)
			{
				if (CAS(&m_dequeuePos, pos, pos + 1))
					break;
			}
			else if (diff < 0)
			{
				return false;
			}
			else	// diff > 0
			{
				pos = Load_Relaxed(m_dequeuePos);
			}
		}
		data = item->m_data;
		Store_Release(item->m_sequence, pos + m_mask + 1);
		return true;
	}

private:
	RDE_FORBID_COPY(BoundedMPMCQueue);

	struct Item
	{
		RDE_ATOMIC_VAR(uint32)	m_sequence;
		T						m_data;
	};
    static const size_t         kCacheLineSize = 64;
    typedef uint8               CacheLinePad[kCacheLineSize];

	CacheLinePad			m_pad0;
	Item* const				m_items;
	uint32					m_mask;
	CacheLinePad			m_pad1;
	RDE_ATOMIC_VAR(uint32)	m_enqueuePos;
	CacheLinePad			m_pad2;
	RDE_ATOMIC_VAR(uint32)	m_dequeuePos;
	CacheLinePad			m_pad3;
};
}

#endif
