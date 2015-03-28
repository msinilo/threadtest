#include "KDTree.h"
#include "ReflectionHelpers.h"
#include "reflection/Type.h"
#include "rdestl/sort.h"
#include "rdestl/vector.h"
#include "io/FileStream.h"
#include "core/Console.h"
#include <float.h>	// _finite

#define KDTREE_OUTPUT_BUILD_STATS	0

#if KDTREE_OUTPUT_BUILD_STATS
#	include "core/Timer.h"
#endif

namespace
{
const float FLT32_MAX			= 3.402823466e+38f;
const int MAX_STACK_DEPTH		= 40;
const int MIN_FACES_PER_LEAF	= 64;

const float TRAVERSE_COST		= 1.f;
const float INTERSECTION_COST	= 80.f;

#define M_Finite(x)	_finite((x))

struct Triangle
{
	void GetVerts(rde::Vector& v0, rde::Vector& v1, rde::Vector& v2) const
	{
		v0 = v[0];
		v1 = v[1];
		v2 = v[2];
	}

	rde::Vector	v[3];
};
// Structure for more efficient ray-tri intersections (optimized projection method).
// See Ingo Wald papers for more info.
struct TriangleAccel
{
	TriangleAccel() {};
	explicit TriangleAccel(int index, const Triangle& tri)
	{
		FromTriangle(index, tri);
	}
	void FromTriangle( int index, const Triangle& tri)
	{
		using rde::M_Fabs;
		using rde::Vector;
		triangleIndex = index;

		Vector v0, v1, v2;
		tri.GetVerts(v0, v1, v2);

		const Vector edge1 = v2 - v0;
		const Vector edge2 = v1 - v0;

		const Vector e1xe2 = Vector::Cross(edge1, edge2);
		// Find major axis.
		axis = 2;
		if (M_Fabs(e1xe2[0]) > M_Fabs(e1xe2[1]))
		{
			if (M_Fabs(e1xe2[0]) > M_Fabs(e1xe2[2]))
				axis = 0;
		}
		else
		{
			if (M_Fabs(e1xe2[1]) > M_Fabs(e1xe2[2]))
				axis = 1;
		}
		int u = (axis + 1) % 3;
		int v = (axis + 2) % 3;
		n_u = e1xe2[u] / e1xe2[axis];
		n_v = e1xe2[v] / e1xe2[axis];
		n_d = Vector::Dot(v0, e1xe2) / e1xe2[axis];
		const float invDet = 1.f / (edge1[u] * edge2[v] - edge1[v] * edge2[u]);
		e1_nu = edge1[u] * invDet;
		e1_nv = -edge1[v] * invDet;
		v0_u = v0[u];
		e2_nu = edge2[v] * invDet;
		e2_nv = -edge2[u] * invDet;
		v0_v = v0[v];
	}

	rde::uint32	axis;
	float		n_u, n_v, n_d;	// Plane equation constants
	float		v0_u, v0_v;	
	float		e1_nu, e1_nv;
	float		e2_nu, e2_nv;
	rde::int32	triangleIndex;
	rde::uint32	padding;
};
RDE_COMPILE_CHECK(sizeof(TriangleAccel) == 48);

struct SplitCandidate
{
	enum SplitType
	{
		START,
		END
	};
	SplitCandidate(float t, rde::uint32 triangleIndex, SplitType type)
	:	m_t(t), m_triangleIndex(triangleIndex), m_splitType(type)
	{}

	bool operator<(const SplitCandidate& rhs) const
	{
		if (m_t == rhs.m_t)	// @TODO: precision problems?
			return m_splitType < rhs.m_splitType;
		return m_t < rhs.m_t;
	}
	float			m_t;
	rde::uint32		m_triangleIndex	: 31;	// Up to 2 147 483 647 tris.
	rde::uint32		m_splitType		: 1;
};
RDE_COMPILE_CHECK(sizeof(SplitCandidate) == 8);

struct StackElement
{
	int		nodeIndex;
	float	min, max;
	int		pading;
};
RDE_COMPILE_CHECK(sizeof(StackElement) == 16);

class AABB
{
public:
	AABB()
	{
		MakeEmpty();
	}

