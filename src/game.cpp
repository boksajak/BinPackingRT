#include "game.h"
#include "utils.h"

/**
* This enum specifies a layout of resources in NN shaders
*/
enum class DescriptorHeapConstants {

	// List of resources declared in the shader, as they appear in the descriptors heap
	GameDataCB = 0,
	FontAtlas,
	DrawingBuffer,
	OutputBuffer,
	UAV1,
	Total = UAV1 + 1,

	// Constant buffer range
	CBStart = GameDataCB,
	CBEnd = GameDataCB,
	CBTotal = CBEnd - CBStart + 1,

	// UAV space 0 range
	UAV0Start = DrawingBuffer,
	UAV0End = OutputBuffer,
	UAV0Total = UAV0End - UAV0Start + 1,

	// UAV space 1 range
	UAV1Start = UAV1,
	UAV1End = UAV1,
	UAV1Total = UAV1End - UAV1Start + 1,

	// SRV space 0 range
	SRV0Start = FontAtlas,
	SRV0End = FontAtlas,
	SRV0Total = SRV0End - SRV0Start + 1,
};

enum class RootParameterIndex {
	CbvSrvUavs,
	RootConstants,
	Count
};

void Game::Initialize(HWND hwnd)
{
	mShaderCompiler.Initialize();
	initializeDx12(hwnd);
}

void Game::ReloadShaders() {
	mReloadShaders = true;
}

