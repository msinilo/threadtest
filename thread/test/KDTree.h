#ifndef KDTREE_H
#define KDTREE_H

#include "core/RdeAssert.h"
#include <cmath>

namespace rde
{
class TypeRegistry;

// (just a minimal subset).

RDE_FORCEINLINE float M_Fabs(float x)	{ return fabsf(x); }
RDE_FORCEINLINE float M_Sqrt(float x)	{ return sqrtf(x); }
RDE_FORCEINLINE double M_Sqrt(double x) { return sqrt(x); }
RDE_FORCEINLINE float M_Cos(float x)	{ return cosf(x); }
RDE_FORCEINLINE float M_Sin(float x)	{ return sinf(x); }

struct Vector
{
	Vector()
	{
#if RDE_DEBUG
		const float FLT_NAN = sqrtf(-4.f);
		Set(FLT_NAN, FLT_NAN, FLT_NAN);
#endif
	}
	Vector(float x, float y, float z)
	{
		m_v[0] = x;
		m_v[1] = y;
		m_v[2] = z;
	}

	float operator[](int i) const
	{
		return m_v[i];
	}
	float& operator[](int i)
	{
		return m_v[i];
	}

	void Set(float x, float y, float z)
	{
		m_v[0] = x;
		m_v[1] = y;
		m_v[2] = z;
	}

	Vector& operator-=(const Vector& rhs)
	{
		m_v[0] -= rhs.m_v[0];
		m_v[1] -= rhs.m_v[1];
		m_v[2] -= rhs.m_v[2];
		return *this;
	}
	Vector& operator+=(const Vector& rhs)
	{
		m_v[0] += rhs.m_v[0];
		m_v[1] += rhs.m_v[1];
		m_v[2] += rhs.m_v[2];
		return *this;
	}
	Vector& operator*=(float f)
	{
		m_v[0] *= f;
		m_v[1] *= f;
		m_v[2] *= f;
		return *this;
	}

	float LengthSquared() const
	{
		return m_v[0]*m_v[0] + m_v[1]*m_v[1] + m_v[2]*m_v[2];
	}

	Vector& Normalize()
	{
		const float lsqr = LengthSquared();
		const float invLen = 1.f / M_Sqrt(lsqr);
		m_v[0] *= invLen;
		m_v[1] *= invLen;
		m_v[2] *= invLen;
		return *this;
	}

	static float Dot(const Vector& lhs, const Vector& rhs)
	{
		return lhs[0]*rhs[0] + lhs[1]*rhs[1] + lhs[2]*rhs[2];
	}
	static Vector Cross(const Vector& lhs, const Vector& rhs)
	{
		return Vector(	lhs[1] * rhs[2] - lhs[2] * rhs[1],
						lhs[2] * rhs[0] - lhs[0] * rhs[2],
						lhs[0] * rhs[1] - lhs[1] * rhs[0]);
	}
	static Vector Min(const Vector& lhs, const Vector& rhs)
	{
		return Vector(	lhs[0] < rhs[0] ? lhs[0] : rhs[0],
						lhs[1] < rhs[1] ? lhs[1] : rhs[1],
						lhs[2] < rhs[2] ? lhs[2] : rhs[2]);
	}
	static Vector Max(const Vector& lhs, const Vector& rhs)
	{
		return Vector(	lhs[0] > rhs[0] ? lhs[0] : rhs[0],
						lhs[1] > rhs[1] ? lhs[1] : rhs[1],
						lhs[2] > rhs[2] ? lhs[2] : rhs[2]);
	}

private:
	float	m_v[3];
};
RDE_FORCEINLINE Vector operator-(const Vector& lhs, const Vector& rhs)
{
	Vector nrv(lhs);
	nrv -= rhs;
	return nrv;
}

class KDTree
{
	struct Node
	{
		bool IsLeaf() const
		{
			return (m_leftChildIndex & 0x3) == 0x3;
		}
		int GetAxis() const
		{
			return (m_leftChildIndex & 0x3);
		}
		void InitLeaf(int firstTriangleIndex, int numTriangles)
		{
			m_firstTriangleIndex = (firstTriangleIndex << 3) | 0x3;
			m_numTriangles = numTriangles;
			RDE_ASSERT(IsLeaf());
		}
		void InitBranch(int leftChildIndex, float splitCoord, int axis)
		{
			RDE_ASSERT(axis >= 0 && axis < 3);
			m_leftChildIndex = (leftChildIndex << 3) | axis;
			m_splitCoord = splitCoord;
			RDE_ASSERT(!IsLeaf());
		}
		int GetFirstTriangleIndex() const
		{
			return (m_firstTriangleIndex >> 3);
		}

		// Lower 3 bits of firstTriangleIndex and leftChildIndex
		// are taken by axis code (0, 1, 2 or 3 for leaf).
		// Note: current implementation exploits the fact that node size is exactly
		// 8 == sizeof(byte). Don't change.
#pragma warning(push)
#pragma warning(disable: 4201)	// nameless struct/union
		union
		{
			struct 
			{
				uint32	m_firstTriangleIndex;
				uint32	m_numTriangles;
			};
			struct
			{
				uint32	m_leftChildIndex;
				float	m_splitCoord;
			};
		};
	};
#pragma warning(pop)
	static const size_t NODE_SIZE	= 8;
	RDE_COMPILE_CHECK(sizeof(Node) == NODE_SIZE);

public:
	KDTree();
	~KDTree();

	void AddTriangle(const Vector& v0, const Vector& v1, const Vector& v2);
	void Build();

	bool RayCast(const Vector& origin, const Vector& dir, float maxRange, float& hitDist);

	bool Save(const char* fileName, TypeRegistry& typeRegistry) const;
	bool Load(const char* fileName, TypeRegistry& typeRegistry);

	struct Impl;
private:
	void FreeImpl();

	Impl*	m_impl;
};

} // rde

#endif
