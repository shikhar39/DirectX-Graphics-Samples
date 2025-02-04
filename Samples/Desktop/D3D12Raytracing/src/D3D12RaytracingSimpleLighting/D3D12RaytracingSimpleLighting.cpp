//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12RaytracingSimpleLighting.h"
#include "DirectXRaytracingHelper.h"
#include "CompiledShaders\Raytracing.hlsl.h"
#include "WavefrontLoader.h"
#include "imgui-docking/imgui.h"
#include "imgui-docking/backends/imgui_impl_dx12.h"
#include "imgui-docking/backends/imgui_impl_win32.h"


using namespace std;
using namespace DX;

const wchar_t* D3D12RaytracingSimpleLighting::c_hitGroupName = L"MyHitGroup";
const wchar_t* D3D12RaytracingSimpleLighting::c_raygenShaderName = L"MyRaygenShader";
const wchar_t* D3D12RaytracingSimpleLighting::c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t* D3D12RaytracingSimpleLighting::c_missShaderName = L"MyMissShader";


const wchar_t* D3D12RaytracingSimpleLighting::c_shadowHitGroupName = L"ShadowHitGroup";
const wchar_t* D3D12RaytracingSimpleLighting::c_shadowMissShaderName = L"ShadowRayMissShader";
const wchar_t* D3D12RaytracingSimpleLighting::c_shadowAnyHitShaderName = L"ShadowRayAnyHitShader";

D3D12RaytracingSimpleLighting::D3D12RaytracingSimpleLighting(UINT width, UINT height, std::wstring name) :
	DXSample(width, height, name),
	m_raytracingOutputResourceUAVDescriptorHeapIndex(UINT_MAX)
{
	UpdateForSizeChange(width, height);
}

void D3D12RaytracingSimpleLighting::OnInit()
{
	m_deviceResources = std::make_unique<DeviceResources>(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_UNKNOWN,
		FrameCount,
		D3D_FEATURE_LEVEL_11_0,
		// Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
		// Since the sample requires build 1809 (RS5) or higher, we don't need to handle non-tearing cases.
		DeviceResources::c_RequireTearingSupport,
		m_adapterIDoverride
		);
	m_deviceResources->RegisterDeviceNotify(this);
	m_deviceResources->SetWindow(Win32Application::GetHwnd(), m_width, m_height);
	m_deviceResources->InitializeDXGIAdapter();

	ThrowIfFalse(IsDirectXRaytracingSupported(m_deviceResources->GetAdapter()),
		L"ERROR: DirectX Raytracing is not supported by your OS, GPU and/or driver.\n\n");

	m_deviceResources->CreateDeviceResources();
	m_deviceResources->CreateWindowSizeDependentResources();

	InitializeScene();

	CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();

	// Initialise Dear Imgui
	// InitImGui();

}

void D3D12RaytracingSimpleLighting::InitImGui() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(Win32Application::GetHwnd());


	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = m_deviceResources->GetD3DDevice();
	init_info.CommandQueue = m_deviceResources->GetCommandQueue();
	init_info.NumFramesInFlight = FrameCount;
	init_info.RTVFormat = m_deviceResources->GetBackBufferFormat();
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;


	init_info.SrvDescriptorHeap = nullptr;
	init_info.SrvDescriptorAllocFn = nullptr;
	init_info.SrvDescriptorFreeFn = nullptr;


	ImGui_ImplDX12_Init(&init_info);
}

// Update camera matrices passed into the shader.
void D3D12RaytracingSimpleLighting::UpdateCameraMatrices()
{
	auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

	m_viewDir = XMVector4Transform(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMMatrixRotationRollPitchYaw(m_altAngle, m_azAngle, 0));

	m_sceneCB[frameIndex].cameraPosition = m_eye;
	float fovAngleY = 45.0f;
	// XMMATRIX view = XMMatrixLookAtLH(m_eye, m_at, m_up);
	XMMATRIX view = XMMatrixLookAtLH(m_eye, m_eye + m_viewDir, m_up);
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovAngleY), m_aspectRatio, 1.0f, 125.0f);
	XMMATRIX viewProj = view * proj;

	m_sceneCB[frameIndex].projectionToWorld = XMMatrixInverse(nullptr, viewProj);
}

// Initialize scene rendering parameters.
void D3D12RaytracingSimpleLighting::InitializeScene()
{
	auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

	// Setup materials.
	{
		m_cubeCB.albedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// Setup camera.
	{
		// Initialize the view and projection inverse matrices.
		m_eye = { 0.0f, 2.0f, -5.0f, 1.0f };
		XMVECTOR right = { 1.0f, 0.0f, 0.0f, 0.0f };

		m_viewDir = XMVector4Normalize(- m_eye);
		// m_up = XMVector3Normalize(XMVector3Cross(m_viewDir, right));
		m_up = { 0.0f, 1.0f, 0.0f, 0.0f };
		/*
		// Rotate camera around Y axis.
		XMMATRIX rotate = XMMatrixRotationY(XMConvertToRadians(45.0f));
		m_eye = XMVector3Transform(m_eye, rotate);
		m_up = XMVector3Transform(m_up, rotate);
		*/
		
		UpdateCameraMatrices();
	}

	m_models.push_back(Model("teapot.obj", "triangles.png", XMMatrixTranslation(0.0, 2.0, 0.0)));
	m_models.push_back(Model("cube.obj", "triangles.png", XMMatrixScaling(50, 5, 50) * XMMatrixTranslation(0.0, -2.0, 0.0)));



	// Setup lights.
	{
		// Initialize the lighting parameters.
		XMFLOAT4 lightPosition;
		XMFLOAT4 lightAmbientColor;
		XMFLOAT4 lightDiffuseColor;

		lightPosition = XMFLOAT4(10.0f, 40.0f, 0.0f, 0.0f);
		m_sceneCB[frameIndex].lightPosition = XMLoadFloat4(&lightPosition);
		// m_models.push_back(Model("cube.obj", "triangles.png", XMMatrixTranslation(lightPosition.x, lightPosition.y, lightPosition.z)));

		lightAmbientColor = XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f);
		m_sceneCB[frameIndex].lightAmbientColor = XMLoadFloat4(&lightAmbientColor);

		lightDiffuseColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
		m_sceneCB[frameIndex].lightDiffuseColor = XMLoadFloat4(&lightDiffuseColor);
	}

	// Apply the initial values to all frames' buffer instances.
	for (auto& sceneCB : m_sceneCB)
	{
		sceneCB = m_sceneCB[frameIndex];
	}
}