bool Game::Update(HWND hwnd, const float elapsedTime)
{

	// Update constant buffer
	{
		mGameData.outputWidth = frameWidth;
		mGameData.outputHeight = frameHeight;

		uploadConstantBuffer();

		mGameData.frameNumber++;
	}

	// Setup root signature
	{
		ID3D12DescriptorHeap* ppHeaps[] = { mDescriptorHeap };
		mCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
		mCmdList->SetComputeRootSignature(mGlobalRootSignature);
		mCmdList->SetComputeRootDescriptorTable(UINT(RootParameterIndex::CbvSrvUavs), mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	}

	// Clear the screen
	D3D12_CPU_DESCRIPTOR_HANDLE destination = getCurrentBackBufferView();
	const glm::vec4 black = glm::vec4(0, 0, 0, 0);
	transitionBarrier(mBackBuffer[mCurrentFrameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCmdList->ClearRenderTargetView(destination, &black.x, 0, nullptr);

	// Specify the buffers we are going to render to - destination (back buffer)
	D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBufferViewHandle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();
	mCmdList->OMSetRenderTargets(1, &destination, true, &depthStencilBufferViewHandle);

	// Render the game
	{
		// Clear the drawing board
		{
			uavBarrier(mDrawingBuffer);

			uint32_t dispatchWidth = utils::divRoundUp(frameWidth, CLEAR_UAV_THREADGROUP_SIZE);
			uint32_t dispatchHeight = utils::divRoundUp(frameHeight, CLEAR_UAV_THREADGROUP_SIZE);
			dispatchCompute2D(mClearDrawingPSO, dispatchWidth, dispatchHeight);
		}

		// Post processing and write to output
		{ 
			uavBarrier(mDrawingBuffer);
			uavBarrier(mOutputBuffer);

			uint32_t dispatchWidth = utils::divRoundUp(frameWidth, POST_PROCESS_THREADGROUP_SIZE);
			uint32_t dispatchHeight = utils::divRoundUp(frameHeight, POST_PROCESS_THREADGROUP_SIZE);
			dispatchCompute2D(mPostProcessPSO, dispatchWidth, dispatchHeight);
		}

	}

	// Copy the final output and target texture to the back buffer
	{
		transitionBarrier(mBackBuffer[mCurrentFrameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
		transitionBarrier(mOutputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

		// Copy Final output (inference result)
		{
			CD3DX12_TEXTURE_COPY_LOCATION dest(mBackBuffer[mCurrentFrameIndex]);
			CD3DX12_TEXTURE_COPY_LOCATION src(mOutputBuffer);
			CD3DX12_BOX box(0, 0, frameWidth, frameHeight);
			mCmdList->CopyTextureRegion(&dest, 0, 0, 0, &src, &box);
		}

		transitionBarrier(mBackBuffer[mCurrentFrameIndex], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
		transitionBarrier(mOutputBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	transitionBarrier(mBackBuffer[mCurrentFrameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// End frame - process cmd. list and move to next frame
	submitCmdList();
	waitForGPU();
	present();
	moveToNextFrame();
	resetCommandList();

	// Reload shaders here if needed
	if (mReloadShaders) {
		createComputePasses();
		mReloadShaders = false;
	}

	return true;
}

void Game::Cleanup()
{
}


ID3D12PipelineState* Game::createComputePSO(IDxcBlob& shaderBlob, ID3D12RootSignature* rootSignature)
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
	pipelineDesc.pRootSignature = rootSignature;
	pipelineDesc.CS.pShaderBytecode = shaderBlob.GetBufferPointer();
	pipelineDesc.CS.BytecodeLength = shaderBlob.GetBufferSize();
	pipelineDesc.NodeMask = 0;
	pipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	ID3D12PipelineState* pso = nullptr;
	HRESULT hr = mDevice->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pso));
	utils::validate(hr, L"Error: failed to create compute PSO!");

	return pso;
}

void Game::moveToNextFrame()
{
	// Schedule a Signal command in the queue
	const UINT64 currentFenceValue = mFenceValues[mCurrentFrameIndex];
	HRESULT hr = mCmdQueue->Signal(mFence, currentFenceValue);
	utils::validate(hr, L"Error: failed to signal command queue!");

	// Update the frame index
	mCurrentFrameIndex = mSwapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is
	if (mFence->GetCompletedValue() < mFenceValues[mCurrentFrameIndex])
	{
		hr = mFence->SetEventOnCompletion(mFenceValues[mCurrentFrameIndex], mFenceEvent);
		utils::validate(hr, L"Error: failed to set fence value!");

		WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame
	mFenceValues[mCurrentFrameIndex] = currentFenceValue + 1;
}

void Game::submitCmdList()
{
	mCmdList->Close();

	ID3D12CommandList* pGraphicsList = { mCmdList };
	mCmdQueue->ExecuteCommandLists(1, &pGraphicsList);
	mFenceValues[mCurrentFrameIndex]++;
	mCmdQueue->Signal(mFence, mFenceValues[mCurrentFrameIndex]);
}

void Game::present()
{
	// When using sync interval 0, it is recommended to always pass the tearing
	// flag when it is supported, even when presenting in windowed mode.
	// However, this flag cannot be used if the app is in fullscreen mode as a
	// result of calling SetFullscreenState.
	UINT presentFlags = (!mEnableVSync && mIsTearingSupport) ? DXGI_PRESENT_ALLOW_TEARING : 0;

	HRESULT hr = mSwapChain->Present(mEnableVSync ? 1 : 0, presentFlags);
	if (FAILED(hr))
	{
		hr = mDevice->GetDeviceRemovedReason();
		utils::validate(hr, L"Error: failed to present!");
	}
}

void Game::waitForGPU()
{
	// Schedule a signal command in the queue
	HRESULT hr = mCmdQueue->Signal(mFence, mFenceValues[mCurrentFrameIndex]);
	utils::validate(hr, L"Error: failed to signal fence!");

	// Wait until the fence has been processed
	hr = mFence->SetEventOnCompletion(mFenceValues[mCurrentFrameIndex], mFenceEvent);
	utils::validate(hr, L"Error: failed to set fence event!");

	WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame
	mFenceValues[mCurrentFrameIndex]++;
}

void Game::resetCommandList()
{
	// Reset the command allocator for the current frame
	HRESULT hr = mCmdAlloc[mCurrentFrameIndex]->Reset();
	utils::validate(hr, L"Error: failed to reset command allocator!");

	// Reset the command list for the current frame
	hr = mCmdList->Reset(mCmdAlloc[mCurrentFrameIndex], nullptr);
	utils::validate(hr, L"Error: failed to reset command list!");
}

D3D12_CPU_DESCRIPTOR_HANDLE Game::getBackBufferView(UINT bufferIndex) {

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	renderTargetViewHandle.ptr += (mRtvDescSize * bufferIndex);

	return renderTargetViewHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE Game::getCurrentBackBufferView() {
	return getBackBufferView(mCurrentFrameIndex);
}


void Game::transitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = from;
	barrier.Transition.StateAfter = to;

	mCmdList->ResourceBarrier(1, &barrier);
}

void Game::uavBarrier(ID3D12Resource* resource) {
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.UAV.pResource = resource;

	mCmdList->ResourceBarrier(1, &barrier);
}

void Game::uavBarrier(ID3D12Resource** resources, int resourceCount) {
	assert(resourceCount <= 8);
	D3D12_RESOURCE_BARRIER barriers[8];

	for (int i = 0; i < resourceCount; i++)
	{
		barriers[i] = {};
		barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barriers[i].UAV.pResource = resources[i];
	}

	mCmdList->ResourceBarrier(resourceCount, barriers);
}

void Game::dispatchCompute2D(ID3D12PipelineState* pso, uint32_t dispatchWidth, uint32_t dispatchHeight)
{
	if (dispatchWidth > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION || dispatchHeight > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)
	{
		utils::validate(E_FAIL, L"Error: dispatch size too large!");
	}

	mCmdList->SetPipelineState(pso);
	mCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);
}

D3D12_CPU_DESCRIPTOR_HANDLE Game::getDescriptorHandle(UINT index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += (mRtvDescSize * index);
	return handle;
}

ID3D12RootSignature* Game::createGlobalRootSignature() {

	// CBVs
	D3D12_DESCRIPTOR_RANGE cbvRange;
	cbvRange.BaseShaderRegister = 0;
	cbvRange.NumDescriptors = UINT(DescriptorHeapConstants::CBTotal);
	cbvRange.RegisterSpace = 0;
	cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	cbvRange.OffsetInDescriptorsFromTableStart = UINT(DescriptorHeapConstants::CBStart);

	// UAVs
	D3D12_DESCRIPTOR_RANGE uavRange0;
	uavRange0.BaseShaderRegister = 0;
	uavRange0.NumDescriptors = UINT(DescriptorHeapConstants::UAV0Total);
	uavRange0.RegisterSpace = 0;
	uavRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uavRange0.OffsetInDescriptorsFromTableStart = UINT(DescriptorHeapConstants::UAV0Start);

	D3D12_DESCRIPTOR_RANGE uavRange1;
	uavRange1.BaseShaderRegister = 0;
	uavRange1.NumDescriptors = UINT(DescriptorHeapConstants::UAV1Total);
	uavRange1.RegisterSpace = 1;
	uavRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uavRange1.OffsetInDescriptorsFromTableStart = UINT(DescriptorHeapConstants::UAV1Start);

	// SRVs
	D3D12_DESCRIPTOR_RANGE srvRange0;
	srvRange0.BaseShaderRegister = 0;
	srvRange0.NumDescriptors = UINT(DescriptorHeapConstants::SRV0Total);
	srvRange0.RegisterSpace = 0;
	srvRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange0.OffsetInDescriptorsFromTableStart = UINT(DescriptorHeapConstants::SRV0Start);

	D3D12_DESCRIPTOR_RANGE cbvUavSrvRanges[] = {
		cbvRange,
		uavRange0,
		uavRange1,
		srvRange0
	};

	// Root parameter - CBV/UAV/SRV
	D3D12_ROOT_PARAMETER paramCbvUavSrv = {};
	paramCbvUavSrv.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	paramCbvUavSrv.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	paramCbvUavSrv.DescriptorTable.NumDescriptorRanges = _countof(cbvUavSrvRanges);
	paramCbvUavSrv.DescriptorTable.pDescriptorRanges = cbvUavSrvRanges;

	// Root parameter - Root constants
	D3D12_ROOT_PARAMETER paramRootConstants = {};
	paramRootConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	paramRootConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	paramRootConstants.Constants.Num32BitValues = 6;
	paramRootConstants.Constants.ShaderRegister = 1;
	paramRootConstants.Constants.RegisterSpace = 0;

	D3D12_ROOT_PARAMETER rootParams[UINT(RootParameterIndex::Count)];
	rootParams[UINT(RootParameterIndex::CbvSrvUavs)] = paramCbvUavSrv;
	rootParams[UINT(RootParameterIndex::RootConstants)] = paramRootConstants;

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = _countof(rootParams);
	rootDesc.pParameters = rootParams;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	return createRootSignature(rootDesc);
}

ID3D12RootSignature* Game::createRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc) {
	HRESULT hr;
	ID3DBlob* sig;
	ID3DBlob* error;

	hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &error);
	utils::validate(hr, L"Error: failed to serialize root signature!");

	ID3D12RootSignature* pRootSig;
	hr = mDevice->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
	utils::validate(hr, L"Error: failed to create root signature!");

	SAFE_RELEASE(sig);
	SAFE_RELEASE(error);
	return pRootSig;
}

void Game::uploadConstantBuffer()
{
	transitionBarrier(mGameDataCB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_SUBRESOURCE_DATA bufferDataDesc = {};
	bufferDataDesc.pData = &mGameData;
	bufferDataDesc.RowPitch = mGameDataCBSize;
	bufferDataDesc.SlicePitch = bufferDataDesc.RowPitch;

	UINT64 uploadedBytes = UpdateSubresources(mCmdList, mGameDataCB, mGameDataCBUpload, 0, 0, 1, &bufferDataDesc);
	HRESULT hr = (mGameDataCBSize == uploadedBytes ? S_OK : E_FAIL);
	utils::validate(hr, L"Error: failed to update constant buffer!");

	transitionBarrier(mGameDataCB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void Game::createBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, UINT64 alignment, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ID3D12Resource** ppResource)
{
	HRESULT hr;

	D3D12_HEAP_PROPERTIES heapDesc = {};
	heapDesc.Type = heapType;
	heapDesc.CreationNodeMask = 1;
	heapDesc.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = alignment;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Width = size;
	resourceDesc.Flags = flags;

	// Create the GPU resource
	hr = device->CreateCommittedResource(&heapDesc, D3D12_HEAP_FLAG_NONE, &resourceDesc, state, nullptr, IID_PPV_ARGS(ppResource));
	utils::validate(hr, L"Error: failed to create buffer resource!");
}

void Game::createTexture(ID3D12Device* device, UINT64 width, UINT64 height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ID3D12Resource** ppResource)
{
	D3D12_HEAP_PROPERTIES heapDesc = {};
	heapDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapDesc.CreationNodeMask = 1;
	heapDesc.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC desc = {};
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = format;
	desc.Flags = flags;
	desc.Width = width;
	desc.Height = height;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	// Create the GPU resource
	HRESULT hr = mDevice->CreateCommittedResource(&heapDesc, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(ppResource));
	utils::validate(hr, L"Error: failed to create texture!");
}

void Game::createBuffers()
{
	// Create output and drawing buffer
	{
		createTexture(mDevice, frameWidth, frameHeight, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &mOutputBuffer);
		createTexture(mDevice, frameWidth, frameHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &mDrawingBuffer);
	}

	// Create constant buffer
	{
		mGameDataCBSize = ALIGN(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(mGameData));
		createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, 0, mGameDataCBSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &mGameDataCB);

		UINT64 uploadBufferSize = GetRequiredIntermediateSize(mGameDataCB, 0, 1);
		uploadBufferSize = ALIGN(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, uploadBufferSize);
		createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, uploadBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &mGameDataCBUpload);
	}

	// Fill descriptor heap
	{
		// Create the NNData CBV
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.SizeInBytes = mGameDataCBSize;
			cbvDesc.BufferLocation = mGameDataCB->GetGPUVirtualAddress();
			mDevice->CreateConstantBufferView(&cbvDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::GameDataCB)));
		}

		// Create the output buffer UAV
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			mDevice->CreateUnorderedAccessView(mOutputBuffer, nullptr, &uavDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::OutputBuffer)));
		}

		// Create the drawing buffer UAV
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			mDevice->CreateUnorderedAccessView(mOutputBuffer, nullptr, &uavDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::DrawingBuffer)));
		}
	}
}

