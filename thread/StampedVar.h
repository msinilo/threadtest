#ifndef THREAD_STAMPED_VAR_H
#define THREAD_STAMPED_VAR_H

#if !RDE_DBG_RELACY_TEST
#	include "core/RdeAssert.h"	// RDE_COMPILE_CHECK
#endif

namespace rde
{
// Variable (32-bit) + modification counter.
// Helper structure for lock-free algorithms to avoid ABA problems.
template<typename T>
union StampedVar
{
#if !RDE_DBG_RELACY_TEST
	RDE_COMPILE_CHECK(sizeof(T) == 4);
#endif
	RDE_ALIGN(8) struct
	{
		uint32	count;
		T		var;
	} m_v32;
	RDE_ALIGN(8) int64		m_v64;

	StampedVar(): m_v64(0) {}
	StampedVar(T var, uint32 count)
	{
		Set(var, count);
	}
	explicit StampedVar(int64 i64): m_v64(i64) {}
	StampedVar(const StampedVar& rhs): m_v64(rhs.m_v64) {}

	StampedVar& operator=(const StampedVar& rhs)
	{
		m_v64 = rhs.m_v64;
		return *this;
	}
	bool operator==(const StampedVar& rhs) const
	{
		return m_v64 == rhs.m_v64;
	}
	void Set(T var, uint32 count)
	{
		m_v32.var = var;
		m_v32.count = count;
	}
};

#if !RDE_DBG_RELACY_TEST
// if (*addr == cmp)
//		*addr = xchg
//		return true
// else
//		return false
template<typename T>
RDE_FORCEINLINE bool CAS64(volatile void* addr, const StampedVar<T>& cmp, const StampedVar<T>& xchg)
{
	RDE_ASSERT((ptrdiff_t(addr) & 0x7) == 0);
	RDE_ASSERT((ptrdiff_t(&cmp) & 0x7) == 0);
	RDE_ASSERT((ptrdiff_t(&xchg) & 0x7) == 0);
	return Interlocked::CompareAndSwap((volatile int64*)addr, cmp.m_v64, xchg.m_v64) == cmp.m_v64;
}
template<typename T>
RDE_FORCEINLINE void FetchAndStore64(volatile void* addr, const StampedVar<T>& newValue)
{
	RDE_ASSERT((ptrdiff_t(addr) & 0x7) == 0);
	RDE_ASSERT((ptrdiff_t(&newValue) & 0x7) == 0);
	Interlocked::FetchAndStore((volatile int64*)addr, newValue.m_v64);
}
template<typename T>
RDE_FORCEINLINE StampedVar<T> LoadStampedVar64(const StampedVar<T>& val)
{
	return StampedVar<T>(Interlocked::CompareAndSwap((volatile int64*)(&val.m_v64), 0, 0));
}

#else

template<typename T>
RDE_FORCEINLINE StampedVar<T> LoadStampedVar64(const std::atomic<StampedVar<T> >& val)
{
	return StampedVar<T>(Load_Acquire(val));
}

template<typename T>
inline std::ostream& operator << (std::ostream& s, StampedVar<T> const& right)
{
    return s << "{" << right.m_v32.var << "," << right.m_v32.count << "}";
}
#endif

}

#endif

