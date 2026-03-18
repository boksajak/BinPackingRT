#include "common.h"
#include "shaderCompiler.h"
#include "fileSystem.h"
#include "shaders/shared.h"

class CustomBlob : public IDxcBlob {
public:

	CustomBlob() : data(0), size(0) {}
	CustomBlob(void* _data, size_t _size) : data(_data), size(_size) {}

	virtual LPVOID STDMETHODCALLTYPE GetBufferPointer(void) {
		return data;
	}
	virtual SIZE_T STDMETHODCALLTYPE GetBufferSize(void) {
		return size;
	}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
		/* [in] */ REFIID riid,
		/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) {
		return 0;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void) { return 0; }

	virtual ULONG STDMETHODCALLTYPE Release(void) { return 0; }

	void* data;
	size_t size;
};

class Renderer
{
public:

	void Initialize(HWND hwnd, FileSystem& fileSystem, unsigned int fieldWidth, unsigned int fieldHeight);
	void Cleanup();

	void ReloadShaders();
	bool Update(HWND hwnd, const float elapsedTime, unsigned int playingFieldHeight, unsigned int rtFieldPosX, unsigned int rtFieldPosY, std::vector<glm::ivec2>& blockDifferences, bool rtOn);

	// Strings drawing
	void ResetStrings();
	void AddString(const std::string& inputString, unsigned int posX, unsigned int posY, glm::vec4 fontColor = glm::vec4(1, 1, 1, 1), glm::vec4 bgColor = glm::vec4(0, 0, 0, 0), bool stringIsCP437 = false, glm::vec4 highlightColor = glm::vec4(0, 0, 0, 0), bool* highlightColorMask = nullptr, size_t highlightColorMaskLength = 0);
	void AddString(const std::string& s, float posX, float posY, glm::vec4 fontColor = glm::vec4(1, 1, 1, 1), glm::vec4 bgColor = glm::vec4(0, 0, 0, 0), bool stringIsCP437 = false, glm::vec4 highlightColor = glm::vec4(0, 0, 0, 0), bool* highlightColorMask = nullptr, size_t highlightColorMaskLength = 0);
	unsigned int GetCenteredTextX(const char* buffer, size_t bufferLength, unsigned int characterWidth, unsigned int frameWidth);
	void GetCharacterSize(int& characterWidth, int& characterHeight);
	void SetColorGrading(glm::vec3 crtColor, bool invertDisplay);

	// path traced Drawing
	int GetAllMaterialsCount();
	void ClearAllCubes();
	void AddCube(size_t materialIndex, glm::uvec2 position);

private:

	// Dx12 Helper Functions
	void initializeDx12(HWND hwnd, FileSystem& fileSystem);
	void createComputePasses(FileSystem& fileSystem);
	void createBuffers();
	CustomBlob readShaderBlobFromFile(FileSystem& fileSystem, std::wstring shaderName);

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

	// Materials buffer
	const size_t kMaxMaterials = 1024;
	ID3D12Resource* mMaterialsBuffer;
	ID3D12Resource* mMaterialsUploadHeap;
	Material* mMaterialData = nullptr;
	size_t mMaterialDataCurrentOffset;
	std::vector<Material> mMaterials;

	glm::vec3 kBlockColors[5] = { glm::vec3(0.9, 0.27, 0.46), glm::vec3(0.67, 1, 0.47), glm::vec3(1, 0.5, 1), glm::vec3(1, 1, 0.52), glm::vec3(0.33, 1, 1) };

	// Meshes
	struct AccelerationStructureBuffer
	{
		ID3D12Resource* pScratch = nullptr;
		ID3D12Resource* pResult = nullptr;
		ID3D12Resource* pInstanceDesc = nullptr;
	};

	struct D3D12ModelData {
		ID3D12Resource* vertexBuffer;
		ID3D12Resource* indexBuffer;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
		D3D12_INDEX_BUFFER_VIEW indexBufferView;
		AccelerationStructureBuffer asBuffer;
	};

	struct ModelData {
		glm::vec4* vertices;
		unsigned int* indices;

		size_t verticesCount;
		size_t indicesCount;
	};

	struct ModelInstance {

		UINT rtMask = 0xFF;
		UINT instanceId = 0;
		glm::mat3x4 transform;
		Material material;

		D3D12ModelData* d3d12Data = nullptr;
		ModelData* data = nullptr;
	};

	// Geometry info
	static const size_t			kVertexSize = sizeof(glm::vec4);
	static const size_t			kIndexSize = sizeof(unsigned int);
	static const DXGI_FORMAT	kIndexFormat = DXGI_FORMAT_R32_UINT;