// Create constant buffers.
void D3D12RaytracingSimpleLighting::CreateConstantBuffers()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto frameCount = m_deviceResources->GetBackBufferCount();
	
	// Create the constant buffer memory and map the CPU and GPU addresses
	const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	// Allocate one constant buffer per frame, since it gets updated every frame.
	size_t cbSize = frameCount * sizeof(AlignedSceneConstantBuffer);
	const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

	ThrowIfFailed(device->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&constantBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_perFrameConstants)));

	// Map the constant buffer and cache its heap pointers.
	// We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	ThrowIfFailed(m_perFrameConstants->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData)));
}



// Create resources that depend on the device.
void D3D12RaytracingSimpleLighting::CreateDeviceDependentResources()
{
	// Initialize raytracing pipeline.

	// Create raytracing interfaces: raytracing device and commandlist.
	CreateRaytracingInterfaces();

	// Create root signatures for the shaders.
	CreateRootSignatures();

	// Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
	CreateRaytracingPipelineStateObject();

	// Create a heap for descriptors.
	CreateDescriptorHeap();

	// Build geometry to be used in the sample.
	BuildGeometry();

	// Build raytracing acceleration structures from the generated geometry.
	BuildAccelerationStructures();

	// Create constant buffers for the geometry and the scene.
	CreateConstantBuffers();

	// Build shader tables, which define shaders and their local root arguments.
	BuildShaderTables();

	// Create an output 2D texture to store the raytracing result to.
	CreateRaytracingOutputResource();
}

void D3D12RaytracingSimpleLighting::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
{
	auto device = m_deviceResources->GetD3DDevice();
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;

	ThrowIfFailed( D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
	ThrowIfFailed(device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
}

void D3D12RaytracingSimpleLighting::CreateRootSignatures()
{
	auto device = m_deviceResources->GetD3DDevice();

	// Global Root Signature
	// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[4]; // Perfomance TIP: Order from most frequent to least frequent.
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // 1 output texture
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_models.size(), 1);  // Index Buffer, t0 is for acceleration structure
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_models.size(), 0, 1);  // Vertex Buffers
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, m_models.size(), 0, 2);  // Texture Buffers
		// ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // texture to be mapped on the object

		// CD3DX12_DESCRIPTOR_RANGE textureRange;
		// textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

		CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
		rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
		rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
		rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);
		rootParameters[GlobalRootSignatureParams::IndexBuffersSlot].InitAsDescriptorTable(1, &ranges[1]);
		rootParameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable(1, &ranges[2]);
		rootParameters[GlobalRootSignatureParams::TextureSlot].InitAsDescriptorTable(1, &ranges[3]);


		CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc(
			0,                               // Register (s0)
			D3D12_FILTER_MIN_MAG_MIP_LINEAR, // Filter mode
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // AddressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // AddressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, // AddressW
			0.0f,                            // MipLODBias
			16,                              // MaxAnisotropy
			D3D12_COMPARISON_FUNC_LESS_EQUAL,// Comparison function
			D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
			0.0f,                            // MinLOD
			D3D12_FLOAT32_MAX                // MaxLOD
		);


		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters, 1, &staticSamplerDesc );
		SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
	}

	// Local Root Signature
	// This is a root signature that enables a shader to have unique arguments that come from shader tables.
	{
		CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
		rootParameters[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants(SizeOfInUint32(m_cubeCB), 1);
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
	}
}

// Create raytracing device and command list.
void D3D12RaytracingSimpleLighting::CreateRaytracingInterfaces()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();

	ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
	ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
}

// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void D3D12RaytracingSimpleLighting::CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline)
{
	// Ray gen and miss shaders in this sample are not using a local root signature and thus one is not associated with them.

	// Local root signature to be used in a hit group.
	auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
	// Define explicit shader association for the local root signature. 
	{
		auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
		rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
		rootSignatureAssociation->AddExport(c_hitGroupName);
	}
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a DispatchRays() call,
// with all configuration options resolved, such as local signatures and other state.
void D3D12RaytracingSimpleLighting::CreateRaytracingPipelineStateObject()
{
	// Create 7 subobjects that combine into a RTPSO:
	// Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
	// Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
	// This simple sample utilizes default shader association except for local root signature subobject
	// which has an explicit association specified purely for demonstration purposes.
	// 1 - DXIL library
	// 1 - Triangle hit group
	// 1 - Shadow ray hit group
	// 1 - Shader config
	// 2 - Local root signature and association
	// 1 - Global root signature
	// 1 - Pipeline config
	CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };


	// DXIL library
	// This contains the shaders and their entrypoints for the state object.
	// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
	auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void *)g_pRaytracing, ARRAYSIZE(g_pRaytracing));
	lib->SetDXILLibrary(&libdxil);
	// Define which shader exports to surface from the library.
	// If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
	// In this sample, this could be ommited for convenience since the sample uses all shaders in the library. 
	{
		lib->DefineExport(c_raygenShaderName);
		lib->DefineExport(c_closestHitShaderName);
		lib->DefineExport(c_missShaderName);
		lib->DefineExport(c_shadowAnyHitShaderName);
		lib->DefineExport(c_shadowMissShaderName);
	}
	
	// Triangle hit group
	// A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB.
	// In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
	auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	hitGroup->SetClosestHitShaderImport(c_closestHitShaderName);
	hitGroup->SetHitGroupExport(c_hitGroupName);
	hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Triangle Hit group for shadow rays

	auto shadowHitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	shadowHitGroup->SetAnyHitShaderImport(c_shadowAnyHitShaderName);
	shadowHitGroup->SetHitGroupExport(c_shadowHitGroupName);
	hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	
	// Shader config
	// Defines the maximum sizes in bytes for the ray payload and attribute structure.
	auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	UINT payloadSize = sizeof(XMFLOAT4);    // float4 pixelColor
	UINT attributeSize = sizeof(XMFLOAT2);  // float2 barycentrics
	shaderConfig->Config(payloadSize, attributeSize);

	// Local root signature and shader association
	// This is a root signature that enables a shader to have unique arguments that come from shader tables.
	CreateLocalRootSignatureSubobjects(&raytracingPipeline);

	// Global root signature
	// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
	auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

	// Pipeline config
	// Defines the maximum TraceRay() recursion depth.
	auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	// PERFOMANCE TIP: Set max recursion depth as low as needed 
	// as drivers may apply optimization strategies for low recursion depths.
	UINT maxRecursionDepth = 2; // ~ primary rays only + 1 shadow ray. 
	pipelineConfig->Config(maxRecursionDepth);

#if _DEBUG
	PrintStateObjectDesc(raytracingPipeline);
#endif

	// Create the state object.
	ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
}

// Create 2D output texture for raytracing.
void D3D12RaytracingSimpleLighting::CreateRaytracingOutputResource()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

	// Create the output resource. The dimensions and format should match the swap-chain.
	auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(backbufferFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(
		&defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput)));
	NAME_D3D12_OBJECT(m_raytracingOutput);

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
	m_raytracingOutputResourceUAVDescriptorHeapIndex = AllocateDescriptor(&uavDescriptorHandle, 0);
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	device->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);
	m_raytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), m_raytracingOutputResourceUAVDescriptorHeapIndex, m_DescriptorIncrementSize);
	//m_raytracingOutputResourceUAVGpuDescriptor =  m_raytracingOutput->GetGPUVirtualAddress();
}

void D3D12RaytracingSimpleLighting::CreateDescriptorHeap()
{
	auto device = m_deviceResources->GetD3DDevice();

	/*
	*/
	{
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
		// Allocate a heap for 3 descriptors:
		// 1 - raytracing output texture SRV[1]
		// 2 - vertex and index buffer SRVs[2]
		// 4 - Texture SRV[1]
		// descriptorHeapDesc.NumDescriptors = 3; 
		descriptorHeapDesc.NumDescriptors = m_models.size() * 3 + 1; // Every object gets a VB, IB and tex Buffer + 1 for the raytracing output Texture SRV
		descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		descriptorHeapDesc.NodeMask = 0;
		device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_DescriptorHeap));

		NAME_D3D12_OBJECT(m_DescriptorHeap);

		m_DescriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

}

// Build geometry used in the sample.
void D3D12RaytracingSimpleLighting::BuildGeometry()
{
	auto device = m_deviceResources->GetD3DDevice();
	
	for (size_t i = 0; i < m_models.size(); i++) {
		auto& model = m_models[i];

		// Index in the descriptor buffer
		UINT descriptorIndexIB = 1 + i; 
		UINT descriptorIndexVB = 1 + m_models.size() + i ;
		UINT descriptorIndexTexBuf = 1 + m_models.size() * 2 + i;


		WavefrontLoader obj(model.GetOBJFileName());
		// ComPtr<ID3D12Resource> IndexUploadBuffer, VertexUploadBuffer, TextureUploadBuffer;
		ImageLoader::ImageData textureData;
		ThrowIfFalse(ImageLoader::LoadImageFromDisk(model.GetTexFileName(), textureData));

		AllocateUploadBuffer(device, obj.GetIndices().data(), obj.GetIndices().size() * sizeof(obj.GetIndices()[0]), &model.GetIndexBuffer().resource);

		AllocateUploadBuffer(device, obj.GetVertices().data(), obj.GetVertices().size() * sizeof(obj.GetVertices()[0]), &model.GetVertexBuffer().resource);
		


		descriptorIndexIB = CreateBufferSRV(&model.GetIndexBuffer(), obj.GetIndices().size() * sizeof(obj.GetIndices()[0]) / 4, 0, descriptorIndexIB);
		
		descriptorIndexVB = CreateBufferSRV(&model.GetVertexBuffer(), obj.GetVertices().size(), sizeof(obj.GetVertices()[0]), descriptorIndexVB);
		CreateTextureResource(textureData, &model.GetTextureBuffer(), descriptorIndexTexBuf);
	}
	
	// WavefrontLoader obj("teapot.obj");


	// AllocateUploadBuffer(device, indices, sizeof(indices), &m_indexBuffer.resource);
	// AllocateUploadBuffer(device, vertices, sizeof(vertices), &m_vertexBuffer.resource);

	// AllocateUploadBuffer(device, obj.GetIndices().data(), obj.GetIndices().size() * sizeof(obj.GetIndices()[0]), &m_indexBuffer.resource);
	// AllocateUploadBuffer(device, obj.GetIndices().data(), obj.GetIndices().size() * sizeof(obj.GetIndices()[0]), &m_model.GetIndexBuffer()->resource);

	// AllocateUploadBuffer(device, obj.GetVertices().data(), obj.GetVertices().size() * sizeof(obj.GetVertices()[0]), &m_model.GetVertexBuffer()->resource);


	// Vertex buffer is passed to the shader along with index buffer as a descriptor table.
	// Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
	// UINT descriptorIndexIB = CreateBufferSRV(&m_indexBuffer, sizeof(indices)/4, 0);
	// UINT descriptorIndexVB = CreateBufferSRV(&m_vertexBuffer, ARRAYSIZE(vertices), sizeof(vertices[0]));

	// UINT descriptorIndexIB = CreateBufferSRV(&m_indexBuffer, obj.GetIndices().size() * sizeof(obj.GetIndices()[0]) / 4, 0);
	// UINT descriptorIndexVB = CreateBufferSRV(&m_vertexBuffer, obj.GetVertices().size(), sizeof(obj.GetVertices()[0]));
	// ThrowIfFalse(descriptorIndexVB == descriptorIndexIB + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor index!");

	// ImageLoader::ImageData textureData;
	// ThrowIfFalse(ImageLoader::LoadImageFromDisk("triangles.png", textureData));
	// CreateTextureResource(textureData);


}

