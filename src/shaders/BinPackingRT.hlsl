#include "shared.h"

// =========================================================================
//   Resources
// =========================================================================

// Constant buffer with global data
cbuffer GameDataCB : register(b0)
{
    GameData gData;
}

struct RootConstants
{
    uint32_t ax;
    uint32_t bx;
    uint32_t cx;
    uint32_t dx;
    uint32_t ex;
    uint32_t fx;
};
cbuffer RootConstantsCB : register(b1)
{
    RootConstants gRootConstants;
}

// Output and drawing buffers
RWTexture2D<float4> DrawingBuffer : register(u0);
RWTexture2D<float4> OutputBuffer : register(u1);

// Strings rendering
Texture2D<float4> fontAtlasTexture : register(t0);
Buffer<uint> stringsBuffer : register(t1);

// RT rendering
StructuredBuffer<Material> materialsBuffer : register(t2);
RaytracingAccelerationStructure SceneBVH : register(t3);
Buffer<float3> normalsBuffer : register(t4);
RWTexture2D<float4> rtWindowBuffersRW[RT_WINDOW_BUFFERS_COUNT] : register(u0, space1);

// =========================================================================
//   String Drawing
// =========================================================================

[shader("compute")]
[numthreads(DRAW_STRING_THREADGROUP_SIZE, DRAW_STRING_THREADGROUP_SIZE, 1)]
void DrawString(
    int3 groupID : SV_GroupID,
    int3 groupThreadID : SV_GroupThreadID,
    int3 LaunchIndex : SV_DispatchThreadID)
{
	// Load data and sanity checks
    const int stringIndex = groupID.z;
    if (stringIndex > gData.stringsCount) return;
    
    const StringData stringData = gData.strings[NonUniformResourceIndex(stringIndex)];
    if (LaunchIndex.y >= gData.characterSize.y) return;
    
    const uint characterIdx = LaunchIndex.x / gData.characterSize.x;
    if (characterIdx >= stringData.stringLength) return;

	// Decode character from buffer
    const uint packedCharacterAtlasPos = stringsBuffer[NonUniformResourceIndex(stringData.stringStartOffset + characterIdx)];
    const uint2 characterAtlasPos = uint2(packedCharacterAtlasPos & 0xFF, (packedCharacterAtlasPos >> 8) & 0x7F);

	// Figure out this texel
    const uint2 characterTopLeftTexel = characterAtlasPos * uint2(gData.characterSize.x, gData.characterSize.y);
    const uint2 characterTexel = characterTopLeftTexel + float2(LaunchIndex.x - (LaunchIndex.x / gData.characterSize.x) * gData.characterSize.x, LaunchIndex.y);

	// Decode highlighted chars
    const float4 color = (packedCharacterAtlasPos >> 15) ? stringData.highlightColor : stringData.fontColor;

	// Read from font atlas and apply string color
    float4 result = float4(fontAtlasTexture[NonUniformResourceIndex(characterTexel)].rrrr) * color;

	// Apply backround assuming premultiplied alpha
    result = stringData.backgroundColor * (1 - result.a) + result;

	// Blend into back buffer (assume premultiplied alpha again)
    const float4 currentPixel = DrawingBuffer[NonUniformResourceIndex(LaunchIndex.xy + stringData.screenPosition)];
    DrawingBuffer[NonUniformResourceIndex(LaunchIndex.xy + stringData.screenPosition)] = currentPixel * (1 - result.a) + result;
}

// =========================================================================
//   Post process
// =========================================================================

float vignette(float2 uvs)
{
    const float vignetteFalloff = 4.0f;
    const float vignetteMax = 1.0f;
    const float vignetteMin = 0.0f;
    const float vignetteMaxDistance = 0.85f;
    const float vignetteMinDistance = 0.0f;
    
    float2 dir = float2(0.5f, 0.5f) - uvs;

    float u = saturate((length(dir) - vignetteMinDistance) / (vignetteMaxDistance - vignetteMinDistance));
    u = saturate(pow(u, vignetteFalloff));

    return lerp(vignetteMax, vignetteMin, u);
}

