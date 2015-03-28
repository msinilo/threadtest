#include "SimpleModel.h"
#include "rply.h"
#include "core/System.h"

namespace
{
template<class T>
struct ClosePly
{
	static void Delete(T* ptr)
	{
		ply_close(ptr);
	}
};

int VertexCallback(p_ply_argument arg)
{
	void* userData(0);
	long vertexComponent(0);
	ply_get_argument_user_data(arg, &userData, &vertexComponent);

	SimpleModel::MeshData* meshData = static_cast<SimpleModel::MeshData*>(userData);
	RDE_ASSERT(meshData != 0);
	RDE_ASSERT(vertexComponent >= 0 && vertexComponent < 3);

	long vertexIndex;
	ply_get_argument_element(arg, NULL, &vertexIndex);
	RDE_ASSERT(vertexIndex >= 0 && vertexIndex < meshData->m_numVerts);
	const float v = (float)ply_get_argument_value(arg);
	//RDE_ASSERT(M_Finite(v));
	meshData->m_vertexData.Get()[vertexIndex * 3 + vertexComponent] = v;

	return 1;
}

int FaceCallback(p_ply_argument arg)
{
	void* userData(0);
	ply_get_argument_user_data(arg, &userData, 0);
	SimpleModel::MeshData* meshData = static_cast<SimpleModel::MeshData*>(userData);
	RDE_ASSERT(meshData != 0);
	long triIndex;
	ply_get_argument_element(arg, NULL, &triIndex);
	RDE_ASSERT(triIndex >= 0 && triIndex < meshData->m_numTris);

	long len, vertexIndex;
	ply_get_argument_property(arg, NULL, &len, &vertexIndex);
	if (vertexIndex >= 0 && vertexIndex < 3)
	{
		RDE_ASSERT(meshData->m_indices.Get()[triIndex * 3 + vertexIndex] == 0);
		const rde::uint32 i = (rde::uint32)ply_get_argument_value(arg);
		meshData->m_indices.Get()[triIndex * 3 + vertexIndex] = i;
	}
	return 1;
}

} // <anonymous> namespace

SimpleModel::MeshData::~MeshData()
{
}
void SimpleModel::MeshData::Init(long numVerts, long numTris)
{
	m_numVerts = numVerts;
	m_vertexData.Reset(new float[m_numVerts * 3]);

	m_numTris = numTris;
	m_indices.Reset(new rde::uint32[numTris * 3]);
	rde::Sys::MemSet(m_indices.Get(), 0, numTris * 3 * 4);
}

SimpleModel::SimpleModel()
{
}
bool SimpleModel::LoadPLY(const char* fileName)
{
	p_ply ply = ply_open(fileName, NULL);
	if (!ply)
		return false;

	rde::ScopedPtr<t_ply_, ClosePly> sply(ply);
	if (!ply_read_header(ply))
		return false;

	const long numVerts = ply_set_read_cb(ply, "vertex", "x", VertexCallback, &m_mesh, 0);
	ply_set_read_cb(ply, "vertex", "y", VertexCallback, this, 1);
	ply_set_read_cb(ply, "vertex", "z", VertexCallback, this, 2);

	const long numTris = ply_set_read_cb(ply, "face", "vertex_indices", FaceCallback, &m_mesh, 0);
	m_mesh.Init(numVerts, numTris);
	return ply_read(ply) != 0;
}