// Build acceleration structures needed for raytracing.
void D3D12RaytracingSimpleLighting::BuildAccelerationStructures()
{
	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	auto commandQueue = m_deviceResources->GetCommandQueue();
	auto commandAllocator = m_deviceResources->GetCommandAllocator();


	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

	// Reset the command list for the acceleration structure construction.
	commandList->Reset(commandAllocator, nullptr);
	auto* raytracingCommandList = m_dxrCommandList.Get();
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDesc(m_models.size());
	ComPtr<ID3D12Resource> instanceDescs;
	std::vector<ComPtr<ID3D12Resource>> BLscratchResources(m_models.size());

	//for (auto& model : m_models) {
	for(size_t i = 0; i < m_models.size(); i++) {
		auto& model = m_models[i];
		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.IndexBuffer = model.GetIndexBuffer().resource->GetGPUVirtualAddress();
		geometryDesc.Triangles.IndexCount = static_cast<UINT>(model.GetIndexBuffer().resource->GetDesc().Width) / sizeof(Index);
		geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
		geometryDesc.Triangles.Transform3x4 = 0;
		geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geometryDesc.Triangles.VertexCount = static_cast<UINT>(model.GetVertexBuffer().resource->GetDesc().Width) / sizeof(Vertex);
		geometryDesc.Triangles.VertexBuffer.StartAddress = model.GetVertexBuffer().resource->GetGPUVirtualAddress();
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

		// Mark the geometry as opaque. 
		// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
		// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
		geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

		// Get required sizes for an acceleration structure.
		
	
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &bottomLevelInputs = bottomLevelBuildDesc.Inputs;
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		bottomLevelInputs.Flags = buildFlags;
		bottomLevelInputs.NumDescs = 1;
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.pGeometryDescs = &geometryDesc;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
		ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

		AllocateUAVBuffer(device, bottomLevelPrebuildInfo.ScratchDataSizeInBytes, &BLscratchResources[i], D3D12_RESOURCE_STATE_COMMON, L"BLScratchResource[i]");

		// Allocate resources for acceleration structures.
		// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
		// Default heap is OK since the application doesn�t need CPU read/write access to them. 
		// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
		// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
		//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
		//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
		
		
		
		AllocateUAVBuffer(device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &model.BottomLevelAccelerationStructure, initialResourceState, L"BottomLevelAccelerationStructure");
		
		//instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
		XMMATRIX transform = model.transform;
		XMFLOAT3X4 transform3x4;
		XMStoreFloat3x4(&transform3x4, transform);

		instanceDesc[i].InstanceID = i;
		instanceDesc[i].InstanceMask = 1;
		instanceDesc[i].AccelerationStructure = model.BottomLevelAccelerationStructure->GetGPUVirtualAddress();
		memcpy(instanceDesc[i].Transform, &transform3x4, sizeof(instanceDesc[i].Transform));


		model.instanceDesc = instanceDesc[i];
		// Bottom Level Acceleration Structure desc
		{
			bottomLevelBuildDesc.ScratchAccelerationStructureData = BLscratchResources[i]->GetGPUVirtualAddress();
			bottomLevelBuildDesc.DestAccelerationStructureData = model.BottomLevelAccelerationStructure->GetGPUVirtualAddress();
		}

		raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
		CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(model.BottomLevelAccelerationStructure.Get());
		commandList->ResourceBarrier(1, &uavBarrier);

	}


	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC &topLevelBuildDesc = m_topLevelBuildDesc;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &topLevelInputs = topLevelBuildDesc.Inputs;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = buildFlags;
	topLevelInputs.NumDescs = m_models.size();
	topLevelInputs.pGeometryDescs = nullptr;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
	m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);


	AllocateUploadBuffer(device, instanceDesc.data(), instanceDesc.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &instanceDescs, L"InstanceDescs");

	ComPtr<ID3D12Resource> TLscratchResource;
	AllocateUAVBuffer(device, topLevelPrebuildInfo.ScratchDataSizeInBytes, &TLscratchResource, D3D12_RESOURCE_STATE_COMMON, L"TLScratchResource");
	


	AllocateUAVBuffer(device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_topLevelAccelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
	// Top Level Acceleration Structure desc
	{
		topLevelBuildDesc.DestAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
		topLevelBuildDesc.ScratchAccelerationStructureData = TLscratchResource->GetGPUVirtualAddress();
		topLevelBuildDesc.Inputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
	}

	raytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);

	
	
	// Kick off acceleration structure construction.
	m_deviceResources->ExecuteCommandList();

	// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
	m_deviceResources->WaitForGpu();
}

// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
void D3D12RaytracingSimpleLighting::BuildShaderTables()
{
	auto device = m_deviceResources->GetD3DDevice();

	void* rayGenShaderIdentifier;
	void* missShaderIdentifier;
	void* hitGroupShaderIdentifier;

	void* shadowMissShaderIdentifier;
	void* shadowHitGroupShaderIdentifier;

	auto GetShaderIdentifiers = [&](auto* stateObjectProperties)
	{
		rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
		missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
		hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_hitGroupName);

		shadowMissShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_shadowMissShaderName);
		shadowHitGroupShaderIdentifier= stateObjectProperties->GetShaderIdentifier(c_shadowHitGroupName);

	};

	// Get shader identifiers.
	UINT shaderIdentifierSize;
	{
		ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
		ThrowIfFailed(m_dxrStateObject.As(&stateObjectProperties));
		GetShaderIdentifiers(stateObjectProperties.Get());
		shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	}

	// Ray gen shader table
	{
		UINT numShaderRecords = 1;
		UINT shaderRecordSize = shaderIdentifierSize;
		ShaderTable rayGenShaderTable(device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
		rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
		m_rayGenShaderTable = rayGenShaderTable.GetResource();
	}

	// Miss shader table
	{
		UINT numShaderRecords = 2;
		UINT shaderRecordSize = shaderIdentifierSize;
		ShaderTable missShaderTable(device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
		missShaderTable.push_back(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
		missShaderTable.push_back(ShaderRecord(shadowMissShaderIdentifier, shaderIdentifierSize));
		m_missShaderTable = missShaderTable.GetResource();
	}

	// Hit group shader table
	{
		struct RootArguments {
			CubeConstantBuffer cb;
		} rootArguments;
		rootArguments.cb = m_cubeCB;

		UINT numShaderRecords = 2;
		UINT shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);
		ShaderTable hitGroupShaderTable(device, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
		hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));
		hitGroupShaderTable.push_back(ShaderRecord(shadowHitGroupShaderIdentifier, shaderIdentifierSize));
		m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
	}
}

void D3D12RaytracingSimpleLighting::UpdateAccelerationStructure() {

	auto device = m_deviceResources->GetD3DDevice();
	auto commandList = m_deviceResources->GetCommandList();
	auto commandQueue = m_deviceResources->GetCommandQueue();
	auto commandAllocator = m_deviceResources->GetCommandAllocator();

	// Reset the command list for the acceleration structure construction.
	commandList->Reset(commandAllocator, nullptr);



	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
	m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&m_topLevelBuildDesc.Inputs, &topLevelPrebuildInfo);
	ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);
	ComPtr<ID3D12Resource> scratchResource;
	AllocateUAVBuffer(m_deviceResources->GetD3DDevice(), topLevelPrebuildInfo.ScratchDataSizeInBytes, &scratchResource, D3D12_RESOURCE_STATE_COMMON, L"ScratchResource");

	// Create an instance desc for the bottom-level acceleration structure.
	ComPtr<ID3D12Resource> instanceDescs;
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDesc(m_models.size());
	
	for (size_t i = 0; i < m_models.size(); i++) {
		auto& model = m_models[i];

		XMMATRIX transform1 = model.transform;
		XMFLOAT3X4 transform3x4;
		XMStoreFloat3x4(&transform3x4, transform1);


		instanceDesc[i].InstanceID = i;
		instanceDesc[i].InstanceMask = 1;
		instanceDesc[i].AccelerationStructure = model.BottomLevelAccelerationStructure->GetGPUVirtualAddress();
		memcpy(instanceDesc[i].Transform, &transform3x4, sizeof(instanceDesc[i].Transform));

	}

	AllocateUploadBuffer(device, instanceDesc.data(), instanceDesc.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &instanceDescs, L"InstanceDescs");
	m_topLevelBuildDesc.SourceAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
	m_topLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
	m_topLevelBuildDesc.Inputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
	m_topLevelBuildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;


	m_dxrCommandList->BuildRaytracingAccelerationStructure(&m_topLevelBuildDesc, 0, nullptr);
	m_deviceResources->ExecuteCommandList();
	m_deviceResources->WaitForGpu();
}

// Update frame-based values.
void D3D12RaytracingSimpleLighting::OnUpdate()
{
	m_timer.Tick();
	CalculateFrameStats();
	float elapsedTime = static_cast<float>(m_timer.GetElapsedSeconds());
	auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
	auto prevFrameIndex = m_deviceResources->GetPreviousFrameIndex();

	if (m_keyWPressed) {
		m_eye = m_eye + m_viewDir * m_cameraMoveSpeed;
	}
	if (m_keySPressed) {
		m_eye = m_eye - m_viewDir * m_cameraMoveSpeed;
	}if (m_keyAPressed) {
		XMVECTOR right = XMVector3Cross(m_up, m_viewDir);
		m_eye = m_eye - right * m_cameraMoveSpeed;
	}if (m_keyDPressed) {
		XMVECTOR right = XMVector3Cross(m_up, m_viewDir);
		m_eye = m_eye + right * m_cameraMoveSpeed;
	}


	UpdateCameraMatrices();
	if (!m_animationPaused){
		if (m_objDistance < -10.0f) {
			m_objDistDelta = abs(m_objDistDelta);
		}
		else if (m_objDistance > 10.0f) {
			m_objDistDelta = -abs(m_objDistDelta);
		}
		m_objDistance += m_objDistDelta;
	}
	if (m_updateAccelerationStructure) {
		UpdateAccelerationStructure();
	}

}

