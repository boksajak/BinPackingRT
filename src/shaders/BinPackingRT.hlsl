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

[shader("compute")]
[numthreads(POST_PROCESS_THREADGROUP_SIZE, POST_PROCESS_THREADGROUP_SIZE, 1)]
void PostProcess(
    int2 groupID : SV_GroupID,
    int2 groupThreadID : SV_GroupThreadID,
    int2 LaunchIndex : SV_DispatchThreadID)
{
    if (LaunchIndex.x >= gData.outputWidth || LaunchIndex.y >= gData.outputHeight)
        return;

    OutputBuffer[LaunchIndex] = DrawingBuffer[LaunchIndex];
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