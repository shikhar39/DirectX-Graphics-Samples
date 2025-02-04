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

#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "RaytracingHlslCompat.h"
#include "ImageLoader.h"
#include "Model.h"

namespace GlobalRootSignatureParams {
    enum Value {
        OutputViewSlot = 0,
        AccelerationStructureSlot,
        SceneConstantSlot,
        IndexBuffersSlot,
        VertexBuffersSlot,
        TextureSlot,
        Count 
    };
}

namespace LocalRootSignatureParams {
    enum Value {
        CubeConstantSlot = 0,
        Count 
    };
}

class D3D12RaytracingSimpleLighting : public DXSample
{
public:
    D3D12RaytracingSimpleLighting(UINT width, UINT height, std::wstring name);

    // IDeviceNotify
    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    // Messages
    virtual void OnInit();
    virtual void OnUpdate();
    virtual void OnRender();
    virtual void OnSizeChanged(UINT width, UINT height, bool minimized);
    virtual void OnDestroy();
    virtual IDXGISwapChain* GetSwapchain() { return m_deviceResources->GetSwapChain(); }

    // Input Messages
    virtual void OnMouseScroll(short delta, UINT x, UINT y);
    virtual void OnMouseMove(UINT x, UINT y);
    virtual void OnLeftButtonDown(UINT x, UINT y);
    virtual void OnLeftButtonUp(UINT x, UINT y);
    virtual void OnKeyDown(UINT8 key);
    virtual void OnKeyUp(UINT8 key);

    
private:
    static const UINT FrameCount = 3;

    // We'll allocate space for several of these and they will need to be padded for alignment.
    static_assert(sizeof(SceneConstantBuffer) < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "Checking the size here.");

    union AlignedSceneConstantBuffer
    {
        SceneConstantBuffer constants;
        uint8_t alignmentPadding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
    };
    AlignedSceneConstantBuffer*  m_mappedConstantData;
    ComPtr<ID3D12Resource>       m_perFrameConstants;

    // DirectX Raytracing (DXR) attributes
    ComPtr<ID3D12Device5> m_dxrDevice;
    ComPtr<ID3D12GraphicsCommandList5> m_dxrCommandList;
    ComPtr<ID3D12StateObject> m_dxrStateObject;

    // Root signatures
    ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
    ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;

    enum BufferType {
        IndexBufferSRV = 0,
        VertexBufferSRV,
        TextureSRV
    };


    // Descriptors
    ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
    UINT m_DescriptorsAllocated;
    UINT m_DescriptorIncrementSize;
    

    // Raytracing scene
    SceneConstantBuffer m_sceneCB[FrameCount];
    CubeConstantBuffer m_cubeCB;

    // Geometry
    // D3DBuffer m_indexBuffer;
    // D3DBuffer m_vertexBuffer;

    std::vector<Model> m_models;
    UINT16 m_numObjects = 1;


    // Acceleration structure
    //  ComPtr<ID3D12Resource> m_bottomLevelAccelerationStructure;
    ComPtr<ID3D12Resource> m_topLevelAccelerationStructure;

    // Acceleration Structure builder
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC m_topLevelBuildDesc;

    // Raytracing output
    ComPtr<ID3D12Resource> m_raytracingOutput;
    D3D12_GPU_DESCRIPTOR_HANDLE m_raytracingOutputResourceUAVGpuDescriptor;
    UINT m_raytracingOutputResourceUAVDescriptorHeapIndex;

    // Texture
    // D3DBuffer m_texture;

    // Shader tables
    static const wchar_t* c_hitGroupName;
    static const wchar_t* c_raygenShaderName;
    static const wchar_t* c_closestHitShaderName;
    static const wchar_t* c_missShaderName;

    static const wchar_t* c_shadowHitGroupName;
    static const wchar_t* c_shadowMissShaderName;
    static const wchar_t* c_shadowAnyHitShaderName;
    
    ComPtr<ID3D12Resource> m_missShaderTable;
    ComPtr<ID3D12Resource> m_hitGroupShaderTable;
    ComPtr<ID3D12Resource> m_rayGenShaderTable;
    
    // Application state
    StepTimer m_timer;
    bool m_animationPaused = true;
    bool m_updateAccelerationStructure = false; 
    UINT m_oldMouseXPosition;
    UINT m_oldMouseYPosition;
    float m_objDistance = 5.0f;
    float m_objDistDelta = 0.2f;

    // Input state Variables
    bool m_mouseClicked;
    bool m_keyWPressed;
    bool m_keySPressed;
    bool m_keyAPressed;
    bool m_keyDPressed; // :(

    // Camera Properties
    float m_cameraMoveSpeed = 0.5f;
    float m_cameraRotateSpeed = 0.001;
    float m_altAngle = 0;
    float m_azAngle = 0;

    XMVECTOR m_viewDir;
    XMVECTOR m_eye;
    XMVECTOR m_up;

    void UpdateCameraMatrices();
    void InitializeScene();
    void RecreateD3D();
    void DoRaytracing();
    void CreateConstantBuffers();
    void CreateTextureResource(ImageLoader::ImageData &texture, Model::D3DBuffer* texBuffer, UINT descriptorIndex);
    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();
    void ReleaseDeviceDependentResources();
    void ReleaseWindowSizeDependentResources();
    void CreateRaytracingInterfaces();
    void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig);
    void CreateRootSignatures();
    void CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline);
    void CreateRaytracingPipelineStateObject();
    void CreateDescriptorHeap();
    void CreateRaytracingOutputResource();
    void BuildGeometry();
    void BuildAccelerationStructures();
    void UpdateAccelerationStructure();
    void InitImGui();
    void RenderImGui();
    void BuildShaderTables();
    void UpdateForSizeChange(UINT clientWidth, UINT clientHeight);
    void CopyRaytracingOutputToBackbuffer();
    void CalculateFrameStats();
    UINT AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse = UINT_MAX);
    UINT CreateBufferSRV(Model::D3DBuffer* buffer, UINT numElements, UINT elementSize, UINT descriptorIndex);



};