void D3D12RaytracingSimpleLighting::DoRaytracing()
{
	auto commandList = m_deviceResources->GetCommandList();
	auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
	
	auto DispatchRays = [&](auto* commandList, auto* stateObject, auto* dispatchDesc)
	{
		// Since each shader table has only one shader record, the stride is same as the size.
		dispatchDesc->HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
		dispatchDesc->HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
		dispatchDesc->HitGroupTable.StrideInBytes = m_hitGroupShaderTable->GetDesc().Width/2;
		dispatchDesc->MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
		dispatchDesc->MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
		dispatchDesc->MissShaderTable.StrideInBytes = m_missShaderTable->GetDesc().Width /2;
		dispatchDesc->RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
		dispatchDesc->RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;
		dispatchDesc->Width = m_width;
		dispatchDesc->Height = m_height;
		dispatchDesc->Depth = 1;
		commandList->SetPipelineState1(stateObject);
		commandList->DispatchRays(dispatchDesc);
	};
	auto SetCommonPipelineState = [&](auto* descriptorSetCommandList)
	{
		ID3D12DescriptorHeap* descHeaps[] = {
			m_DescriptorHeap.Get()
		};
		descriptorSetCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);
		// Set index and successive vertex buffer decriptor tables
		// commandList->SetComputeRootUno(GlobalRootSignatureParams::OutputViewSlot, m_raytracingOutputResourceUAVGpuDescriptor);
		descriptorSetCommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 0, m_DescriptorIncrementSize));
		descriptorSetCommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::IndexBuffersSlot, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 1, m_DescriptorIncrementSize));
		descriptorSetCommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffersSlot, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 1 + m_models.size(), m_DescriptorIncrementSize));
		descriptorSetCommandList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::TextureSlot, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 1 + 2 * m_models.size(), m_DescriptorIncrementSize));
	};


	commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

	// Copy the updated scene constant buffer to GPU.
	memcpy(&m_mappedConstantData[frameIndex].constants, &m_sceneCB[frameIndex], sizeof(m_sceneCB[frameIndex]));
	auto cbGpuAddress = m_perFrameConstants->GetGPUVirtualAddress() + frameIndex * sizeof(m_mappedConstantData[0]);
	commandList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);
	
	// Bind the heaps, acceleration structure and dispatch rays.
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	SetCommonPipelineState(commandList);
	commandList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, m_topLevelAccelerationStructure->GetGPUVirtualAddress());
	// commandList->SetComputeRootUnorderedAccessView(GlobalRootSignatureParams::OutputViewSlot, m_raytracingOutput->GetGPUVirtualAddress());
	DispatchRays(m_dxrCommandList.Get(), m_dxrStateObject.Get(), &dispatchDesc);
}

// Update the application state with the new resolution.
void D3D12RaytracingSimpleLighting::UpdateForSizeChange(UINT width, UINT height)
{
	DXSample::UpdateForSizeChange(width, height);
}

// Copy the raytracing output to the backbuffer.
void D3D12RaytracingSimpleLighting::CopyRaytracingOutputToBackbuffer()
{
	auto commandList= m_deviceResources->GetCommandList();
	auto renderTarget = m_deviceResources->GetRenderTarget();

	D3D12_RESOURCE_BARRIER preCopyBarriers[2];
	preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
	preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commandList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

	commandList->CopyResource(renderTarget, m_raytracingOutput.Get());

	D3D12_RESOURCE_BARRIER postCopyBarriers[2];
	postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
}

// Create resources that are dependent on the size of the main window.
void D3D12RaytracingSimpleLighting::CreateWindowSizeDependentResources()
{
	CreateRaytracingOutputResource(); 
	UpdateCameraMatrices();
}

// Release resources that are dependent on the size of the main window.
void D3D12RaytracingSimpleLighting::ReleaseWindowSizeDependentResources()
{
	m_raytracingOutput.Reset();
}

// Release all resources that depend on the device.
void D3D12RaytracingSimpleLighting::ReleaseDeviceDependentResources()
{
	m_raytracingGlobalRootSignature.Reset();
	m_raytracingLocalRootSignature.Reset();

	m_dxrDevice.Reset();
	m_dxrCommandList.Reset();
	m_dxrStateObject.Reset();

	m_DescriptorHeap.Reset();
	m_DescriptorsAllocated = 0;
	for (auto& model : m_models) {
		model.GetIndexBuffer().resource.Reset();
		model.GetVertexBuffer().resource.Reset();
		model.GetTextureBuffer().resource.Reset();
		model.BottomLevelAccelerationStructure.Reset();
	}
	// m_indexBuffer.resource.Reset();
	// m_vertexBuffer.resource.Reset();
	// m_texture.resource.Reset();
	m_perFrameConstants.Reset();
	m_rayGenShaderTable.Reset();
	m_missShaderTable.Reset();
	m_hitGroupShaderTable.Reset();

	// m_bottomLevelAccelerationStructure.Reset();
	m_topLevelAccelerationStructure.Reset();

}

void D3D12RaytracingSimpleLighting::RecreateD3D()
{
	// Give GPU a chance to finish its execution in progress.
	try
	{
		m_deviceResources->WaitForGpu();
	}
	catch (HrException&)
	{
		// Do nothing, currently attached adapter is unresponsive.
	}
	m_deviceResources->HandleDeviceLost();
}

// Render the scene.
void D3D12RaytracingSimpleLighting::OnRender()
{
	if (!m_deviceResources->IsWindowVisible())
	{
		return;
	}
	// (Your code process and dispatch Win32 messages)
	/*
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGui::ShowDemoWindow(); // Show demo window! :)
	*/


	m_deviceResources->Prepare();
	DoRaytracing();
	CopyRaytracingOutputToBackbuffer();

	m_deviceResources->Present(D3D12_RESOURCE_STATE_PRESENT);
	//RenderImGui();
	// ImGui::Render();

}

