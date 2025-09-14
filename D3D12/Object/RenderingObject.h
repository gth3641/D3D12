#pragma once
#include "Object.h"
#include "Util/Util.h"

#include <vector>

class RenderingObject : public Object
{
public:
	RenderingObject();

public: // Functions

	const std::vector<Triangle>& GetTriangleVector() const { return m_Triangle; }
	Triangle* GetTriagleByIndex(size_t index);
	size_t GetTriangleIndex() const { return m_Triangle.size(); }

	int GetVertexCount();

	void AddTriangle(const Vertex* vertex, size_t size);

private: // Variables
	std::vector<Triangle> m_Triangle;
	
};

