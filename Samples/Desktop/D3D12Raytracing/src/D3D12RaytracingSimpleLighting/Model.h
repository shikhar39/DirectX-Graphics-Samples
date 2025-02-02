#pragma once

#include "WavefrontLoader.h"
#include "ImageLoader.h"
// #include "D3D12RaytracingSimpleLighting.h"
//class D3D12RaytracingSimpleLighting;

class Model
{
public:
	struct D3DBuffer
	{
		ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;
	};
private:
	D3DBuffer m_indexBuffer;
	D3DBuffer m_vertexBuffer;
	D3DBuffer m_textureBuffer;


	std::string m_objFile;
	std::string m_texFile;
public:
	ComPtr<ID3D12Resource> BottomLevelAccelerationStructure;
	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc;
	XMMATRIX transform;


	Model(const std::string& objFilename, const std::string& texFileName) :m_objFile(objFilename), m_texFile(texFileName) {};
	Model(WavefrontLoader obj, ImageLoader::ImageData *img = nullptr);

	std::string GetOBJFileName() { return m_objFile; }
	std::string GetTexFileName() { return m_texFile; }

	D3DBuffer &GetIndexBuffer() { return m_indexBuffer; }
	D3DBuffer &GetVertexBuffer() { return m_vertexBuffer; }
	D3DBuffer &GetTextureBuffer() { return m_textureBuffer; }

};