void D3D12RaytracingSimpleLighting::RenderImGui() {
	/*
	// Create a window called "My First Tool", with a menu bar.
	ImGui::Begin("My First Tool");
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open..", "Ctrl+O")) { }
			if (ImGui::MenuItem("Save", "Ctrl+S")) { }
			//if (ImGui::MenuItem("Close", "Ctrl+W")) { my_tool_active = false; }
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// Edit a color stored as 4 floats
	// ImGui::ColorEdit4("Color", );

	// Generate samples and plot them
	float samples[100];
	for (int n = 0; n < 100; n++)
		samples[n] = sinf(n * 0.2f + ImGui::GetTime() * 1.5f);
	ImGui::PlotLines("Samples", samples, 100);

	// Display contents in a scrolling region
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Important Stuff");
	ImGui::BeginChild("Scrolling");
	for (int n = 0; n < 50; n++)
		ImGui::Text("%04d: Some text", n);
	ImGui::EndChild();
	ImGui::End();
	*/

}

void D3D12RaytracingSimpleLighting::OnDestroy()
{
	// ImGui_ImplDX12_Shutdown();
	// ImGui_ImplWin32_Shutdown();
	// ImGui::DestroyContext();

	// Let GPU finish before releasing D3D resources.
	m_deviceResources->WaitForGpu();
	OnDeviceLost();
}

void D3D12RaytracingSimpleLighting::OnMouseScroll(short delta, UINT x, UINT y)
{
	//XMVECTOR viewDir = XMVector3Normalize(m_at - m_eye);



	// Scale the view direction by the zoom delta
	XMVECTOR zoomOffset = m_viewDir * delta * 0.01;

	// Move the eye along the view direction
	m_eye = m_eye + zoomOffset;
	
	UpdateCameraMatrices();
}

void D3D12RaytracingSimpleLighting::OnMouseMove(UINT x, UINT y)
{

	if (m_mouseClicked) {
		int deltaX = x - m_oldMouseXPosition;
		int deltaY = y - m_oldMouseYPosition;
		
		m_azAngle -= deltaX * m_cameraRotateSpeed;
		m_altAngle -= deltaY * m_cameraRotateSpeed;
		// Rotate camera around Y axis.
		/*
		XMMATRIX rotateY = XMMatrixRotationY(XMConvertToRadians(-deltaX * 0.05));
		m_viewDir = XMVector3Transform(m_viewDir, rotateY);
		m_up = XMVector3Transform(m_up, rotateY);
		// ROtate camera vertically 
		XMVECTOR axis = XMVector3Cross(m_up, m_viewDir);
		XMMATRIX rotateX = XMMatrixRotationAxis(axis, XMConvertToRadians(deltaY * 0.05));
		m_eye = XMVector3Transform(m_eye, rotateX);
		m_up = XMVector3Transform(m_up, rotateX);
		*/

		
		UpdateCameraMatrices();
	}
	m_oldMouseXPosition = x;
	m_oldMouseYPosition = y;

}

void D3D12RaytracingSimpleLighting::OnKeyDown(UINT8 key) {
	if (key == static_cast<UINT8>('P')) {
		m_animationPaused = !m_animationPaused;
	}
	if (key == static_cast<UINT8>('W')) {
		m_keyWPressed = true;
	}
	if (key == static_cast<UINT8>('S')) {
		m_keySPressed = true;
	}
	if (key == static_cast<UINT8>('A')) {
		m_keyAPressed = true;
	}
	if (key == static_cast<UINT8>('D')) {
		m_keyDPressed = true;
	}

}
void D3D12RaytracingSimpleLighting::OnKeyUp(UINT8 key) {
	if (key == static_cast<UINT8>('W')) {
		m_keyWPressed = false;
	}
	if (key == static_cast<UINT8>('S')) {
		m_keySPressed = false;
	}
	if (key == static_cast<UINT8>('A')) {
		m_keyAPressed = false;
	}
	if (key == static_cast<UINT8>('D')) {
		m_keyDPressed = false;
	}
}


void D3D12RaytracingSimpleLighting::OnLeftButtonDown(UINT x, UINT y)
{
	m_mouseClicked = true;
}

void D3D12RaytracingSimpleLighting::OnLeftButtonUp(UINT x, UINT y)
{
	m_mouseClicked = false;
}

// Release all device dependent resouces when a device is lost.
void D3D12RaytracingSimpleLighting::OnDeviceLost()
{
	ReleaseWindowSizeDependentResources();
	ReleaseDeviceDependentResources();
}

// Create all device dependent resources when a device is restored.
void D3D12RaytracingSimpleLighting::OnDeviceRestored()
{
	CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}

// Compute the average frames per second and million rays per second.
void D3D12RaytracingSimpleLighting::CalculateFrameStats()
{
	static int frameCnt = 0;
	static double elapsedTime = 0.0f;
	double totalTime = m_timer.GetTotalSeconds();
	frameCnt++;

	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f)
	{
		float diff = static_cast<float>(totalTime - elapsedTime);
		float fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

		frameCnt = 0;
		elapsedTime = totalTime;

		float MRaysPerSecond = (m_width * m_height * fps) / static_cast<float>(1e6);

		wstringstream windowText;
		windowText << setprecision(2) << fixed
			<< L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
			<< L"    GPU[" << m_deviceResources->GetAdapterID() << L"]: " << m_deviceResources->GetAdapterDescription();
		SetCustomWindowText(windowText.str().c_str());
	}
}

// Handle OnSizeChanged message event.
void D3D12RaytracingSimpleLighting::OnSizeChanged(UINT width, UINT height, bool minimized)
{
	if (!m_deviceResources->WindowSizeChanged(width, height, minimized))
	{
		return;
	}

	UpdateForSizeChange(width, height);

	ReleaseWindowSizeDependentResources();
	CreateWindowSizeDependentResources();
}

