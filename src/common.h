#pragma once
#include <Windows.h>

// Common includes
#include <map>
#include <unordered_map>
#include <chrono>
#include <random>

// GLM library
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/matrix_transform.hpp"

// DX12 includes
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#include <dxgi1_6.h>
#include <d3d12.h>
#include <dxgidebug.h>

#include "d3dx12.h"

// DX Compiler includes
#include "dxcapi.h"

// STB library
#include "stb_truetype.h"

// Dx12 helpers
#define SAFE_RELEASE( x ) { if ( x ) { x->Release(); x = NULL; } }
#define ALIGN(_alignment, _val) (((_val + _alignment - 1) / _alignment) * _alignment)
#define SAFE_DELETE_ARRAY( x ) { if ( x ) { delete[] x; x = NULL; } }

const unsigned int frameWidth = 1920;
const unsigned int frameHeight = 1080;