float luma(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float3 setSaturation(float3 color, float saturation)
{
    return lerp(luma(color), color, saturation);
}

float3 colorGrading(float3 color)
{
    const bool invertColors = gData.invertColors;
    const float exposureAdjustment = 1.5f;
    const float tonemappingContrast = 0.85f;
    const float tonemappingSaturation = 1.0f;
    const float3 colorBalance = gData.colorBalance;
    const float tonemappingGamma = 0.8f;
    
    if (invertColors)
        color = 1 - color;

    color *= exposureAdjustment;

	// Adjust contrast & saturation
    color = saturate((tonemappingContrast * (color - 0.18f)) + 0.18f); // 0.18 = Mid level grey
    color = setSaturation(color, tonemappingSaturation);

	// Apply color temperature & tint
    color *= colorBalance;

	// Apply gamma 
    color = pow(color, tonemappingGamma);

    return color;
}

float bar(float x, float scale, float sharpness)
{
    return sharpness * cos(x * scale);
}

float3 crtGrid(float3 color, float2 uvs, uint2 textureSize)
{
    const float hBarMin = 0.95f;
    const float vBarMin = 0.85f;
    const float hBarSharpness = 1.0f;
    const float vBarSharpness = 1.5f;
    const float crtExposureCompensation = 1.15f;
    const float hBarScale = HALF_PI * gData.outputHeight;
    const float vBarScale = PI * gData.outputWidth;
    
    float horizontalBar = lerp(hBarMin, 1, bar(uvs.y, hBarScale, hBarSharpness));
    float verticalBar = lerp(vBarMin, 1, saturate(bar(uvs.x, vBarScale, vBarSharpness)));
    return color * (horizontalBar * verticalBar * crtExposureCompensation);
}

[shader("compute")]
[numthreads(POST_PROCESS_THREADGROUP_SIZE, POST_PROCESS_THREADGROUP_SIZE, 1)]
void PostProcess(
    int2 groupID : SV_GroupID,
    int2 groupThreadID : SV_GroupThreadID,
    int2 LaunchIndex : SV_DispatchThreadID)
{
    if (LaunchIndex.x >= gData.outputWidth || LaunchIndex.y >= gData.outputHeight)
        return;
    
    const float2 textureSizeRcp = float2(1.0f / float(gData.outputWidth), 1.0f / float(gData.outputHeight));
    const float2 uvs = float2(LaunchIndex) * textureSizeRcp;

    OutputBuffer[LaunchIndex] = float4(vignette(uvs) * crtGrid(colorGrading(DrawingBuffer[LaunchIndex].rgb), uvs, uint2(gData.outputWidth, gData.outputHeight)), 1);
}

// =========================================================================
//   Buffer Clearing
// =========================================================================

[shader("compute")]
[numthreads(CLEAR_UAV_THREADGROUP_SIZE, CLEAR_UAV_THREADGROUP_SIZE, 1)]
void ClearUAV(
    int2 groupID : SV_GroupID,
    int2 groupThreadID : SV_GroupThreadID,
    int2 LaunchIndex : SV_DispatchThreadID)
{
    if (LaunchIndex.x >= gData.outputWidth || LaunchIndex.y >= gData.outputHeight)
        return;

    const float4 clearColor = float4(0, 0, 0, 0);
    DrawingBuffer[LaunchIndex] = clearColor;
}

// =========================================================================
//   Denoiser
// =========================================================================

[shader("compute")]
[numthreads(DENOISER_THREADGROUP_SIZE, DENOISER_THREADGROUP_SIZE, 1)]
void Denoise(
    int2 groupID : SV_GroupID,
    int2 groupThreadID : SV_GroupThreadID,
    int2 LaunchIndex : SV_DispatchThreadID)
{

}

// =========================================================================
//   Path tracer
// =========================================================================

struct HitInfo
{
    float3 throughput;
    uint bounce;
    float3 result;
    float hitT;
    uint2 pixelIndex;
    uint offset;
    uint pad;
    
    bool hasHit()
    {
        return hitT > 0.0f;
    }
};

struct Attributes
{
    float2 uv;
};

float3 offset_ray(const float3 p, const float3 n)
{
    static const float origin = 1.0f / 32.0f;
    static const float float_scale = 1.0f / 65536.0f;
    static const float int_scale = 256.0f;

    int3 of_i = int3(int_scale * n.x, int_scale * n.y, int_scale * n.z);

    float3 p_i = float3(
		asfloat(asint(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
		asfloat(asint(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
		asfloat(asint(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return float3(abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
		abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
		abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

float3x3 buildTBN(float3 normal)
{
	// TODO: Maybe try approach from here (Building an Orthonormal Basis, Revisited): 
	// https://graphics.pixar.com/library/OrthonormalB/paper.pdf

	// Pick random vector for generating orthonormal basis
    static const float3 rvec1 = float3(0.847100675f, 0.207911700f, 0.489073813f);
    static const float3 rvec2 = float3(-0.639436305f, -0.390731126f, 0.662155867f);
    float3 rvec;

    if (dot(rvec1, normal) > 0.95f)
        rvec = rvec2;
    else
        rvec = rvec1;

	// Construct TBN matrix to orient sampling hemisphere along the surface normal
    float3 b1 = normalize(rvec - normal * dot(rvec, normal));
    float3 b2 = cross(normal, b1);
    float3x3 tbn = float3x3(b1, b2, normal);

    return tbn;
}

struct SurfaceData
{
    Material material;
    float3 position;
    float3 normal;
};

float3 getSpectralSample(float u)
{
    return saturate(float3(abs(u * 6.0f - 3.0f) - 1.0f, 2.0f - abs(u * 6.0f - 2.0f), 2.0f - abs(u * 6.0f - 4.0f)));
}

SurfaceData loadSurfaceData(float2 attribUVs, uint instanceID, uint primitiveIndex, float3 rayOrigin, float3 rayDirection, float hitT)
{
    SurfaceData result;

    result.material = materialsBuffer[instanceID];
    result.position = rayOrigin + rayDirection * hitT;
    result.normal = normalsBuffer[primitiveIndex];

	// Calculate UVs
    float3 barycentrics = float3((1.0f - attribUVs.x - attribUVs.y), attribUVs.x, attribUVs.y);
    float2 uv1;
    float2 uv2;
    float2 uv3;

    if (PrimitiveIndex() % 2)
    {
        uv1 = float2(1, 0);
        uv2 = float2(0, 1);
        uv3 = float2(1, 1);
    }
    else
    {
        uv1 = float2(0, 0);
        uv2 = float2(1, 0);
        uv3 = float2(1, 1);
    }

    float2 pseudoUvs = uv1 * barycentrics.x + uv2 * barycentrics.y + uv3 * barycentrics.z;


    if (InstanceID() == 0)
    {
        result.normal.xy = -result.normal.xy; //< Handle arena model as a special case (due to negative scaling, normals need adjustment)

        bool isTopWall = (primitiveIndex == 0 || primitiveIndex == 1);
        bool isRightWall = (primitiveIndex == 2 || primitiveIndex == 3);
        bool isBottomWall = (primitiveIndex == 4 || primitiveIndex == 5);
        bool isLeftWall = (primitiveIndex == 6 || primitiveIndex == 7);

		// Insert some light to the arena walls
		//if (isTopWall && pseudoUvs.y > 0.48 && pseudoUvs.y < 0.52) result.material.emissive = -float3(0.5, 1, 1);

        if (isRightWall && pseudoUvs.y > 0.1 && pseudoUvs.y < 0.15)
            result.material.emissive = float3(1, 0.5, 0.5);
        if (isLeftWall && 1 - pseudoUvs.y > 0.1 && 1 - pseudoUvs.y < 0.15)
            result.material.emissive = float3(1, 0.5, 0.5);

        if (isRightWall && pseudoUvs.y > 0.25 && pseudoUvs.y < 0.3)
            result.material.emissive = float3(1, 1, 0.5) * 0.8;
        if (isLeftWall && 1 - pseudoUvs.y > 0.25 && 1 - pseudoUvs.y < 0.3)
            result.material.emissive = float3(1, 1, 0.5) * 0.8;

        if (isRightWall && pseudoUvs.y > 0.4 && pseudoUvs.y < 0.5)
            result.material.emissive = float3(0.5, 0.5, 1);
        if (isLeftWall && 1 - pseudoUvs.y > 0.4 && 1 - pseudoUvs.y < 0.5)
            result.material.emissive = float3(0.5, 0.5, 1);

        if (isBottomWall && pseudoUvs.y > 0.1 && pseudoUvs.y < 0.9 && pseudoUvs.x > 0.4 && pseudoUvs.x < 0.6)
            result.material.emissive = getSpectralSample(pseudoUvs.y);

        if (any(result.material.emissive) > 0)
            result.material.id += 0x100;
    }
    else
    {

		// Create borders for blocks
	
        const float border = 0.05f;
        if (pseudoUvs.x < border || pseudoUvs.y < border || 1 - pseudoUvs.x < border || 1 - pseudoUvs.y < border)
        {
            result.material.metalness = 0.9f;
            result.material.roughness = 0.9f;
            result.material.albedo = 0.2f;
            result.material.id = 0xFF;
        }
    }

    return result;
}

SurfaceData loadSurfaceData(float2 attribUVs)
{
    return loadSurfaceData(attribUVs, InstanceID(), PrimitiveIndex(), WorldRayOrigin(), WorldRayDirection(), RayTCurrent());
}

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    SurfaceData surfaceData = loadSurfaceData(attrib.uv, InstanceID(), PrimitiveIndex(), WorldRayOrigin(), WorldRayDirection(), RayTCurrent());
    payload.hitT = RayTCurrent();
    payload.bounce++;

    payload.result += surfaceData.material.albedo;

}

[shader("miss")]
void Miss(inout HitInfo payload)
{
    if (payload.bounce == 0)
    {
        payload.throughput = 0;
    }
    else
    {
        payload.result += payload.throughput * gData.ambientColor;
    }
    payload.hitT = -1.0f;
}


[shader("raygeneration")]
void RayGen()
{
    uint2 LaunchIndex = DispatchRaysIndex().xy;
    uint2 resolution = uint2(gRootConstants.ax, gRootConstants.bx);
    if (LaunchIndex.x >= resolution.x || LaunchIndex.y >= resolution.y)
        return;

    float2 resolutionRcp = float2(asfloat(gRootConstants.cx), asfloat(gRootConstants.dx));
    float2 d = (((LaunchIndex.xy + 0.5f) * resolutionRcp) * 2.f - 1.f);
    float aspectRatio = (float(resolution.x) / float(resolution.y));
	
    aspectRatio *= gData.horizontalStretch; //< Squeeze horizontally

	// Setup the ray
    RayDesc ray;
    ray.Origin = gData.cameraPosition;
    ray.Direction = normalize((d.x * gData.view[0].xyz * gData.tanHalfFovY * aspectRatio) - (d.y * gData.view[1].xyz * gData.tanHalfFovY) + gData.view[2].xyz);
    ray.TMin = PRIMARY_TMIN;
    ray.TMax = PRIMARY_TMAX;

    HitInfo payload = (HitInfo) 0;

    unsigned int historyLength = asuint(rtWindowBuffersRW[HISTORY_LENGTH_INDEX][LaunchIndex].r);
    bool isDisocclusion = (rtWindowBuffersRW[DISOCCLUSIONS_INDEX][LaunchIndex].r != 0);
    int rayCount = (isDisocclusion || historyLength < 42) ? 32 : 1;
	//int rayCount = asfloat(gRootConstants.fx) == 1 ? 32 : 1;
    float3 totalResult = 0;

    for (int i = 0; i < rayCount; i++)
    {
		// Trace the ray
        payload.bounce = 0;
        payload.throughput = 1.0f;
        payload.result = 0.0f;
        payload.pixelIndex = LaunchIndex;
        payload.hitT = -1.0f;
		//payload.offset = gData.frameNumber * rayCount + i;
        payload.offset = gRootConstants.ex * rayCount + i;

        TraceRay(
			SceneBVH,
			RAY_FLAG_NONE,
			0xFF,
			0,
			0,
			0,
			ray,
			payload);

        totalResult += payload.result;
    }

    totalResult /= float(rayCount);

	// Temporal accumulation
    RWTexture2D<float4> accumulationBuffer = rtWindowBuffersRW[RT_RAW_OUTPUT_INDEX];
    if (gData.isFirstFrame || isDisocclusion)
        historyLength = 0;

    float temporalAlpha = historyLength == 0 ? 1 : max(1.0f / 512.0f, 1.0f / float(historyLength));
	//float temporalAlpha = asfloat(gRootConstants.fx);

    historyLength += rayCount;
    rtWindowBuffersRW[HISTORY_LENGTH_INDEX][LaunchIndex] = asfloat(historyLength);

    float3 currentResult = totalResult;
    float3 previousResult = accumulationBuffer[LaunchIndex].rgb;
    float3 accumulatedResult = lerp(previousResult, currentResult, temporalAlpha);
    accumulationBuffer[LaunchIndex] = float4(accumulatedResult, (payload.hitT < 0 && payload.bounce == 0) ? 0 : 1);
    
    OutputBuffer[NonUniformResourceIndex(gData.drawingPosition + LaunchIndex)] = accumulationBuffer[LaunchIndex];
}
