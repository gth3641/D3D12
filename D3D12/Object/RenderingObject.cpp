#include "RenderingObject.h"

RenderingObject::RenderingObject()
{

}

Triangle* RenderingObject::GetTriagleByIndex(size_t index)
{
	if (m_Triangle.size() > index)
	{
		return &m_Triangle[index];
	}

	return nullptr;
}

int RenderingObject::GetVertexCount()
{
	size_t size = m_Triangle.size();
	if (size <= 0)
	{
		return 0;
	}

	return size * _countof(m_Triangle[0].m_Verticies);
}

void RenderingObject::AddTriangle(const Vertex* vertex, size_t size)
{
	Triangle triangle;
	for (size_t i = 0; i < size; ++i)
	{
		triangle.m_Verticies[i] = vertex[i];
	}

	m_Triangle.push_back(triangle);
}
