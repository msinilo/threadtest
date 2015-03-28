#ifndef RELACY_WRAPPERS_H
#define RELACY_WRAPPERS_H

#if RDE_DBG_RELACY_TEST

#ifdef CORE_ATOMIC_H
#	error "If Relacy tests are needed, RelacyWrappers must be included instead of core/Atomic.h"
#endif

#include "core/Config.h"

#define RDE_ATOMIC_VAR(VARTYPE)		std::atomic<VARTYPE>
#define RDE_RELACY_YIELD(backoff)	backoff.yield($)
#define RDE_ASSERT					RL_ASSERT

namespace rde
{
typedef long	Atomic32;

// Defines, not functions, so that it results in a more obvious Relacy log
// (with actual points of execution and not Load_xxx/Store_xxx functions in the log).
#define Load_Relaxed(v)			(v).load(rl::memory_order_relaxed)
#define Store_Relaxed(dst, v)	(dst).store((v), rl::memory_order_relaxed);
#define Load_Acquire(v)			(v).load(rl::memory_order_acquire)
#define Store_Release(dst, v)	(dst).store((v), rl::memory_order_release);
#define Load_SeqCst(v)			(v).load(rl::memory_order_seq_cst)
#define Store_SeqCst(dst, v)	(dst).store((v), rl::memory_order_seq_cst)
#define MemoryBarrier()			std::atomic_thread_fence(std::memory_order_seq_cst)

template<typename T> 
inline bool CAS(RDE_ATOMIC_VAR(T)* addr, T expectedValue, T newValue)
{
	return (*addr).compare_exchange_strong(expectedValue, newValue, rl::memory_order_seq_cst);
}
template<typename T> 
inline bool CAS64(RDE_ATOMIC_VAR(T)* addr, T expectedValue, T newValue)
{
	return (*addr).compare_exchange_strong(expectedValue, newValue, rl::memory_order_seq_cst);
}
template<typename T> 
inline void FetchAndStore64(RDE_ATOMIC_VAR(T)* addr, T newValue)
{
	(*addr).exchange(newValue, rl::memory_order_seq_cst);
}

} // rde
#else
#	include "thread/CAS.h"
#	define RDE_ATOMIC_VAR(VARTYPE)		VARTYPE
#	define RDE_RELACY_YIELD(backoff)	
#endif

#endif
