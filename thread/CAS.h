#ifndef THREAD_CAS_H
#define THREAD_CAS_H

#include "core/Atomic.h"
#include "core/RdeAssert.h"

namespace rde
{
// Compare And Swap (32-bit). 
// if (*addr == value)
//	*addr = newvalue;
//	return true;
// else
//	return false;
RDE_FORCEINLINE bool CAS(volatile void* addr, volatile void* value, void* newvalue)
{
	// Possible problem on 64-bit platforms. But this wont work without serious
	// modifications anyway.
#if RDE_64
#		error "CAS not compatible with 64-bit platforms!"
#else
	#pragma warning(push)
	#pragma warning(disable: 4311)
	return Interlocked::CompareAndSwap((long*)addr, (long)value, (long)newvalue) == (long)value;
	#pragma warning(pop)
#endif
}

RDE_FORCEINLINE bool CAS(volatile void* addr, long cmp, long xchg)
{
	return Interlocked::CompareAndSwap((volatile long*)addr, cmp, xchg) == cmp;
}
RDE_FORCEINLINE bool CAS64(volatile void* addr, int64 cmp, int64 xchg)
{
	return Interlocked::CompareAndSwap((volatile int64*)addr, cmp, xchg) == cmp;
}

template<typename T>
RDE_FORCEINLINE bool CAS64(volatile void* addr, T cmp1, int32 cmp2, T xchg1, int32 xchg2)
{
	union MarkerNode
	{
		struct Value64
		{
			T		v;
			int32	i;
		} value;
		int64	value64;
		MarkerNode(T v_, int32 i_)
		{
			value.v = v_;
			value.i = i_;
		}
	};
	RDE_COMPILE_CHECK(sizeof(MarkerNode) == 8);

	MarkerNode oldV(cmp1, cmp2);
	MarkerNode newV(xchg1, xchg2);
	return Interlocked::CompareAndSwap((int64*)addr, oldV.value64, newV.value64) == oldV.value64;
}


} // rde

#endif //