void Game::createComputePasses()
{
	const std::wstring shaderName = L"BinPackingRT.hlsl";
	std::wstring shaderFolder = utils::getExePath() + L"shaders\\";
	std::wstring gameShaderFile = shaderFolder + shaderName;

	FILE* file = std::fopen(utils::wstringToString(gameShaderFile).c_str(), "r");
	if (!file)
	{
		gameShaderFile = utils::stringToWstring(SHADERS_DIR) + shaderName;
	}
	else
	{
		std::fclose(file);
	}

	std::vector<std::wstring> compilerFlags;

	compilerFlags.push_back(L"/D PI=" + std::to_wstring(glm::pi<float>()));

	// Process compiler flags to an array of string pointers
	std::vector<LPCWSTR> flagsPointers;
	flagsPointers.reserve(compilerFlags.size());
	for (const std::wstring& flag : compilerFlags) {
		flagsPointers.push_back(flag.c_str());
	}

	// Create main drawing pass
	{
		IDxcBlob* shaderBlob = mShaderCompiler.CompileShader(gameShaderFile.c_str(), L"DrawString", L"cs_6_2", flagsPointers);

		SAFE_RELEASE(mDrawStringPSO);
		mDrawStringPSO = createComputePSO(*shaderBlob, mGlobalRootSignature);
	}

	// Create post-process pass
	{
		IDxcBlob* shaderBlob = mShaderCompiler.CompileShader(gameShaderFile.c_str(), L"PostProcess", L"cs_6_2", flagsPointers);

		SAFE_RELEASE(mPostProcessPSO);
		mPostProcessPSO = createComputePSO(*shaderBlob, mGlobalRootSignature);
	}

	// Create main drawing pass
	{
		IDxcBlob* shaderBlob = mShaderCompiler.CompileShader(gameShaderFile.c_str(), L"ClearUAV", L"cs_6_2", flagsPointers);

		SAFE_RELEASE(mClearDrawingPSO);
		mClearDrawingPSO = createComputePSO(*shaderBlob, mGlobalRootSignature);
	}
}

