#pragma once

#include "RayTracingHlslCompat.h"
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
class WavefrontLoader
{
private:
public:
	std::vector<Index> m_indices;
	std::vector<Vertex> m_vertices;
	WavefrontLoader(const std::string& filename);
};