	// @post	IsEmpty()
	void MakeEmpty()
	{
		m_min.Set(+FLT32_MAX, +FLT32_MAX, +FLT32_MAX);
		m_max.Set(-FLT32_MAX, -FLT32_MAX, -FLT32_MAX);
		RDE_ASSERT(IsEmpty());
	}
	bool IsEmpty() const
	{
		const rde::Vector d = m_max - m_min;
		return d[0] <= 0.f && d[1] <= 0.f && d[2] <= 0.f;
	}

	void AddPoint(const rde::Vector& p)
	{
		m_min = rde::Vector::Min(m_min, p);
		m_max = rde::Vector::Max(m_max, p);
	}
	float GetAxisLength(int axis) const
	{
		return m_max[axis] - m_min[axis];
	}
	void ClipTo(const AABB& other)
	{
		m_min = rde::Vector::Max(m_min, other.m_min);
		m_max = rde::Vector::Min(m_max, other.m_max);
	}
	float CalcSurfaceArea() const
	{
		const rde::Vector d = m_max - m_min;
		return 2.f * (d[0] * d[1] + d[0] * d[2] + d[1] * d[2]);
	}
	void GetCorners(rde::Vector corners[8]) const
	{
		using rde::Vector;
		corners[0] = Vector(m_min[0], m_min[1], m_min[2]);
		corners[1] = Vector(m_max[0], m_min[1], m_min[2]);
		corners[2] = Vector(m_max[0], m_max[1], m_min[2]);
		corners[3] = Vector(m_min[0], m_max[1], m_min[2]);
		corners[4] = Vector(m_min[0], m_min[1], m_max[2]);
		corners[5] = Vector(m_max[0], m_min[1], m_max[2]);
		corners[6] = Vector(m_max[0], m_max[1], m_max[2]);
		corners[7] = Vector(m_min[0], m_max[1], m_max[2]);
	}

	rde::Vector	m_min;
	rde::Vector	m_max;
};
void AABB_AddTriangle(AABB& box, const Triangle& tri)
{
	box.AddPoint(tri.v[0]);
	box.AddPoint(tri.v[1]);
	box.AddPoint(tri.v[2]);
}

int Round2Int(double inValue) 
{
	return (int) (inValue+(.5-1.4e-11));
}
int Log2Int(float v) 
{
	return ((*(int *) &v) >> 23) - 127;
} 

// Kay/Kajiya
// Rough algo based on Christer Ericsson's book.
bool RayIntersectsAABB(const rde::Vector& origin, const rde::Vector& dir, const AABB& aabb, 
		float& cmin, float& cmax)
{
	using rde::Vector;
	cmin = -FLT32_MAX;
	cmax = +FLT32_MAX;

	Vector p1 = aabb.m_min - origin;
	Vector p2 = aabb.m_max - origin;

	// @unroll?
	for (int axis = 0; axis < 3; ++axis)
	{
		const float rcp = 1.f / dir[axis];
		float c1 = p1[axis] * rcp;
		float c2 = p2[axis] * rcp;

		if (M_Finite(c1) && M_Finite(c2))
		{
			if (c1 > c2)
				rde::swap(c1, c2);

			if (c1 > cmin)
				cmin = c1;
			if (c2 < cmax)
				cmax = c2;

			if (cmin > cmax)
				return false;
		}
		else
		{
			if (p1[axis] > 0.f || p2[axis] < 0.f )
				return false;
		}
	}
	return true;
}


} // <anonymous> namespace

namespace rde
{
template<> struct is_pod<Triangle>			{ enum { value = true }; };
template<> struct is_pod<TriangleAccel>		{ enum { value = true }; };
template<> struct is_pod<KDTree::Node>		{ enum { value = true }; };
template<> struct is_pod<SplitCandidate>	{ enum { value = true }; };

RDE_IMPL_GET_TYPE_NAME(rde::KDTree::Impl);

struct KDTree::Impl
{
	static const uint32	VERSION	= 0x0002;

	typedef rde::vector<Triangle>		Triangles;
	typedef rde::vector<TriangleAccel>	TriangleAccels;
	typedef rde::vector<Node>			Nodes;
	typedef rde::vector<SplitCandidate>	SplitCandidates;

	Impl()
	:	m_loaded(false),
		m_numLeaves(0),
		m_numTrianglesInLeaves(0),
		m_depth(0),
		m_maxDepth(0),
		m_currentDepth(0)
	{
	}

	void AddTriangle(const Vector& v0, const Vector& v1, const Vector& v2)
	{
		Triangle tri;
		tri.v[0] = v0;
		tri.v[1] = v1;
		tri.v[2] = v2;
		m_triangles.push_back(tri);
	}

