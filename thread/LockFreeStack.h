#ifndef THREAD_LOCKFREESTACK_H
#define THREAD_LOCKFREESTACK_H

#include "thread/RelacyWrappers.h"

namespace rde
{
#include <cstddef>

// Lock-free LIFO stack.
// Stack doesn't deal with allocation/deletion of nodes,
// so it doesn't have to care if allocator is lock free as well.
template<typename T, RDE_ATOMIC_VAR(T*) (T::*TNext) = &T::next>
class LockFreeStack
{
public:
	LockFreeStack()
	{
#if !RDE_DBG_RELACY_TEST
		RDE_COMPILE_CHECK(sizeof(m_top) == 4);
		RDE_COMPILE_CHECK(sizeof(m_popCounter) == 4);
		RDE_COMPILE_CHECK(offsetof(
				LockFreeStack<T>, m_popCounter) == offsetof(LockFreeStack<T>, m_top) + 4);
#endif
		Store_Relaxed(m_top, (T*)0);
		Store_Relaxed(m_popCounter, int32(0));
	}

	void Push(T* node)
	{
		RDE_ASSERT(node);
		T* top;
		while (true)
		{
			// Link at head
			top = Load_Acquire(m_top);
			Store_Relaxed(node->*TNext, top);

			if (CAS(&m_top, top, node))
				break;
		} 
	}

	// @return	top node or NULL if empty.
	T* Pop()
	{
		int32 popCounter;
		T* top;
		while (true)
		{
			popCounter = Load_Acquire(m_popCounter);
			top = Load_Acquire(m_top);
			if (!top)
				return 0;
			T* next = Load_Relaxed(top->*TNext);
#if RDE_DBG_RELACY_TEST
			// No need to test ABA, we don't reclaim memory in Relacy tests.
			// @FIXME:	This should be solved in a more elegant (uniform way), right
			// now there is possibility in bugs in the other code path.
			if (CAS(&m_top, top, next))
				break;
#else
			if (CAS64(&m_top, top, popCounter, next, popCounter + 1))
				break;
#endif
		}
		return top;
	}
	bool IsEmpty() const	{ return Load_Acquire(m_top) == 0; }

private:
	RDE_ATOMIC_VAR(T*)		m_top;
	RDE_ATOMIC_VAR(int32)	m_popCounter;
};

} // rde

#endif

