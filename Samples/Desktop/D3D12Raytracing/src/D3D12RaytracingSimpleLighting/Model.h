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
	std::string m_name;
public:
	ComPtr<ID3D12Resource> BottomLevelAccelerationStructure;
	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc;
	XMMATRIX transform;


	Model(const std::string& objFilename, const std::string& texFileName, XMMATRIX trans, const std::string& name = "Objecta") :m_objFile(objFilename), m_texFile(texFileName), transform(trans), m_name(name) {};
	Model(WavefrontLoader obj, ImageLoader::ImageData *img = nullptr);

	const std::string& GetOBJFileName() { return m_objFile; }
	const std::string& GetTexFileName() { return m_texFile; }
	const std::string& GetName() { return m_name; }

	D3DBuffer &GetIndexBuffer() { return m_indexBuffer; }
	D3DBuffer &GetVertexBuffer() { return m_vertexBuffer; }
	D3DBuffer &GetTextureBuffer() { return m_textureBuffer; }
};