	void Build()
	{
#if KDTREE_OUTPUT_BUILD_STATS
		rde::Timer timer;
		timer.Start();
		rde::Console::Profilef("Building KD tree...\n");
#endif
		TriangleAccels triangleAccels;
		ComputeTriangleAccelsAndAABB(triangleAccels);

		m_currentDepth = 0;
		m_maxDepth = Round2Int(8 + 1.3f * Log2Int(static_cast<float>(m_triangles.size())));
		if (m_maxDepth > MAX_STACK_DEPTH)
		{
			rde::Console::Warningf("Max kd tree depth [%d] > MAX_STACK_DEPTH [%d], clamping", 
				m_maxDepth, MAX_STACK_DEPTH);
			m_maxDepth = MAX_STACK_DEPTH;
		}

		m_numLeaves = 0;
		m_numTrianglesInLeaves = 0;

		m_nodes.push_back(Node());	// root
		BuildSubTree(0, triangleAccels, m_aabb);
#if KDTREE_OUTPUT_BUILD_STATS
		timer.Stop();
		rde::Console::Profilef("KD tree built in %f seconds.\n", timer.GetTimeInMs() / 1000.f);
		rde::Console::Profilef("Depth: %d level(s), %d tri(s) processed, %d tri(s) in leaves, "
			"%f tri(s)/leaf on average.\n", m_depth, m_triangles.size(), m_numTrianglesInLeaves,
			float(m_numTrianglesInLeaves) / m_numLeaves);
		const int usedMem = m_nodes.size() * sizeof(Node) + m_triangleAccels.size() * sizeof(TriangleAccel);
		rde::Console::Profilef("Used memory (approx): %d bytes (%d kb)\n", usedMem, usedMem >> 10);
#endif
		// Free "helper" structures.
		for (int i = 0; i < 3; ++i)
			m_splitCandidates[i].reset();
		m_triangles.reset();
	}
	
	void ComputeTriangleAccelsAndAABB(TriangleAccels& accels)
	{
		m_aabb.MakeEmpty();
		const int numTriangles = m_triangles.size();
		for (int i = 0; i < numTriangles; ++i)
		{
			const Triangle& tri = m_triangles[i];
			accels.push_back(TriangleAccel(i, tri));
			AABB_AddTriangle(m_aabb, tri);
		}
	}
	void BuildSubTree(int nodeIndex, TriangleAccels& accels, const AABB& nodeAABB)
	{
		++m_currentDepth;
		if (m_currentDepth > m_depth)
			m_depth = m_currentDepth;
		if (accels.size() < MIN_FACES_PER_LEAF || m_currentDepth == m_maxDepth)
		{
			MakeLeaf(nodeIndex, accels);
		}
		else
		{
			float bestCost;
			int bestAxis;
			const int bestIndex = FindBestSplit(nodeAABB, accels, bestCost, bestAxis);
			if (bestIndex < 0)
				MakeLeaf(nodeIndex, accels);
			else
				BuildBranch(nodeIndex, accels, nodeAABB, bestIndex, bestAxis);
		}
		--m_currentDepth;
	}
	// Divide triangles into left/right (according to split index/axis), continue
	// with building subtrees for those two groups of triangles.
	void BuildBranch(int nodeIndex, TriangleAccels& accels, const AABB& nodeAABB, int splitIndex, int splitAxis)
	{
		// TODO: consider using more precise box-tri overlap tests instead of box-box.

		TriangleAccels trisLeft;
		for (int i = 0; i < splitIndex; ++i)
		{
			const SplitCandidate& cand = m_splitCandidates[splitAxis][i];
			if (cand.m_splitType == SplitCandidate::START)
				trisLeft.push_back(accels[cand.m_triangleIndex]);
		}
		TriangleAccels trisRight;
		const int numSplits = m_splitCandidates[splitAxis].size();
		for (int i = splitIndex + 1; i < numSplits; ++i)
		{
			const SplitCandidate& cand = m_splitCandidates[splitAxis][i];
			if (cand.m_splitType == SplitCandidate::END)
				trisRight.push_back(accels[cand.m_triangleIndex]);
		}
		RDE_ASSERT(trisLeft.size() + trisRight.size() >= accels.size());
		accels.clear();

		SplitCandidate* bestSplit = &m_splitCandidates[splitAxis][splitIndex];
		const int leftChildIndex = MakeBranch(nodeIndex, bestSplit->m_t, splitAxis);

		AABB leftAABB = nodeAABB;
		leftAABB.m_max[splitAxis] = bestSplit->m_t;
		AABB rightAABB = nodeAABB;
		rightAABB.m_min[splitAxis] = bestSplit->m_t;

		BuildSubTree(leftChildIndex, trisLeft, leftAABB);
		BuildSubTree(leftChildIndex + 1, trisRight, rightAABB);
	}
	
