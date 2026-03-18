
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

#define MAX_STRINGS_COUNT 256

struct StringData
{
	float4 fontColor;
	float4 backgroundColor;
	float4 highlightColor;

	uint2 screenPosition;
	uint stringLength;
	uint stringStartOffset;
};

struct GameData
{
	// General
	uint frameNumber;
	uint outputWidth;
	uint outputHeight;
	uint pad;

	// Strings drawing
	StringData strings[MAX_STRINGS_COUNT];

	uint2 characterSize;
	uint stringsCount;
	uint pad2;

	// Tonemapping
	float3 colorBalance;
	uint invertColors;
};


#define DRAW_STRING_THREADGROUP_SIZE 16
#define CLEAR_UAV_THREADGROUP_SIZE 16
#define POST_PROCESS_THREADGROUP_SIZE 16
