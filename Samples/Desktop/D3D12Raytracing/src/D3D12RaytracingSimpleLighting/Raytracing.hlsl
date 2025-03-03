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

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"
#include "hlslUtils.hlsli"


#define nShadowSamples 128 
RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ByteAddressBuffer Indices[] : register(t1, space0);
StructuredBuffer<Vertex> Vertices[] : register(t0, space1);
// ByteAddressBuffer Vertices[] : register(t2, space0);

Texture2D<float4> textures[] : register(t0, space2);
SamplerState textureSampler : register(s0);

ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
ConstantBuffer<CubeConstantBuffer> g_cubeCB : register(b1);


// Load three 16 bit indices from a byte addressed buffer.
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;    
    const uint2 four16BitIndices = Indices[InstanceID()].Load2(dwordAlignedOffset);
 
    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
};

struct ShadowRayPayload
{
    float shadowFactor;
};
struct Triangle
{
    float4 v0;
    float4 v1;
    float4 v2;
};

// M�ller�Trumbore ray-triangle intersection test
bool IntersectRayTriangle(RayDesc ray, Triangle tri, out float t)
{
    const float EPSILON = 1e-5f;

    float3 edge1 = tri.v1 - tri.v0;
    float3 edge2 = tri.v2 - tri.v0;

    float3 h = cross(ray.Direction, edge2);
    float a = dot(edge1, h);

    if (abs(a) < EPSILON)
        return false; // Ray is parallel to the triangle

    float f = 1.0f / a;
    float3 s = ray.Origin - tri.v0.xyz;
    float u = f * dot(s, h);

    if (u < 0.0f || u > 1.0f)
        return false;

    float3 q = cross(s, edge1);
    float v = f * dot(ray.Direction, q);

    if (v < 0.0f || (u + v) > 1.0f)
        return false;

    // Compute intersection distance
    t = f * dot(edge2, q);
    return t > EPSILON;
}


// Retrieve hit world position.
float3 HitWorldPosition()
{
    float3 objectHitPosition = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    return objectHitPosition;
    // return mul(float4(objectHitPosition, 1.0f), ObjectToWorld4x3()).xyz;

}

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's barycentrics.
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);
}

// Diffuse lighting calculation.
float4 CalculateDiffuseLighting(float3 hitPosition, float3 normal)
{
    float3 pixelToLight = normalize(g_sceneCB.lightPosition.xyz - hitPosition);

    // Diffuse contribution.
    float fNDotL = max(0.0f, dot(pixelToLight, normal));

    return g_cubeCB.albedo * g_sceneCB.lightDiffuseColor * fNDotL;
}


[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;
    
    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    /*
    */
    Triangle light1;
    light1.v0 = g_sceneCB.v1;
    light1.v1 = g_sceneCB.v2;
    light1.v2 = g_sceneCB.v3;
    
    Triangle light2;
    light2.v0 = g_sceneCB.v1;
    light2.v1 = g_sceneCB.v2;
    light2.v2 = g_sceneCB.v4;
    
    float t;
    
    if (!(IntersectRayTriangle(ray, light1, t) | IntersectRayTriangle(ray, light2, t)))
    {
        TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);
    }
    else
    {
        payload.color = float4(1, 1, 1, 1);

    }

    // Write the raytraced color to the output texture.
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("anyhit")]
void ShadowRayAnyHitShader(inout ShadowRayPayload shpayload, in MyAttributes attr)
{
    shpayload.shadowFactor = 0.0f;
    // AcceptHitAndEndSearch();
}

[shader("miss")]
void ShadowRayMissShader(inout ShadowRayPayload shpayload)
{
    shpayload.shadowFactor = 1.0f/nShadowSamples;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
    uint i = InstanceID();
    // Get the base index of the triangle's first 16 bit index.
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * triangleIndexStride;

    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = Load3x16BitIndices(baseIndex);

    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 vertexNormals[3] = { 
        Vertices[i][indices[0]].normal, 
        Vertices[i][indices[1]].normal,
        Vertices[i][indices[2]].normal 
    };

    // Compute the triangle's normal.
    // This is redundant and done for illustration purposes 
    // as all the per-vertex normals are the same and match triangle's normal in this sample. 
    // float3 triangleNormal = HitAttribute(vertexNormals, attr);
    
    float3 triangleNormal =     Vertices[i][indices[0]].normal +
        attr.barycentrics.x * ( Vertices[i][indices[1]].normal - Vertices[i][indices[0]].normal) +
        attr.barycentrics.y * ( Vertices[i][indices[2]].normal - Vertices[i][indices[0]].normal);
    
    float2 triangleUV =         Vertices[i][indices[0]].uvs +
        attr.barycentrics.x * ( Vertices[i][indices[1]].uvs - Vertices[i][indices[0]].uvs) +
        attr.barycentrics.y * ( Vertices[i][indices[2]].uvs - Vertices[i][indices[0]].uvs);
    
    Triangle light1;
    light1.v0 = g_sceneCB.v1;
    light1.v1 = g_sceneCB.v2;
    light1.v2 = g_sceneCB.v3;
    
    Triangle light2;
    light2.v0 = g_sceneCB.v1;
    light2.v1 = g_sceneCB.v2;
    light2.v2 = g_sceneCB.v4;
    float totalShadowFactor = 0.0f;
    uint seed = initRand(DispatchRaysIndex().x, DispatchRaysIndex().y);
    for (int k = 0; k < nShadowSamples; k++)
    {
        float x1 = nextRand(seed);
        float x2 = nextRand(seed);
        float3 pointOnLight = g_sceneCB.v1 + x1 * (g_sceneCB.v3 - g_sceneCB.v1) + x2 * (g_sceneCB.v4 - g_sceneCB.v1);
        float3 hitPointToLight = normalize(pointOnLight - hitPosition);
        
        RayDesc shadowRay;
        shadowRay.Origin = hitPosition;
        shadowRay.TMin = 0.001;
        shadowRay.TMax = 100.0;
        shadowRay.Direction = hitPointToLight;
        
        ShadowRayPayload shadowPayload = { 0.0f };
        float NDotL = dot(normalize(shadowRay.Direction), triangleNormal);
        if ( NDotL > 0.1f)
        {
            TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 1, 1, 1, shadowRay, shadowPayload);
        
        }
        totalShadowFactor += shadowPayload.shadowFactor;
    }
    
    
    
    
    //float4 diffuseColor = CalculateDiffuseLighting(hitPosition, triangleNormal);
    
    float diffuseFactor = max(0.0, dot(triangleNormal, normalize(g_sceneCB.lightPosition.xyz - hitPosition)));
    
    //float4 texColor = texture.Sample(textureSampler, triangleUV);
    float4 texColor = textures[i].SampleLevel(textureSampler, triangleUV, 0.0f);
    if (dot(g_sceneCB.cameraPosition.xyz - hitPosition, triangleNormal) < 0.0)
    {
        totalShadowFactor = 0.0f;

    }
    float4 color = texColor * (g_sceneCB.lightAmbientColor + totalShadowFactor * diffuseFactor * g_sceneCB.lightDiffuseColor);
    //float4 color = texColor * shadowPayload.shadowFactor;
    
    
    /*
    */

    payload.color = color;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float4 background = float4(0.0f, 0.2f, 0.4f, 1.0f);
    payload.color = background;
}

#endif // RAYTRACING_HLSL