	// -1 if no sense in splitting.
	int FindBestSplit(const AABB& nodeAABB, const TriangleAccels& accels, float& bestCost, int& bestAxis)
	{
		const float invTotalSurfaceArea = 1.f / nodeAABB.CalcSurfaceArea();
		// Require split to be better than not splitting at all.
		bestCost = (INTERSECTION_COST * accels.size()) * 0.98f;
		bestAxis = 3;
		int bestIndex = -1;
		for (int axis = 0; axis < 3; ++axis)
		{
			const float axisLength = nodeAABB.GetAxisLength(axis);
			// No sense in dividing along very short axis.
			if (axisLength <= 1e-10)
				continue;

			CollectSplitCandidates(axis, accels, nodeAABB);

			int numLeft = 0;
			int numRight = accels.size();
			AABB leftAABB = nodeAABB;
			AABB rightAABB = nodeAABB;
			for (int i = 0; i < m_splitCandidates[axis].size(); ++i)
			{
				SplitCandidate* itSplit = &m_splitCandidates[axis][i];
				if (itSplit->m_splitType == SplitCandidate::END)
					--numRight;
				const float t = itSplit->m_t;
				if (t > nodeAABB.m_min[axis] && t < nodeAABB.m_max[axis])
				{
					leftAABB.m_max[axis] = t;
					rightAABB.m_min[axis] = t;
					const float leftSurfaceArea = leftAABB.CalcSurfaceArea();
					const float rightSurfaceArea = rightAABB.CalcSurfaceArea();
					const float leftProb = leftSurfaceArea * invTotalSurfaceArea;
					const float rightProb = rightSurfaceArea * invTotalSurfaceArea;
					RDE_ASSERT(leftProb >= 0.f && leftProb <= 1.f);
					RDE_ASSERT(rightProb >= 0.f && rightProb <= 1.f);
					const float cost = TRAVERSE_COST + INTERSECTION_COST * 
						(numLeft * leftProb + numRight * rightProb);
					if (cost < bestCost)
					{
						bestCost = cost;
						bestAxis = axis;
						bestIndex = i;
					}
				}
				if (itSplit->m_splitType == SplitCandidate::START)
					++numLeft;
			}
		} // for each axis
		return bestIndex;
	}
	// Fills internal table of split candidates for given axis.
	void CollectSplitCandidates(int axis, const TriangleAccels& accels, const AABB& nodeAABB)
	{
		m_splitCandidates[axis].clear();
		const int numTris = accels.size();
		m_splitCandidates[axis].reserve(numTris * 2);
		for (int i = 0; i < numTris; ++i)
		{
			const Triangle& tri = m_triangles[accels[i].triangleIndex];
			AABB triangleAABB;
			AABB_AddTriangle(triangleAABB, tri);
			triangleAABB.ClipTo(nodeAABB);

			m_splitCandidates[axis].push_back(SplitCandidate(triangleAABB.m_min[axis], i, SplitCandidate::START));
			m_splitCandidates[axis].push_back(SplitCandidate(triangleAABB.m_max[axis], i, SplitCandidate::END));
		}
		rde::quick_sort(m_splitCandidates[axis].begin(), m_splitCandidates[axis].end());
	}

	void MakeLeaf(int nodeIndex, TriangleAccels& accels)
	{
		const int numTris = accels.size();
		++m_numLeaves;
		m_numTrianglesInLeaves += numTris;

		Node* node = m_nodes.begin() + nodeIndex;
		node->InitLeaf(m_triangleAccels.size(), numTris);
		for (int i = 0; i < numTris; ++i)
			m_triangleAccels.push_back(accels[i]);
	}
	int MakeBranch(int nodeIndex, float splitCoord, int splitAxis)
	{
		const int leftChildIndex = m_nodes.size();
		// Reserve two nodes for our children.
		m_nodes.resize(leftChildIndex + 2);
		Node* node = m_nodes.begin() + nodeIndex;
		node->InitBranch(leftChildIndex, splitCoord, splitAxis);
		return leftChildIndex;
	}

