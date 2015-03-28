#ifndef CORE_MAX_ALIGN_H
#define CORE_MAX_ALIGN_H

#include "core/System.h"

namespace rde
{
	union MaxAlign
	{
		char m_char;
		short m_short;
		int m_int;
		long m_long;
		double m_double;
		long double m_longDouble;
		float m_float;
		int64 m_int64;
		MaxAlign* m_this;
		void* m_ptr;
		void* (*m_fptr)(void*);
		char MaxAlign::* m_memberPtr;
	};

	// Trick found in Intel's TBB.
	template<size_t N> struct TypeWithAlignment
	{
		MachineTypeWithStrictestAlignment	member;
	};
	template<> struct TypeWithAlignment<1> { uint8 member; };
	template<> struct TypeWithAlignment<2> { uint16 member; };
	template<> struct TypeWithAlignment<4> { uint32 member; };
	template<> struct TypeWithAlignment<8> { uint64 member; };

	template<size_t N, typename T>
	struct WorkAroundAlignmentBug
	{
		static const size_t	kAlignment = __alignof(T); 
	};
#define RDE_TYPE_WITH_ALIGNMENT_AT_LEAST_AS_STRICT(T)	\
	TypeWithAlignment<WorkAroundAlignmentBug<sizeof(T), T>::kAlignment>
}

#endif // 
