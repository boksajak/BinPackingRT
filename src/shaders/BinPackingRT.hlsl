#include "shared.h"
#include "brdf.h"

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


// -------------------------------------------------------------------------
//    RNG
// -------------------------------------------------------------------------

// 32-bit Xorshift random number generator
inline uint xorshift32(inout uint rngState)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

// PCG Hash Function
// Source: https://jcgt.org/published/0009/03/02/
uint pcgHash(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Converts unsigned integer into float int range <0; 1) by using 23 most significant bits for mantissa
// Explanation: https://vectrx.substack.com/p/lcg-xs-fast-gpu-rng
float uintToFloat(const uint x)
{
    return asfloat(0x3f800000 | (x >> 9)) - 1.0f;
}

// Initialize RNG
uint initRNG(const uint linearIndex, const uint frameNumber)
{
    return pcgHash(linearIndex ^ pcgHash(frameNumber));
}

// Initialize RNG for given pixel and frame
uint initRNG(const uint2 pixelCoords, const uint2 resolution, const uint frameNumber)
{
    return initRNG(dot(pixelCoords, uint2(1, resolution.x)), frameNumber);
}

// Generate random float in <0; 1) range
float rand(inout uint rngState)
{
    return uintToFloat(xorshift32(rngState));
}

// Generate a random float in the range <-x; x)
float randInRange(inout uint rng, float x)
{
    return (rand(rng) * 2.0f - 1.0f) * x;
}

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

float2 octWrap(float2 v)
{
    return float2((1.0f - abs(v.y)) * (v.x >= 0.0f ? 1.0f : -1.0f), (1.0f - abs(v.x)) * (v.y >= 0.0f ? 1.0f : -1.0f));
}

float2 encodeNormalOctahedron(float3 n)
{
    float2 p = float2(n.x, n.y) * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));
    p = (n.z < 0.0f) ? octWrap(p) : p;
    return p;
}

float3 decodeNormalOctahedron(float2 p)
{
    float3 n = float3(p.x, p.y, 1.0f - abs(p.x) - abs(p.y));
    float2 tmp = (n.z < 0.0f) ? octWrap(float2(n.x, n.y)) : float2(n.x, n.y);
    n.x = tmp.x;
    n.y = tmp.y;
    return normalize(n);
}

inline bool isValidTap(float4 centerGBuffer, float4 tapGBuffer)
{
    float3 centerNormal = decodeNormalOctahedron(centerGBuffer.xy);
    float3 tapNormal = decodeNormalOctahedron(tapGBuffer.xy);
    float centerDepth = centerGBuffer.w;
    float tapDepth = tapGBuffer.w;
    uint centerMaterial = asuint(centerGBuffer.z);
    uint tapMaterial = asuint(tapGBuffer.z);

	// Adjust depth difference epsilon based on view space normal
    float dotViewNormal = abs(centerNormal.z);

    const float depthRelativeDifferenceEpsilonMin = 0.03f;
    const float depthRelativeDifferenceEpsilonMax = 0.2;
    float depthRelativeDifferenceEpsilon = lerp(depthRelativeDifferenceEpsilonMax, depthRelativeDifferenceEpsilonMin, dotViewNormal);

	// Check materials
    if (centerMaterial != tapMaterial)
        return false;

	// Check depth
    if (abs(1.0f - (tapDepth / centerDepth)) > depthRelativeDifferenceEpsilon)
        return false;

	// Check normals
    const float dotNormalsEpsilon = 0.9f;
    if (dot(tapNormal, centerNormal) < dotNormalsEpsilon)
        return false;

    return true;
}

float3 blurPassTent(int2 pixelPos, RWTexture2D<float4> inputBuffer, int2 direction)
{
	//return inputBuffer[pixelPos].rgb;
    int2 offset;
    float3 acc = 0.0f;
    float weight = 0.0f;

    float4 centerGBuffer = rtWindowBuffersRW[NORMAL_MATERIAL_DEPTH_INDEX][pixelPos];

	[unroll]
    for (int i = -11; i <= 11; ++i)
    {
        offset = pixelPos + direction * i;

        float3 tapColor = inputBuffer[offset].rgb;
        float4 tapGBuffer = rtWindowBuffersRW[NORMAL_MATERIAL_DEPTH_INDEX][offset];

        if (isValidTap(centerGBuffer, tapGBuffer))
        {
            float tapWeight = 12 - abs(i);
            acc += tapColor * tapWeight;
            weight += tapWeight;
        }
    }

    if (weight == 0.0f)
        return 0.0f;

    return acc / weight;
}

