#include "common.h"
#include "shaderCompiler.h"
#include "fileSystem.h"
#include "shaders/shared.h"

class Renderer
{
public:

	void Initialize(HWND hwnd, FileSystem& fileSystem);
	void Cleanup();

	void ReloadShaders();
	bool Update(HWND hwnd, const float elapsedTime);

	// Strings drawing
	void ResetStrings();
	void AddString(const std::string& inputString, unsigned int posX, unsigned int posY, glm::vec4 fontColor = glm::vec4(1, 1, 1, 1), glm::vec4 bgColor = glm::vec4(0, 0, 0, 0), bool stringIsCP437 = false, glm::vec4 highlightColor = glm::vec4(0, 0, 0, 0), bool* highlightColorMask = nullptr, size_t highlightColorMaskLength = 0);
	void AddString(const std::string& s, float posX, float posY, glm::vec4 fontColor = glm::vec4(1, 1, 1, 1), glm::vec4 bgColor = glm::vec4(0, 0, 0, 0), bool stringIsCP437 = false, glm::vec4 highlightColor = glm::vec4(0, 0, 0, 0), bool* highlightColorMask = nullptr, size_t highlightColorMaskLength = 0);
	unsigned int GetCenteredTextX(const char* buffer, size_t bufferLength, unsigned int characterWidth, unsigned int frameWidth);
	void GetCharacterSize(int& characterWidth, int& characterHeight);
	void SetColorGrading(glm::vec3 crtColor, bool invertDisplay);

	// path traced Drawing
	int getAllMaterialsCount();

private:

	// Dx12 Helper Functions
	void initializeDx12(HWND hwnd);
	void createComputePasses();
	void createBuffers();

	ID3D12PipelineState* createComputePSO(IDxcBlob& shaderBlob, ID3D12RootSignature* rootSignature);
	void moveToNextFrame();
	void submitCmdList();
	void present();
	void waitForGPU();
	void resetCommandList();
	D3D12_CPU_DESCRIPTOR_HANDLE getBackBufferView(UINT bufferIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE getCurrentBackBufferView();
	void transitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
	void uavBarrier(ID3D12Resource* resource);
	void uavBarrier(ID3D12Resource** resources, int resourceCount);
	void dispatchCompute2D(ID3D12PipelineState* pso, uint32_t dispatchWidth, uint32_t dispatchHeight);
	D3D12_CPU_DESCRIPTOR_HANDLE getDescriptorHandle(UINT index);
	ID3D12RootSignature* createGlobalRootSignature();
	ID3D12RootSignature* createRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc);
	void createBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, UINT64 alignment, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ID3D12Resource** ppResource);
	void createTexture(ID3D12Device* device, UINT64 width, UINT64 height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ID3D12Resource** ppResource);
	void uploadTexture(ID3D12Resource* texture, void* texels, UINT width, UINT height, UINT bytesPerTexel);
	void uploadConstantBuffer();
	void createFontAtlas(int characterHeight, int extraHorizontalSpacing, FileSystem& fileSystem);

	// Strings drawing helpers
	void createStringBuffer();
	void updateStringBuffer();

	// Dx12 Constant buffer
	ID3D12Resource* mGameDataCB = nullptr;
	ID3D12Resource* mGameDataCBUpload = nullptr;
	GameData mGameData;
	UINT mGameDataCBSize = 0;

	// State
	bool mReloadShaders = false;

	// Dx12 boilerplate
	ID3D12Device5* mDevice = nullptr;
	IDXGIAdapter1* mAdapter = nullptr;
	ID3D12Debug* mDebugController = nullptr;

	IDXGISwapChain3* mSwapChain = nullptr;

	DXGI_FORMAT mDepthBufferFormat = DXGI_FORMAT::DXGI_FORMAT_D32_FLOAT;
	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
	ID3D12DescriptorHeap* mDsvHeap = nullptr;

	static const unsigned int kMaxFramesInFlight = 2;
	ID3D12Fence* mFence = nullptr;
	UINT64 mFenceValues[kMaxFramesInFlight];
	HANDLE mFenceEvent;

	bool mIsTearingSupport = false;
	bool mEnableVSync = false;

	IDXGIFactory6* mDxgiFactory = nullptr;
	unsigned int mCurrentFrameIndex = 0;
	ID3D12Resource* mDepthStencilBuffer = nullptr;

	ID3D12DescriptorHeap* mRtvHeap = nullptr;
	UINT mRtvDescSize = 0;
	UINT mCbvSrvUavDescSize = 0;

	ID3D12GraphicsCommandList4* mCmdList = nullptr;

	ID3D12CommandQueue* mCmdQueue = nullptr;
	ID3D12CommandAllocator* mCmdAlloc[kMaxFramesInFlight];
	ID3D12DescriptorHeap* mDescriptorHeap = nullptr;
	ID3D12RootSignature* mGlobalRootSignature = nullptr;

	// Buffers
	ID3D12Resource* mBackBuffer[kMaxFramesInFlight];
	ID3D12Resource* mOutputBuffer = nullptr;
	ID3D12Resource* mDrawingBuffer = nullptr;

	// PSOs
	ID3D12PipelineState* mDrawStringPSO = nullptr;
	ID3D12PipelineState* mPostProcessPSO = nullptr;
	ID3D12PipelineState* mClearDrawingPSO = nullptr;

	// Shader stuff
	DxcShaderCompiler mShaderCompiler;

	// Font atlas stuff
	ID3D12Resource* mFontTexture = nullptr;
	const bool kUseCP437FontAtlas = true;
	const int kFontAtlasCharactersPerSide = 16;

	// Strings drawing stuff
	ID3D12Resource* mStringBuffer = nullptr;
	ID3D12Resource* mStringUploadHeap = nullptr;
	uint32_t* mStringData = nullptr;
	const size_t kMaxStringDataLength = 20 * 1024;
	size_t mStringDataCurrentOffset = 0;

};