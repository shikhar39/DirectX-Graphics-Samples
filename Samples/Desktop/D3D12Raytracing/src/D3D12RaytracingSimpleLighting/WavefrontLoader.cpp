#include "stdafx.h"
#include "WavefrontLoader.h"

WavefrontLoader::WavefrontLoader(const std::string& filename)
{
	struct WavefrontVertex {
		unsigned int p, n, t;
	};

	// reading buffers
	std::vector<XMFLOAT3> positions;
	std::vector<XMFLOAT3> normals;
	std::vector<XMFLOAT2> uvs;
	std::vector<WavefrontVertex> wv_vertices;



	std::ifstream objFile(filename);
	if (!objFile.is_open()) {
		throw std::runtime_error("Error: could not open file " + filename);
	}

	while (objFile.good()) {
		std::string line;
		std::getline(objFile, line);
		std::istringstream objLine(line);

		std::string mode;
		objLine >> mode;

		if (objLine.good()) {
			if (mode == "v") {
				XMFLOAT3 v;
				objLine >> v.x >> v.y >> v.z;
				positions.push_back(v);
			}
			else if (mode == "vn") {
				XMFLOAT3 vn;
				objLine >> vn.x >> vn.y >> vn.z;
				normals.push_back(vn);
			}
			else if (mode == "vt") {
				XMFLOAT2 vt;
				objLine >> vt.x >> vt.y;
				uvs.push_back(vt);
			}
			else if (mode == "f") {
				std::vector<WavefrontVertex> face;
				while (objLine.good()) {
					WavefrontVertex v;

					objLine >> v.p;
					if (objLine.fail()) break;

					if (objLine.peek() == '/') {
						objLine.ignore(1);

						if (objLine.peek() != '/') {
							objLine >> v.t;
						}

						if (objLine.peek() == '/') {
							objLine.ignore(1);
							objLine >> v.n;
						}
					}

					v.p -= 1;
					v.n -= 1;
					v.t -= 1;

					face.push_back(v);
				}

				if (face.size() == 3) {
					for (int i = 0; i < 3; i++) {
						wv_vertices.push_back(face[i]);
					}
				}
			}
		}

	}
	if (normals.empty()) {
		/*
		normals.resize(positions.size(), XMFLOAT3(0));

		for (size_t i = 0; i < wv_vertices.size() / 3; i++) {
			WavefrontVertex& a = wv_vertices[i * 3];
			WavefrontVertex& b = wv_vertices[i * 3 + 1];
			WavefrontVertex& c = wv_vertices[i * 3 + 2];

			a.n = a.p;
			b.n = b.p;
			c.n = c.p;

			XMFLOAT3 ab = XMVectorSubtract( positions[b.p], positions[a.p]);
			XMFLOAT3 ac = positions[c.p] - positions[a.p];


		}

		*/
	}
 	for (unsigned int i = 0; i < wv_vertices.size(); i++) {
		m_indices.push_back(i);
		m_vertices.push_back(Vertex{ positions[wv_vertices[i].p], normals[wv_vertices[i].n], uvs[wv_vertices[i].t] });
	}


}
