#ifndef SIMPLEMODEL_H
#define SIMPLEMODEL_H

#include "core/ScopedPtr.h"

class SimpleModel
{
public:
	struct MeshData
	{
		MeshData():	m_numVerts(0), m_vertexData(0), m_numTris(0), m_indices(0) {}
		~MeshData();
		void Init(long numVerts, long numTris);

		long											m_numVerts;
		rde::ScopedPtr<float, rde::ArrayDeleter>		m_vertexData;
		long											m_numTris;
		rde::ScopedPtr<rde::uint32, rde::ArrayDeleter>	m_indices;
	};

	SimpleModel();
	bool LoadPLY(const char* fileName);

	const MeshData& GetMeshData() const	{ return m_mesh; }

private:
	RDE_FORBID_COPY(SimpleModel);
	MeshData	m_mesh;
};

#endif