	D3D12ModelData				mCubeModelD3DData;
	ModelData					mCubeModelData;

	ModelInstance				mArenaInstance;
	std::vector<ModelInstance>	mInstances;

	AccelerationStructureBuffer	mTLAS;
	uint64_t					mTlasSize;
	D3D12_GPU_VIRTUAL_ADDRESS	mTlasGPUAddress;

	// Path tracing stuff
	unsigned int				mRtFieldPixelsWidth;
	unsigned int				mRtFieldPixelsHeight;
	unsigned int				mRtFieldCharactersWidth;

	ID3D12Resource* mTLASInstanceDescriptorsCache;
	ID3D12Resource* mTLASScratchBuffersCache;
	ID3D12Resource* mTLASResultsCache;

	ID3D12Resource* mNormalsBuffer;
	ID3D12Resource* mNormalsBufferUploadHeap;
	UINT			mNormalsBufferLength;

	ID3D12Resource* mShaderTable;
	uint32_t mShaderTableRecordSize;

	ID3D12StateObject* mRTPSO = nullptr;
	ID3D12StateObjectProperties* mRTPSOInfo = nullptr;
	ID3D12PipelineState* mDenoisingPSO = nullptr;

	unsigned int	mFrameNumber;
	unsigned int	mAccumulatedFrames;
	unsigned int	mDenoiserIterationsCount = 1;
	unsigned int	mFieldWidth;
	unsigned int	mFieldHeight;
	float			mHorizontalStretch = 0.83f;
	bool            mIsFirstFrame = true;

	AccelerationStructureBuffer createBottomLevelAS(ID3D12Resource* vertexBuffer, UINT verticesCount, UINT64 VertexBufferStrideInBytes, ID3D12Resource* indexBuffer, UINT indicesCount, DXGI_FORMAT indexBufferFormat);
	void createBottomLevelAS(ModelData* data, D3D12ModelData* d3d12Data);
	void createBottomLevelAS(ModelInstance& model);

	void createVertexBuffer(ID3D12Device* device, UINT64 verticesCount, UINT64 vertexSize, void* vertexData, D3D12_VERTEX_BUFFER_VIEW& vertexBufferView, ID3D12Resource*& vertexBuffer, std::wstring debugName = L"Vertex Buffer");
	void createIndexBuffer(ID3D12Device* device, UINT64 indicesCount, UINT64 indexSize, DXGI_FORMAT indexFormat, void* indexData, D3D12_INDEX_BUFFER_VIEW& indexBufferView, ID3D12Resource*& indexBuffer, std::wstring debugName = L"Index Buffer");
	void createNormalsBuffer(const std::vector<glm::vec4>& normals);
	std::vector<glm::vec4> createNormals(ModelData modelData);

	void createModelBuffers(ModelData* data, D3D12ModelData* d3d12Data, std::wstring debugName = L"3D Model");
	void createModelBuffers(ModelInstance& model, std::wstring debugName = L"3D Model");

	void createShaderTable();
	void createRtResources(FileSystem& fileSystem);
	void createMaterialsBuffer();
	void updateMaterialsBuffer(Material* materialData, size_t materialsCount);
	void createAllMaterials();
	void calculateRTWindowSize();
	void createDisocclusionBuffer();
	void createRaytracingPSO(IDxcBlob* programBlob);
	void processChangedBlocksListToDisocclusionMap(std::vector<glm::ivec2>& differences);
	void updateRayTracing();
	void updateBlockDifferencesList();
	D3D12_RAYTRACING_INSTANCE_DESC createRTInstanceDesc(const ModelInstance& model);
	D3D12_RAYTRACING_INSTANCE_DESC createRTInstanceDesc(const ModelInstance& model, std::vector<Material>& materials);
	D3D12_RAYTRACING_INSTANCE_DESC createRTInstanceDesc(const ModelInstance& model, Material* materials, size_t& materialsOffset);
	void createTopLevelAS(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances);

	// RT Window Buffers
	enum class RtWindowBuffers {
		RAW_OUTPUT,
		NORMAL_MATERIAL_DEPTH,
		TEMP1,
		TEMP2,
		HISTORY_LENGTH,
		DISOCCLUSIONS,
		COUNT
	};

	static const int				kRtWindowBuffersCount = int(RtWindowBuffers::COUNT);
	ID3D12Resource* mRtWindowBuffers[kRtWindowBuffersCount];
	D3D12_CPU_DESCRIPTOR_HANDLE		mRtWindowsUavHandle;

	ID3D12Resource* mDisocclusionBufferUploadHeap;
	glm::vec4* mDisocclusionBufferData = nullptr;
};