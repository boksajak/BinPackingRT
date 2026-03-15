
#if __cplusplus
#pragma once

// Typedefs for sharing types between C++ (using GLM) and HLSL
#include "..\common.h"
typedef uint32_t uint;

typedef glm::uint16_t half;

typedef glm::vec2 float2;
typedef glm::vec3 float3;
typedef glm::vec4 float4;

typedef glm::uvec2 uint2;
typedef glm::uvec3 uint3;
typedef glm::uvec4 uint4;

typedef glm::ivec2 int2;
typedef glm::ivec3 int3;
typedef glm::ivec4 int4;

typedef glm::mat2 float2x2;
typedef glm::mat3 float3x3;
typedef glm::mat4 float4x4;

#endif


struct GameData
{
	uint frameNumber;
	uint outputWidth;
	uint outputHeight;
	uint pad;
};


#define DRAW_STRING_THREADGROUP_SIZE 16
#define CLEAR_UAV_THREADGROUP_SIZE 16