	// ASSERT slows stuff down significantly, so for KD tree ray trace is only for true debug builds.
	#if !RDE_KD_TREE_DEBUG_ENABLED
	#	define KD_ASSERT(x)
	#else
	#	pragma message("KD tree asserts enabled")
	#	define KD_ASSERT(x)	RDE_ASSERT((x))
	#endif
	bool RayCast(const rde::Vector& origin, const rde::Vector& dir, float maxRange, float& hitDist)
	{
		// Whole function compiles to about 300 lines of quite nicely optimized assembly.
		// Not inlined calls are: RayIntersectsAABB, PrimitiveHitCheck & _finite (that's my only grip, could
		// be solved in a different way).

		using rde::uint8;
		float min, max;
		if (!RayIntersectsAABB(origin, dir, m_aabb, min, max))
			return false;

		float invDir[3];
		invDir[0] = 1.f / dir[0];
		invDir[1] = 1.f / dir[1];
		invDir[2] = 1.f / dir[2];

		uint8 invDirAlmostZero[4];
		invDirAlmostZero[0] = M_Fabs(invDir[0]) < 1e-6f;
		invDirAlmostZero[1] = M_Fabs(invDir[1]) < 1e-6f;
		invDirAlmostZero[2] = M_Fabs(invDir[2]) < 1e-6f;

		uint8 rightward[4];
		rightward[0] = (dir[0] < 0) * NODE_SIZE;
		rightward[1] = (dir[1] < 0) * NODE_SIZE;
		rightward[2] = (dir[2] < 0) * NODE_SIZE;

		int nextStackIndex = 0;
		StackElement nodeStack[MAX_STACK_DEPTH];

		bool hit = false;
		int nodeIndex = 0;

		uint8* nodeMem = (uint8*)(&m_nodes[0]);

#		define KD_NODE_GET_AXIS(c)		((c) & 0x3)
#		define KD_NODE_IS_LEAF(a)		((a) == 0x3)
#		define KD_NODE_CLEAR_AXIS(c)	((c) &= ~0x3)
		while (true)
		{
			while (true)
			{
				Node* node = (Node*)(nodeMem + nodeIndex);
				int nearChildIndex = node->m_leftChildIndex;
				const int axis = KD_NODE_GET_AXIS(nearChildIndex);
				if (KD_NODE_IS_LEAF(axis))
				{
					const float t = PrimitiveHitCheck(origin, dir, node->GetFirstTriangleIndex(), node->m_numTriangles);
					const float range = M_Fabs(t);
					if (range < maxRange)
					{
						hit = true;
						maxRange = range;
						hitDist = range;
					}
					break;
				}
				else
				{
					if (!invDirAlmostZero[axis])
					{
					const float splitCoord = (node->m_splitCoord - origin[axis]) * invDir[axis];
					KD_NODE_CLEAR_AXIS(nearChildIndex);
//					int leftChildIndex = nearChildIndex;
					int farChildIndex = nearChildIndex + NODE_SIZE - rightward[axis];
					nearChildIndex += rightward[axis];
					KD_ASSERT(nearChildIndex - farChildIndex == NODE_SIZE || farChildIndex - nearChildIndex == NODE_SIZE);
					// NOTE: We don't have to shift indices, it's already multiplied by 8 and we use
					// byte array.
					//if (M_Finite(splitCoord))	// Possible performance problem (function call).
					//{
						if (splitCoord < min)
						{
							nodeIndex = farChildIndex;
						}
						else if (splitCoord > max)
						{
							nodeIndex = nearChildIndex;
						}
						else
						{
							// Push far on stack, jump to near.
							KD_ASSERT(nextStackIndex != MAX_STACK_DEPTH);
							nodeStack[nextStackIndex].nodeIndex = farChildIndex;
							nodeStack[nextStackIndex].max = max;
							nodeStack[nextStackIndex].min = splitCoord;
							++nextStackIndex;
							nodeIndex = nearChildIndex;
							max = splitCoord;
						}
						KD_ASSERT((nodeIndex % NODE_SIZE) == 0);
					}
					else 
					{
						KD_NODE_CLEAR_AXIS(nearChildIndex);
						int leftChildIndex = nearChildIndex;
						nodeIndex = (origin[axis] > node->m_splitCoord ? leftChildIndex + NODE_SIZE : leftChildIndex);
						KD_ASSERT((nodeIndex % NODE_SIZE) == 0);
					}
				}
			} // search for leaf.

			while (true)
			{
				if (--nextStackIndex < 0)
					return hit;

				if (nodeStack[nextStackIndex].max > -maxRange && nodeStack[nextStackIndex].min < maxRange)
				{
					nodeIndex = nodeStack[nextStackIndex].nodeIndex;
					KD_ASSERT((nodeIndex % NODE_SIZE) == 0);
					min = nodeStack[nextStackIndex].min;
					max = nodeStack[nextStackIndex].max;
					break;
				}
			}
		}
	}