[shader("compute")]
[numthreads(DENOISER_THREADGROUP_SIZE, DENOISER_THREADGROUP_SIZE, 1)]
void Denoise(
    int2 groupID : SV_GroupID,
    int2 groupThreadID : SV_GroupThreadID,
    int2 threadIdx : SV_DispatchThreadID)
{
    uint2 textureSize = uint2(gRootConstants.ax, gRootConstants.bx);
    if (threadIdx.x >= textureSize.x || threadIdx.y >= textureSize.y)
        return;

    bool isHorizontalPass = gRootConstants.cx;
    bool isEvenPass = gRootConstants.dx;
    bool isLastPass = gRootConstants.ex;

    int inputBufferIndex;
    int outputBufferIndex;
    if (isEvenPass)
    {
        inputBufferIndex = (isHorizontalPass ? RT_TEMP2_INDEX : RT_TEMP1_INDEX);
        outputBufferIndex = (isHorizontalPass ? RT_TEMP1_INDEX : RT_RAW_OUTPUT_INDEX);
    }
    else
    {
        inputBufferIndex = (isHorizontalPass ? RT_RAW_OUTPUT_INDEX : RT_TEMP1_INDEX);
        outputBufferIndex = (isHorizontalPass ? RT_TEMP1_INDEX : RT_TEMP2_INDEX);
    }

    RWTexture2D<float4> inputBuffer = rtWindowBuffersRW[inputBufferIndex];
    RWTexture2D<float4> outputBuffer = rtWindowBuffersRW[outputBufferIndex];

#if 1
    int2 direction = isHorizontalPass ? int2(1, 0) : int2(0, 1);
    float3 output = blurPassTent(threadIdx.xy, inputBuffer, direction);
#else
	float3 output;
	if (!isLastPass) output = inputBuffer[threadIdx.xy];
	else output = blurPassTent2D(threadIdx.xy, inputBuffer);
#endif

    if (isLastPass)
    {
        if (rtWindowBuffersRW[RT_RAW_OUTPUT_INDEX][threadIdx.xy].a != 0)
            OutputBuffer[NonUniformResourceIndex(gData.drawingPosition + threadIdx.xy)] = float4(output, 1);
    }
    else
        outputBuffer[threadIdx.xy] = float4(output, 1);
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
    uint rng;
    
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

struct SurfaceData
{
    Material material;
    float3 position;
    float3 normal;
};

MaterialProperties getMatProps(Material m)
{
    MaterialProperties result;

    result.baseColor = m.albedo;
    result.metalness = m.metalness;
    result.roughness = m.roughness;
    result.emissive = 0;
    result.opacity = 1;
    result.reflectance = 0.5f;
    result.transmissivness = 0;
    
    return result;
}

float3 getSpectralSample(float u)
{
    return saturate(float3(abs(u * 6.0f - 3.0f) - 1.0f, 2.0f - abs(u * 6.0f - 2.0f), 2.0f - abs(u * 6.0f - 4.0f)));
}

SurfaceData loadSurfaceData(float2 attribUVs, uint instanceID, uint primitiveIndex, float3 rayOrigin, float3 rayDirection, float hitT)
{
    SurfaceData result;

    result.material = materialsBuffer[NonUniformResourceIndex(instanceID)];
    result.position = rayOrigin + rayDirection * hitT;
    result.normal = normalsBuffer[NonUniformResourceIndex(primitiveIndex)];

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

void castSecondaryRay(float2 u, float3 V, SurfaceData surfaceData, inout HitInfo payload, int type)
{
    RayDesc ray;
    ray.Origin = offset_ray(surfaceData.position, surfaceData.normal);
    ray.TMin = 0.0f;
    ray.TMax = PRIMARY_TMAX;
    
    float3 weight;
    if (evalIndirectCombinedBRDF(u, surfaceData.normal, surfaceData.normal, V, getMatProps(surfaceData.material), type, ray.Direction, weight))
    {
        payload.throughput *= weight;
        
	    // Trace the ray
        TraceRay(
		    SceneBVH,
		    RAY_FLAG_NONE,
		    0xFF,
		    0,
		    0,
		    0,
		    ray,
		    payload);
    }
}


[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    SurfaceData surfaceData = loadSurfaceData(attrib.uv, InstanceID(), PrimitiveIndex(), WorldRayOrigin(), WorldRayDirection(), RayTCurrent());
    payload.hitT = RayTCurrent();
    payload.bounce++;

    float3 V = -WorldRayDirection();

	// Account for emissivness
    payload.result += payload.throughput * surfaceData.material.emissive;
		 
    if (payload.bounce == 1)
    {
		// This is primary hit

		// Store G-Buffer
        rtWindowBuffersRW[NORMAL_MATERIAL_DEPTH_INDEX][NonUniformResourceIndex(payload.pixelIndex)] = float4(encodeNormalOctahedron(surfaceData.normal), asfloat(surfaceData.material.id), payload.hitT);

        float2 u = float2(rand(payload.rng), rand(payload.rng));

        payload.throughput = 1;
        castSecondaryRay(u, V, surfaceData, payload, SPECULAR_TYPE);
        u = float2(rand(payload.rng), rand(payload.rng));
        payload.throughput = 1;
        castSecondaryRay(u, V, surfaceData, payload, DIFFUSE_TYPE);

    }
    else if (payload.bounce < MAX_BOUNCES)
    {
		// This is secondary hit
        float p = rand(payload.rng);
        float2 u = float2(rand(payload.rng), rand(payload.rng));

        payload.throughput /= 0.5f;
        castSecondaryRay(u, V, surfaceData, payload, (p > 0.5f) ? DIFFUSE_TYPE : SPECULAR_TYPE);
    }

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
    
    // Initialize RNG
    payload.rng = initRNG(LaunchIndex, resolution, gData.frameNumber);
    
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
    
    //OutputBuffer[NonUniformResourceIndex(gData.drawingPosition + LaunchIndex)] = accumulationBuffer[LaunchIndex];
}
