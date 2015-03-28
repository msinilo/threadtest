#ifndef CORE_ATOMIC_H
#define CORE_ATOMIC_H

// Basic atomic operations.

#include "core/BitMath.h"
#include "core/RdeAssert.h"

#if RDE_PLATFORM_WIN32
#	include "core/win32/Win32Interlocked.h"
#else
#	error "Platform not supported"
#endif

namespace rde
{
// T should be fundamental, 1/2/4/8 byte type.
template<typename T>
class Atomic
{
	RDE_COMPILE_CHECK(sizeof(T) <= 8);
	RDE_COMPILE_CHECK(RDE_IS_POWER_OF_TWO(sizeof(T)));
public:
	typedef	T	ValueType;

	T operator=(T rhs) 
	{
		Store_Release<T>(m_value, rhs);
		return m_value;
	}

	ValueType operator++()
	{
		return Interlocked::Increment(&m_value);
	}
	ValueType operator++(int)
	{
		return Interlocked::Increment(&m_value) - 1;
	}
	ValueType operator--()
	{
		return Interlocked::Decrement(&m_value);
	}
	ValueType operator--(int)
	{
		return Interlocked::Decrement(&m_value) + 1;
	}

	operator ValueType() const
	{
		return Load_Acquire(m_value);
	}

private:
	ValueType	m_value;
};

}

#endif