	float PrimitiveHitCheck(const Vector& origin, const Vector& dir, int startPrim, int numPrims)
	{
		RDE_ALIGN(64) const uint32 modulo[] = { 0, 1, 2, 0, 1 };

		float tBest = FLT32_MAX;
		const TriangleAccel* itAccel = m_triangleAccels.begin() + startPrim;
		const TriangleAccel* itEnd = itAccel + numPrims;
		while (itAccel != itEnd)
		{
			const TriangleAccel& accel = *itAccel;
			++itAccel;
			const uint32 axis = accel.axis;
			const uint32 u = modulo[(axis + 1)];
			const uint32 v = modulo[(axis + 2)];

			const float d = 1.0f / (dir[axis] + accel.n_u * dir[u] + accel.n_v * dir[v]); 
			const float tHit = (accel.n_d - origin[axis] - accel.n_u * origin[u] - accel.n_v * origin[v]) * d; 
			if (tHit > tBest || tHit < 0.f)
				continue;
			const float hu = origin[u] + tHit * dir[u] - accel.v0_u;
			const float hv = origin[v] + tHit * dir[v] - accel.v0_v;
			const float uu = hv * accel.e1_nu + hu * accel.e1_nv;
			if (uu < 0)
				continue;
			const float vv = hu * accel.e2_nu + hv * accel.e2_nv; 
			if (vv < 0 || uu + vv > 1.0f) 
				continue;
	
			tBest = tHit;
		}
		return tBest;
	}

	bool Save(const char* fileName, rde::TypeRegistry& typeRegistry) const
	{
		bool success(false);
		FileStream f;
		if (f.Open(fileName, iosys::AccessMode::WRITE))
		{
			SaveObject(*this, f, typeRegistry, VERSION);

			f.Close();
			success = true;
		}
		return success;
	}

	Nodes			m_nodes;
	Triangles		m_triangles;
	TriangleAccels	m_triangleAccels;
	// Work (temp) buffer.
	SplitCandidates	m_splitCandidates[3];
	AABB			m_aabb;
	bool			m_loaded;	// True if loaded from file (LIP), in such case we can't execute destructor.
	// Stats
	int				m_numLeaves;
	int				m_numTrianglesInLeaves;
	int				m_depth;
	int				m_maxDepth;
	int				m_currentDepth;
};

KDTree::KDTree()
:	m_impl(new Impl)
{
}
KDTree::~KDTree()
{
	m_impl = 0;
}

void KDTree::AddTriangle(const Vector& v0, const Vector& v1, const Vector& v2)
{
	m_impl->AddTriangle(v0, v1, v2);
}
void KDTree::Build()
{
	m_impl->Build();
}

bool KDTree::RayCast(const Vector& origin, const Vector& dir, float maxRange, float& hitDist)
{
	return m_impl->RayCast(origin, dir, maxRange, hitDist);
}

bool KDTree::Save(const char* fileName, rde::TypeRegistry& typeRegistry) const
{
	return m_impl->Save(fileName, typeRegistry);
}
bool KDTree::Load(const char *fileName, rde::TypeRegistry &typeRegistry)
{
	bool success(false);
	FileStream f;
	if (f.Open(fileName, iosys::AccessMode::READ))
	{
		Impl* impl = LoadObject<Impl>(f, typeRegistry, Impl::VERSION);
		if (impl != 0)
		{
			FreeImpl();
			m_impl = impl;
			m_impl->m_loaded = true;
			success = true;
		}
	}
	return success;
}

void KDTree::FreeImpl()
{
	if (!m_impl->m_loaded)
		delete m_impl;
	else
		operator delete(m_impl);
}

} // rde