// Allocate a descriptor and return its index. 
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT D3D12RaytracingSimpleLighting::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
	auto descriptorHeapCpuBase = m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	if (descriptorIndexToUse >= m_DescriptorHeap->GetDesc().NumDescriptors)
	{
		descriptorIndexToUse = m_DescriptorsAllocated++;
	}
	*cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, m_DescriptorIncrementSize);
	return descriptorIndexToUse;
}

// Create SRV for a buffer.
UINT D3D12RaytracingSimpleLighting::CreateBufferSRV(Model::D3DBuffer* buffer, UINT numElements, UINT elementSize, UINT descriptorIndex)
{
	auto device = m_deviceResources->GetD3DDevice();

	// SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements = numElements;
	if (elementSize == 0)
	{
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		srvDesc.Buffer.StructureByteStride = 0;
	}
	else
	{
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.StructureByteStride = elementSize;
	}
	descriptorIndex = AllocateDescriptor(&buffer->cpuDescriptorHandle, descriptorIndex);
	device->CreateShaderResourceView(buffer->resource.Get(), &srvDesc, buffer->cpuDescriptorHandle);
	buffer->gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorIncrementSize);
	return descriptorIndex;
}


void D3D12RaytracingSimpleLighting::CreateTextureResource(ImageLoader::ImageData& texture, Model::D3DBuffer* texBuffer, UINT descriptorIndex)
{

	uint32_t textureStride = texture.width * ((texture.bpp + 7) / 8);
	uint32_t textureSize = textureStride * texture.height;

	auto device = m_deviceResources->GetD3DDevice();

	// Resource heap, where the texture lives, but just for 1 tex because we use this to create Committed resource
	const D3D12_HEAP_PROPERTIES texHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	// Resource Descriptor for the texture
	D3D12_RESOURCE_DESC texSRVBufferDesc{};
	texSRVBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texSRVBufferDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	texSRVBufferDesc.Width = texture.width;
	texSRVBufferDesc.Height = texture.height;
	texSRVBufferDesc.DepthOrArraySize = 1;
	texSRVBufferDesc.MipLevels = 1;
	texSRVBufferDesc.Format = texture.giPixelFormat;
	texSRVBufferDesc.SampleDesc.Count = 1;
	texSRVBufferDesc.SampleDesc.Quality = 0;
	texSRVBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texSRVBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

	// Create the Resource(texture) on the Resource heap
	ThrowIfFailed(device->CreateCommittedResource(
		&texHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texSRVBufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&texBuffer->resource)));

	// Create SRV Descriptor for the texture
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = texture.giPixelFormat;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.PlaneSlice = 0;
	srv.Texture2D.ResourceMinLODClamp = 0.0f;

	descriptorIndex = AllocateDescriptor(&texBuffer->cpuDescriptorHandle, descriptorIndex);

	device->CreateShaderResourceView(texBuffer->resource.Get(), &srv, texBuffer->cpuDescriptorHandle);
	texBuffer->gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, m_DescriptorIncrementSize);

	// Create a upload Buffer to copy texture from CPU to GPU
	D3D12_HEAP_PROPERTIES hpUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC rdUpload{};
	rdUpload.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rdUpload.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	rdUpload.Width = textureSize;
	rdUpload.Height = 1;
	rdUpload.DepthOrArraySize = 1;
	rdUpload.MipLevels = 1;
	rdUpload.Format = DXGI_FORMAT_UNKNOWN;
	rdUpload.SampleDesc.Count = 1;
	rdUpload.SampleDesc.Quality = 0;
	rdUpload.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rdUpload.Flags = D3D12_RESOURCE_FLAG_NONE;

	ComPtr<ID3D12Resource> uploadBuffer;
	device->CreateCommittedResource(&hpUpload, D3D12_HEAP_FLAG_NONE, &rdUpload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

	// Copy the data to the upload buffer
	char* uploadBufferAddress;
	D3D12_RANGE uploadRange;
	uploadRange.Begin = 0;
	uploadRange.End = textureSize;

	uploadBuffer->Map(0, &uploadRange, (void**)&uploadBufferAddress);
	memcpy(&uploadBufferAddress[0], texture.data.data(), textureSize);
	uploadBuffer->Unmap(0, &uploadRange);

	// Get device to copy from upload buffer to the texture resource 

	auto commandList = m_deviceResources->GetCommandList();
	auto commandAllocator = m_deviceResources->GetCommandAllocator();
	commandAllocator->Reset();
	commandList->Reset(commandAllocator, nullptr);

	D3D12_TEXTURE_COPY_LOCATION txtcSrc, txtcDst;
	D3D12_BOX textureSizeAsBox;

	textureSizeAsBox.left = textureSizeAsBox.top = textureSizeAsBox.front = 0;
	textureSizeAsBox.right = texture.width;
	textureSizeAsBox.bottom = texture.height;
	textureSizeAsBox.back = 1;

	txtcSrc.pResource = uploadBuffer.Get();
	txtcSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	txtcSrc.PlacedFootprint.Offset = 0;
	txtcSrc.PlacedFootprint.Footprint.Width = texture.width;
	txtcSrc.PlacedFootprint.Footprint.Height = texture.height;
	txtcSrc.PlacedFootprint.Footprint.Depth = 1;
	txtcSrc.PlacedFootprint.Footprint.RowPitch = textureStride;
	txtcSrc.PlacedFootprint.Footprint.Format = texture.giPixelFormat;

	txtcDst.pResource = texBuffer->resource.Get();
	txtcDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	txtcDst.SubresourceIndex = 0;

	commandList->CopyTextureRegion(&txtcDst, 0, 0, 0, &txtcSrc, &textureSizeAsBox);

	// Kick off acceleration structure construction.
	m_deviceResources->ExecuteCommandList();

	// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
	m_deviceResources->WaitForGpu();
}