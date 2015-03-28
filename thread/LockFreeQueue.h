#ifndef THREAD_LOCKFREEQUEUE_H
#define THREAD_LOCKFREEQUEUE_H

#include "thread/RelacyWrappers.h"
#include "thread/StampedVar.h"
#include "core/Config.h"
#include <cstddef>	// offsetoff

namespace rde
{

// Lock-free FIFO queue.
// Multiple producers, multiple consumers.
// @references	Fober et al. "Lock-Free Techniques for Concurrent Access to Shared Objects".
template<typename T, RDE_ATOMIC_VAR(T*) (T::*TNext) = &T::next>
class LockFreeQueue
{
	typedef T	Node;
public:
	LockFreeQueue()
	{
		Store_Relaxed(m_dummy.*TNext, EndMarker());
		StampedNode dummyNode;
		dummyNode.m_v32.var = &m_dummy;
		dummyNode.m_v32.count = 0;
		Store_Relaxed(m_head, dummyNode);
		Store_Relaxed(m_tail, dummyNode);
	}
	void Enqueue(Node* node)
	{
		Store_Relaxed(node->*TNext, EndMarker());

#if RDE_DBG_RELACY_TEST
		rl::backoff b;
#endif
		StampedNode tail;
		StampedNode newTail;
		while (true)
		{
			tail = Load_Acquire(m_tail);

			// Link node at tail->next if it points to what we expect it to point (end)
			Node* endMarker = EndMarker();
			if (CAS(&(tail.m_v32.var->*TNext), endMarker, node))
				break;

			newTail.Set(Load_Relaxed(tail.m_v32.var->*TNext), tail.m_v32.count + 1);
			// ..tail->next not pointing to end marker. Try to push tail to next node.
			CAS64(&m_tail, tail, newTail);
			RDE_RELACY_YIELD(b);
		}
		newTail.Set(node, tail.m_v32.count + 1);
		CAS64(&m_tail, tail, newTail);
	}
	Node* Dequeue()
	{
		StampedNode head;
		StampedNode tail;
		Node* next;
#if RDE_DBG_RELACY_TEST
		rl::backoff b;
#endif
		while (true)
		{
			head = Load_Acquire(m_head);
			tail = Load_Acquire(m_tail);
			next = Load_Acquire(head.m_v32.var->*TNext);
			if (head.m_v32.count == Load_Acquire(m_head).m_v32.count)
			{
				if (head.m_v32.var == tail.m_v32.var)	// empty/falling behind?
				{
					if (next == EndMarker())	// empty
						return 0;
					// tail==head, non-empty. Progress tail.
					StampedNode newTail(next, tail.m_v32.count + 1);
					CAS64(&m_tail, head, newTail);
				}
				else if (next != EndMarker())
				{
					// Unlink head
					StampedNode newHead(next, head.m_v32.count + 1);
					if (CAS64(&m_head, head, newHead))
						break;
				}
			}
			RDE_RELACY_YIELD(b);
		}
		Node* ret(head.m_v32.var);
		if (ret == &m_dummy)	// trying to unlink dummy
		{
			Enqueue(ret);
			ret = Dequeue();
		}
		return ret;
	}

private:
	RDE_FORBID_COPY(LockFreeQueue);
	RDE_FORCEINLINE Node* EndMarker()	{ return (Node*)this; }

	typedef StampedVar<Node*>	StampedNode;

	// Do NOT touch the layout!
	RDE_ATOMIC_VAR(StampedNode)	m_head;
	RDE_ATOMIC_VAR(StampedNode)	m_tail;
	Node						m_dummy;
};

} // rde

#endif // THREAD_LOCKFREEQUEUE_H
