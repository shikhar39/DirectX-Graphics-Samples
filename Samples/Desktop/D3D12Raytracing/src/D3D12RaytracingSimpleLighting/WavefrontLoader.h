#pragma once

#include "RayTracingHlslCompat.h"
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
class WavefrontLoader
{
private:
	std::vector<Index> m_indices;
	std::vector<Vertex> m_vertices;
public:
	WavefrontLoader(const std::string& filename);
	std::vector<Index> GetIndices() const { return  m_indices; };
	std::vector<Vertex> GetVertices() const { return m_vertices; }
};

