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


// =========================================================================
//   String Drawing
// =========================================================================

[shader("compute")]
[numthreads(DRAW_STRING_THREADGROUP_SIZE, DRAW_STRING_THREADGROUP_SIZE, 1)]
void DrawString(
    int2 groupID : SV_GroupID,
    int2 groupThreadID : SV_GroupThreadID,
    int2 LaunchIndex : SV_DispatchThreadID)
{
	
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
    bool invertColors = false;
    const float exposureAdjustment = 1.5f;
    const float tonemappingContrast = 0.85f;
    const float tonemappingSaturation = 1.0f;
    const float3 colorBalance = float3(1, 1, 1);
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
    const float hBarScale = (PI * 0.5f) * gData.outputHeight;
    const float vBarScale = (PI) * gData.outputWidth;
    
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
    
    float2 textureSizeRcp = float2(1.0f / float(gData.outputWidth), 1.0f / float(gData.outputHeight));
    float2 uvs = float2(LaunchIndex) * textureSizeRcp;

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

    const float4 clearColor = float4(1, 0, 0, 0);
    DrawingBuffer[LaunchIndex] = clearColor;
}