#pragma once

constexpr size_t VERTEX_SIZE = 3;

struct Vertex
{
	float x, y;
	float u, v;
};


struct Triangle
{

public:
	size_t GetVerticiesCount() { return VERTEX_SIZE; }
	size_t GetVerticiesSize() { return sizeof(Vertex) * GetVerticiesCount(); }

public:
	Vertex m_Verticies[VERTEX_SIZE];

};