
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
	uint outputWidth;
	uint outputHeight;
	float2 pad;

	// Strings drawing
	StringData strings[MAX_STRINGS_COUNT];

	uint2 characterSize;
	uint stringsCount;
	uint pad2;

	// Tonemapping
	float3 colorBalance;
	uint invertColors;

	// Path Tracing
	float4x4 view;

	float3 cameraPosition;
	float tanHalfFovY;

	uint2 drawingPosition;
	unsigned int frameNumber;
	float accumulationAlpha;

	float3 ambientColor;
	unsigned int drawToScreen;

	float horizontalStretch;
	unsigned int isFirstFrame;
	float2 pad3;
};

struct Material {
	float3 albedo;
	float metalness;
	float3 emissive;
	float roughness;
	unsigned int id;
	float3 pad;
};

#define DRAW_STRING_THREADGROUP_SIZE 16
#define CLEAR_UAV_THREADGROUP_SIZE 16
#define POST_PROCESS_THREADGROUP_SIZE 16
#define DENOISER_THREADGROUP_SIZE 16