void Game::initializeDx12(HWND hwnd)
{
	for (int i = 0; i < kMaxFramesInFlight; i++) {
		mFenceValues[i] = 0;
		mBackBuffer[i] = nullptr;
		mCmdAlloc[i] = nullptr;
	}

	// Create DXGI factory
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&mDxgiFactory));
	utils::validate(hr, L"Error: failed to create DXGI factory!");

	const D3D_FEATURE_LEVEL kDx12FeatureLevel = D3D_FEATURE_LEVEL_12_1;

#ifdef _DEBUG
	// Enable the D3D12 debug layer.
	{
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&mDebugController))))
		{
			mDebugController->EnableDebugLayer();
		}
	}
#endif

	// Find a suitable adapter to run D3D
	int i = 0;
	IDXGIAdapter1* adapter = nullptr;
	DXGI_ADAPTER_DESC1 selectedAdapterDesc = {};
	bool adapterFound = false;
	while (mDxgiFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		ID3D12Device5* tempDevice;

		if (SUCCEEDED(D3D12CreateDevice(adapter, kDx12FeatureLevel, IID_PPV_ARGS(&tempDevice))))
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 features = {};
			HRESULT hr = tempDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features));
			if (SUCCEEDED(hr) && features.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
					selectedAdapterDesc = desc;
					adapterFound = true;
					break;
				}
			}
			SAFE_RELEASE(tempDevice);
			adapter->Release();
			i++;
		}
	}

	if (!adapterFound)
	{
		utils::validate(E_FAIL, L"Could not find a ray tracing adapter to play this game!");
	}

	// Create D3D Device
	{
		HRESULT hr = mDxgiFactory->EnumAdapterByLuid(selectedAdapterDesc.AdapterLuid, IID_PPV_ARGS(&mAdapter));
		utils::validate(hr, L"Error: failed to enumerate selected adapter by luid!");

		hr = D3D12CreateDevice(mAdapter, kDx12FeatureLevel, IID_PPV_ARGS(&mDevice));
		utils::validate(hr, L"Error: failed to create D3D device!");
	}

	// Create command queue
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		HRESULT hr = mDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&mCmdQueue));
		utils::validate(hr, L"Error: failed to create command queue!");
	}

	// Create a command allocator for each frame
	{
		for (UINT n = 0; n < kMaxFramesInFlight; n++)
		{
			HRESULT hr = mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCmdAlloc[n]));
			utils::validate(hr, L"Error: failed to create the command allocator!");
		}
	}

	// Create Command List
	{
		// Create the command list
		HRESULT hr = mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCmdAlloc[mCurrentFrameIndex], nullptr, IID_PPV_ARGS(&mCmdList));
		hr = mCmdList->Close();
		utils::validate(hr, L"Error: failed to create the command list!");

		resetCommandList();
	}

	// Create fence
	{
		HRESULT hr = mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
		utils::validate(hr, L"Error: failed to create fence!");

		mFenceValues[mCurrentFrameIndex]++;

		// Create an event handle to use for frame synchronization
		mFenceEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
		if (mFenceEvent == nullptr)
		{
			hr = HRESULT_FROM_WIN32(GetLastError());
			utils::validate(hr, L"Error: failed to create fence event!");
		}
	}

	// Create swap chain
	{
		// Check for tearing support
		BOOL allowTearing = FALSE;
		HRESULT hr = mDxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
		utils::validate(hr, L"Error: failed to create DXGI factory!");

		mIsTearingSupport = SUCCEEDED(hr) && allowTearing;

		// Describe the swap chain
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.BufferCount = kMaxFramesInFlight;
		desc.Width = frameWidth;
		desc.Height = frameHeight;
		desc.Format = mBackBufferFormat;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Flags = mIsTearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		IDXGISwapChain1* tempSwapChain;

		// Create the swap chain
		hr = mDxgiFactory->CreateSwapChainForHwnd(mCmdQueue, hwnd, &desc, 0, 0, &tempSwapChain);
		utils::validate(hr, L"Error: failed to create swap chain!");

		if (mIsTearingSupport)
		{
			// When tearing support is enabled we will handle ALT+Enter key presses in the
			// window message loop rather than let DXGI handle it by calling SetFullscreenState.
			mDxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
			utils::validate(hr, L"Error: failed to make window association!");
		}

		// Get the swap chain interface
		hr = tempSwapChain->QueryInterface(IID_PPV_ARGS(&mSwapChain));
		utils::validate(hr, L"Error: failed to cast swap chain!");

		SAFE_RELEASE(tempSwapChain);
		mCurrentFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
	}

	// Create RTV heap
	{
		// Describe the RTV heap
		D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
		rtvDesc.NumDescriptors = kMaxFramesInFlight;
		rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		// Create the RTV heap
		HRESULT hr = mDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRtvHeap));
		utils::validate(hr, L"Error: failed to create RTV descriptor heap!");

		mRtvDescSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create DSV heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDescription;
		heapDescription.NumDescriptors = 1;
		heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heapDescription.NodeMask = 0;

		HRESULT result = mDevice->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&mDsvHeap));
		utils::validate(result, L"Error: failed to create DSV heap!");
	}

	// Create back buffer
	{
		HRESULT hr;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

		// Create a RTV for each back buffer
		for (unsigned int n = 0; n < kMaxFramesInFlight; n++)
		{
			hr = mSwapChain->GetBuffer(n, IID_PPV_ARGS(&mBackBuffer[n]));
			utils::validate(hr, L"Error: failed to get swap chain buffer!");

			mDevice->CreateRenderTargetView(mBackBuffer[n], nullptr, rtvHandle);

			rtvHandle.ptr += mRtvDescSize;
		}
	}

	// Create depth stencil buffer
	{
		// Create the depth/stencil buffer and view.
		D3D12_RESOURCE_DESC depthStencilDesc;
		depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilDesc.Alignment = 0;
		depthStencilDesc.Width = frameWidth;
		depthStencilDesc.Height = frameHeight;
		depthStencilDesc.DepthOrArraySize = 1;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.Format = mDepthBufferFormat;
		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
		depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE optClear;
		optClear.Format = mDepthBufferFormat;
		optClear.DepthStencil.Depth = 1.0f;
		optClear.DepthStencil.Stencil = 0;

		mDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_COMMON,
			&optClear,
			IID_PPV_ARGS(&mDepthStencilBuffer));

		// Create descriptor to mip level 0 of entire resource	using the format of the resource.
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Format = mDepthBufferFormat;
		dsvDesc.Texture2D.MipSlice = 0;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mDsvHeap->GetCPUDescriptorHandleForHeapStart());

		mDevice->CreateDepthStencilView(mDepthStencilBuffer, &dsvDesc, hDescriptor);

		// Transition the resource from its initial state to be used as a depth buffer.
		transitionBarrier(mDepthStencilBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}

	// Create root signature
	{
		mGlobalRootSignature = createGlobalRootSignature();
	}

	// Create descriptor heap
	{
		// Describe the CBV/SRV/UAV heap
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = UINT(DescriptorHeapConstants::Total);
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		// Create the descriptor heap
		HRESULT hr = mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mDescriptorHeap));
		utils::validate(hr, L"Error: failed to create descriptor heap!");

		// Get the descriptor heap handle and increment size
		mCbvSrvUavDescSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Create shaders and compute passes
	{
		createComputePasses();
	}

	// Create GPU resources
	{
		createBuffers();
	}

}