#include "renderer.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/**
* This enum specifies a layout of resources in NN shaders
*/
enum class DescriptorHeapConstants {

	// List of resources declared in the shader, as they appear in the descriptors heap
	GameDataCB = 0,
	DrawingBuffer,
	OutputBuffer,
	FontAtlas,
	StringsBuffer,	
	MaterialsBuffer,
	TLAS,
	NormalsBuffer,
	RAW_OUTPUT,
	NORMAL_MATERIAL_DEPTH,
	TEMP1,
	TEMP2,
	HISTORY_LENGTH,
	DISOCCLUSIONS,
	Total = DISOCCLUSIONS + 1,

	// Constant buffer range
	CBStart = GameDataCB,
	CBEnd = GameDataCB,
	CBTotal = CBEnd - CBStart + 1,

	// UAV space 0 range
	UAV0Start = DrawingBuffer,
	UAV0End = OutputBuffer,
	UAV0Total = UAV0End - UAV0Start + 1,

	// UAV space 1 range
	UAV1Start = RAW_OUTPUT,
	UAV1End = DISOCCLUSIONS,
	UAV1Total = UAV1End - UAV1Start + 1,

	// SRV space 0 range
	SRV0Start = FontAtlas,
	SRV0End = NormalsBuffer,
	SRV0Total = SRV0End - SRV0Start + 1,
};

enum class RootParameterIndex {
	CbvSrvUavs,
	RootConstants,
	Count
};

glm::vec4 cubeVertices[8] = {
	glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
	glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
	glm::vec4(1.0f, 0.0f, 1.0f, 0.0f),
	glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
	glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
	glm::vec4(1.0f, 1.0f, 0.0f, 0.0f),
	glm::vec4(0.0f, 1.0f, 1.0f, 0.0f),
	glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)
};

unsigned int cubeIndices[30] = {
	0, 1, 2,
	0, 2, 3,
	3, 2, 4,
	3, 4, 5,
	5, 4, 6,
	5, 6, 7,
	7, 6, 1,
	7, 1, 0,
	1, 6, 4,
	1, 4, 2
};


void Renderer::Initialize(HWND hwnd, FileSystem& fileSystem, unsigned int fieldWidth, unsigned int fieldHeight)
{
	mGameData.stringsCount = 0;
	mGameData.frameNumber = 0;
	mStringData = nullptr;

	for (int i = 0; i < kRtWindowBuffersCount; i++)
	{
		mRtWindowBuffers[i] = nullptr;
	}

	mFrameNumber = 0;
	mAccumulatedFrames = 0;

	mFieldWidth = fieldWidth;
	mFieldHeight = fieldHeight;

	mShaderCompiler.Initialize();

	initializeDx12(hwnd, fileSystem);
}

void Renderer::Cleanup()
{
	SAFE_DELETE_ARRAY(mStringData);
}

void Renderer::ReloadShaders()
{
	mReloadShaders = true;
}

int Renderer::GetAllMaterialsCount()
{
	return int(mMaterials.size());
}

void Renderer::ClearAllCubes()
{
	mInstances.clear();
}

void Renderer::AddCube(size_t materialIndex, glm::uvec2 position)
{
	Material material = mMaterials[materialIndex];

	ModelInstance cubeInstance;
	cubeInstance.d3d12Data = &mCubeModelD3DData;
	cubeInstance.data = &mCubeModelData;
	cubeInstance.instanceId = UINT(mInstances.size()) + 1;
	cubeInstance.rtMask = 0xFF;
	cubeInstance.transform = glm::mat3x4(0.0f);
	cubeInstance.transform[0][0] = 1.0f;
	cubeInstance.transform[1][1] = 1.0f;
	cubeInstance.transform[2][2] = 1.5f;
	cubeInstance.material = material;

	cubeInstance.transform[0][3] = (float)position.x;
	cubeInstance.transform[1][3] = (float)position.y;
	cubeInstance.transform[2][3] = -2.0f;

	mInstances.push_back(cubeInstance);
}

bool Renderer::Update(HWND hwnd, const float elapsedTime, unsigned int playingFieldHeight, unsigned int rtFieldPosX, unsigned int rtFieldPosY, std::vector<glm::ivec2>& blockDifferences, bool rtOn)
{

	// Update constant buffer
	{
		mGameData.outputWidth = frameWidth;
		mGameData.outputHeight = frameHeight;

		// Update camera
		glm::mat4 viewMatrix = glm::lookAtLH(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
		glm::mat4 invViewMatrix = glm::inverse(viewMatrix);
		glm::mat4 transposeInverseViewMatrix = glm::transpose(invViewMatrix);

		mGameData.view = transposeInverseViewMatrix;
		mGameData.cameraPosition = glm::vec3(10, 10, 38);
		mGameData.tanHalfFovY = -float(playingFieldHeight / 2) / mGameData.cameraPosition.z;
		mGameData.horizontalStretch = 1.0f / mHorizontalStretch;
		mGameData.drawingPosition.x = rtFieldPosX;
		mGameData.drawingPosition.y = rtFieldPosY;
		mGameData.frameNumber = mFrameNumber;
		mGameData.drawToScreen = rtOn ? 1 : 0;
		glm::vec3 mAmbientColor = glm::vec3(1, 1, 1);
		float mExposure = 0.7f;
		mGameData.ambientColor = mAmbientColor * mExposure;
		mGameData.isFirstFrame = mIsFirstFrame;
		mIsFirstFrame = false;

		uploadConstantBuffer();

		mGameData.frameNumber++;
	}

	// Update strings buffer
	{
		updateStringBuffer();
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

		// Render strings
		if (mGameData.stringsCount > 0)
		{
			uavBarrier(mDrawingBuffer);

			uint32_t dispatchWidth = 1;
			uint32_t dispatchHeight = 1;
			for (uint32_t i = 0; i < mGameData.stringsCount; i++) {
				dispatchWidth = glm::max(dispatchWidth, utils::divRoundUp(mGameData.characterSize.x * mGameData.strings[i].stringLength, DRAW_STRING_THREADGROUP_SIZE));
				dispatchHeight = glm::max(dispatchHeight, utils::divRoundUp(mGameData.characterSize.y, DRAW_STRING_THREADGROUP_SIZE));
			}

			mCmdList->SetPipelineState(mDrawStringPSO);
			mCmdList->Dispatch(dispatchWidth, dispatchHeight, mGameData.stringsCount);
		}

		// Post processing and write to output
		{
			uavBarrier(mDrawingBuffer);
			uavBarrier(mOutputBuffer);

			uint32_t dispatchWidth = utils::divRoundUp(frameWidth, POST_PROCESS_THREADGROUP_SIZE);
			uint32_t dispatchHeight = utils::divRoundUp(frameHeight, POST_PROCESS_THREADGROUP_SIZE);
			dispatchCompute2D(mPostProcessPSO, dispatchWidth, dispatchHeight);
		}

		if (rtOn)
		{
			// Update RT stuff
			{
				processChangedBlocksListToDisocclusionMap(blockDifferences);
				updateRayTracing();
			}

			// Path tracing
			{
				// Dispatch rays
				D3D12_DISPATCH_RAYS_DESC desc = {};
				desc.RayGenerationShaderRecord.StartAddress = mShaderTable->GetGPUVirtualAddress();
				desc.RayGenerationShaderRecord.SizeInBytes = mShaderTableRecordSize;

				desc.MissShaderTable.StartAddress = mShaderTable->GetGPUVirtualAddress() + mShaderTableRecordSize;
				desc.MissShaderTable.SizeInBytes = mShaderTableRecordSize;		// Only a single Miss program entry
				desc.MissShaderTable.StrideInBytes = mShaderTableRecordSize;

				desc.HitGroupTable.StartAddress = mShaderTable->GetGPUVirtualAddress() + (mShaderTableRecordSize * 2);
				desc.HitGroupTable.SizeInBytes = mShaderTableRecordSize;			// Only a single Hit program entry
				desc.HitGroupTable.StrideInBytes = mShaderTableRecordSize;

				desc.Width = mRtFieldPixelsWidth;
				desc.Height = mRtFieldPixelsHeight;
				desc.Depth = 1;

				mCmdList->SetPipelineState1(mRTPSO);

				mAccumulatedFrames++;
				float accumulationAlpha = glm::max(1.0f / 512.0f, 1.0f / float(mAccumulatedFrames));

				uavBarrier(mRtWindowBuffers[int(RtWindowBuffers::RAW_OUTPUT)]);
				uavBarrier(mRtWindowBuffers[int(RtWindowBuffers::HISTORY_LENGTH)]);
				uavBarrier(mRtWindowBuffers[int(RtWindowBuffers::DISOCCLUSIONS)]);

				uint32_t rootConstants[6] = { mRtFieldPixelsWidth, mRtFieldPixelsHeight, glm::floatBitsToUint(1.0f / float(mRtFieldPixelsWidth)), glm::floatBitsToUint(1.0f / float(mRtFieldPixelsHeight)), mFrameNumber, glm::floatBitsToUint(accumulationAlpha) };
				mCmdList->SetComputeRoot32BitConstants(UINT(RootParameterIndex::RootConstants), _countof(rootConstants), &rootConstants, 0);
				mCmdList->DispatchRays(&desc);

				mFrameNumber++;
				mFrameNumber = mFrameNumber % 0x1000; //< Wrap counter early to catch bugs with wraping
			}

			// Denoising
			{
				uint32_t dispatchWidth = utils::divRoundUp(mRtFieldPixelsWidth, DENOISER_THREADGROUP_SIZE);
				uint32_t dispatchHeight = utils::divRoundUp(mRtFieldPixelsHeight, DENOISER_THREADGROUP_SIZE);

				uavBarrier(mRtWindowBuffers[int(RtWindowBuffers::RAW_OUTPUT)]);
				uavBarrier(mRtWindowBuffers[int(RtWindowBuffers::NORMAL_MATERIAL_DEPTH)]);
				uavBarrier(mOutputBuffer);

				// Run drawing shader pass
				mCmdList->SetPipelineState(mDenoisingPSO);

				for (unsigned int i = 0; i < mDenoiserIterationsCount; i++) {
					{
						uint32_t rootConstants[6] = { mRtFieldPixelsWidth, mRtFieldPixelsHeight, 1u /* HORIZONTAL */, i % 2, 0u, 0u };
						mCmdList->SetComputeRoot32BitConstants(UINT(RootParameterIndex::RootConstants), _countof(rootConstants), &rootConstants, 0);
						mCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);
					}
					uavBarrier(mRtWindowBuffers[int(RtWindowBuffers::TEMP1)]);
					{
						uint32_t rootConstants[6] = { mRtFieldPixelsWidth, mRtFieldPixelsHeight, 0u /* VERTICAL */, i % 2, (i == (mDenoiserIterationsCount - 1) ? 1u : 0u), 0u };
						mCmdList->SetComputeRoot32BitConstants(UINT(RootParameterIndex::RootConstants), _countof(rootConstants), &rootConstants, 0);
						mCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);
					}
				}
			}
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

void Renderer::ResetStrings()
{
	mStringDataCurrentOffset = 0;
	mGameData.stringsCount = 0;
}

void Renderer::AddString(const std::string& inputString, unsigned int posX, unsigned int posY, glm::vec4 fontColor, glm::vec4 bgColor, bool stringIsCP437, glm::vec4 highlightColor, bool* highlightColorMask, size_t highlightColorMaskLength)
{
	// Split multi-line string
	size_t stringPos = 0;
	size_t newLinePos = 0;
	size_t processedChars = 0;

	while (stringPos < inputString.length()) {
		std::string s;
		newLinePos = inputString.find("\n", stringPos);
		if (newLinePos == std::string::npos) {
			s = inputString.substr(stringPos);
			stringPos = inputString.length();
		}
		else {
			s = inputString.substr(stringPos, newLinePos - stringPos);
			stringPos = newLinePos + 1;
		}

		if (s.length() == 0) return;
		if (mGameData.stringsCount == MAX_STRINGS_COUNT) {
			utils::validate(E_FAIL, L"You're trying to add more strings than the limit set by 'MAX_STRINGS_COUNT'.");
			return;
		}

		if (mStringDataCurrentOffset + s.length() > kMaxStringDataLength) {
			utils::validate(E_FAIL, L"Combined strings size was above the limit set by 'kMaxStringDataLength'.");
			return;
		}

		mGameData.strings[mGameData.stringsCount].stringLength = (uint32_t)s.length();
		mGameData.strings[mGameData.stringsCount].screenPosition = glm::uvec2(posX, posY);
		mGameData.strings[mGameData.stringsCount].stringStartOffset = (uint32_t)mStringDataCurrentOffset;

		for (size_t i = 0; i < s.length(); i++) {

			int codepoint = (kUseCP437FontAtlas && !stringIsCP437) ? utils::mapCharToCP437(s[i]) : s[i];
			if (codepoint < 0) codepoint += 256;
			if (codepoint > 255) utils::validate(E_FAIL, L"Trying to render unsupported character.");

			uint8_t characterAtlasPosX = codepoint % kFontAtlasCharactersPerSide;
			uint8_t characterAtlasPosY = codepoint / kFontAtlasCharactersPerSide;

			// Encode highlight color as highest but in characterAtlasPosY
			if (highlightColorMask != nullptr) {
				if (highlightColorMask[(processedChars + i) % highlightColorMaskLength]) characterAtlasPosY |= (1 << 7);
			}

			mStringData[mStringDataCurrentOffset++] = characterAtlasPosX + (characterAtlasPosY << 8);
		}

		// Pre-multiply alpha
		mGameData.strings[mGameData.stringsCount].fontColor = glm::vec4(fontColor.x * fontColor.a, fontColor.y * fontColor.a, fontColor.z * fontColor.a, fontColor.a);
		mGameData.strings[mGameData.stringsCount].backgroundColor = glm::vec4(bgColor.x * bgColor.a, bgColor.y * bgColor.a, bgColor.z * bgColor.a, bgColor.a);
		mGameData.strings[mGameData.stringsCount].highlightColor = glm::vec4(highlightColor.x * highlightColor.a, highlightColor.y * highlightColor.a, highlightColor.z * highlightColor.a, highlightColor.a);

		processedChars = stringPos;
		mGameData.stringsCount++;
		posY += mGameData.characterSize.y;
	}
}

void Renderer::AddString(const std::string& s, float posX, float posY, glm::vec4 fontColor, glm::vec4 bgColor, bool stringIsCP437, glm::vec4 highlightColor, bool* highlightColorMask, size_t highlightColorMaskLength)
{
	AddString(s, unsigned int(posX * frameWidth), unsigned int(posY * frameHeight), fontColor, bgColor, stringIsCP437, highlightColor, highlightColorMask, highlightColorMaskLength);
}

void Renderer::createStringBuffer()
{
	SAFE_DELETE_ARRAY(mStringData);
	mStringData = new uint32_t[kMaxStringDataLength];

	// Allocate string buffer
	createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, 0, kMaxStringDataLength * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &mStringBuffer);

	// Create upload heap
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mStringBuffer, 0, 1);
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, uploadBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &mStringUploadHeap);

	// Create SRV for buffer
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = kMaxStringDataLength;
		srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		mDevice->CreateShaderResourceView(mStringBuffer, &srvDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::StringsBuffer)));
	}
}

void Renderer::updateStringBuffer()
{
	transitionBarrier(mStringBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_SUBRESOURCE_DATA stringDataDesc = {};
	stringDataDesc.pData = mStringData;
	stringDataDesc.RowPitch = kMaxStringDataLength * sizeof(uint32_t);
	stringDataDesc.SlicePitch = stringDataDesc.RowPitch;

	UpdateSubresources(mCmdList, mStringBuffer, mStringUploadHeap, 0, 0, 1, &stringDataDesc);
	transitionBarrier(mStringBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

unsigned int Renderer::GetCenteredTextX(const char* buffer, size_t bufferLength, unsigned int characterWidth, unsigned int frameWidth)
{
	size_t numChars = strnlen_s(buffer, bufferLength);
	return (frameWidth - unsigned int(numChars) * characterWidth) / 2;
}

void Renderer::GetCharacterSize(int& characterWidth, int& characterHeight)
{
	 characterWidth = mGameData.characterSize.x;
	 characterHeight = mGameData.characterSize.y;
}

void Renderer::SetColorGrading(glm::vec3 crtColor, bool invertDisplay)
{
	mGameData.colorBalance = crtColor;
	mGameData.invertColors = invertDisplay ? 1 : 0;
}

ID3D12PipelineState* Renderer::createComputePSO(IDxcBlob& shaderBlob, ID3D12RootSignature* rootSignature)
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

void Renderer::moveToNextFrame()
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

void Renderer::submitCmdList()
{
	mCmdList->Close();

	ID3D12CommandList* pGraphicsList = { mCmdList };
	mCmdQueue->ExecuteCommandLists(1, &pGraphicsList);
	mFenceValues[mCurrentFrameIndex]++;
	mCmdQueue->Signal(mFence, mFenceValues[mCurrentFrameIndex]);
}

void Renderer::present()
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

void Renderer::waitForGPU()
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

void Renderer::resetCommandList()
{
	// Reset the command allocator for the current frame
	HRESULT hr = mCmdAlloc[mCurrentFrameIndex]->Reset();
	utils::validate(hr, L"Error: failed to reset command allocator!");

	// Reset the command list for the current frame
	hr = mCmdList->Reset(mCmdAlloc[mCurrentFrameIndex], nullptr);
	utils::validate(hr, L"Error: failed to reset command list!");
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::getBackBufferView(UINT bufferIndex) {

	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	renderTargetViewHandle.ptr += (mRtvDescSize * bufferIndex);

	return renderTargetViewHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::getCurrentBackBufferView() {
	return getBackBufferView(mCurrentFrameIndex);
}

void Renderer::transitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = from;
	barrier.Transition.StateAfter = to;

	mCmdList->ResourceBarrier(1, &barrier);
}

void Renderer::uavBarrier(ID3D12Resource* resource) {
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.UAV.pResource = resource;

	mCmdList->ResourceBarrier(1, &barrier);
}

void Renderer::uavBarrier(ID3D12Resource** resources, int resourceCount) {
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

void Renderer::dispatchCompute2D(ID3D12PipelineState* pso, uint32_t dispatchWidth, uint32_t dispatchHeight)
{
	if (dispatchWidth > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION || dispatchHeight > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)
	{
		utils::validate(E_FAIL, L"Error: dispatch size too large!");
	}

	mCmdList->SetPipelineState(pso);
	mCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::getDescriptorHandle(UINT index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += (mCbvSrvUavDescSize * index);
	return handle;
}

ID3D12RootSignature* Renderer::createGlobalRootSignature() {

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

ID3D12RootSignature* Renderer::createRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc) {
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

void Renderer::uploadConstantBuffer()
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

void Renderer::createBuffer(ID3D12Device* device, D3D12_HEAP_TYPE heapType, UINT64 alignment, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ID3D12Resource** ppResource)
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

void Renderer::createTexture(ID3D12Device* device, UINT64 width, UINT64 height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ID3D12Resource** ppResource)
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

void Renderer::uploadTexture(ID3D12Resource* texture, void* texels, UINT width, UINT height, UINT bytesPerTexel)
{
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture, 0, 1);

	// Create the upload heap
	ID3D12Resource* textureUploadBuffer = nullptr;
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, uploadBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &textureUploadBuffer);

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = texels;
	textureData.RowPitch = width * bytesPerTexel;
	textureData.SlicePitch = textureData.RowPitch * height;

	// Schedule a copy from the upload heap to the Texture2D resource
	UpdateSubresources(mCmdList, texture, textureUploadBuffer, 0, 0, 1, &textureData);

	transitionBarrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	// TODO: release textureUploadBuffer
}

void Renderer::createBuffers()
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

	// Strings rendering buffers
	{
		createStringBuffer();
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
			mDevice->CreateUnorderedAccessView(mDrawingBuffer, nullptr, &uavDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::DrawingBuffer)));
		}
	}
}

void Renderer::createComputePasses()
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
	compilerFlags.push_back(L"/D HALF_PI=" + std::to_wstring(glm::half_pi<float>()));
	compilerFlags.push_back((L"/D RT_WINDOW_BUFFERS_COUNT=" + std::to_wstring(kRtWindowBuffersCount)).c_str());
	compilerFlags.push_back((L"/D RT_RAW_OUTPUT_INDEX=" + std::to_wstring(int(RtWindowBuffers::RAW_OUTPUT))).c_str());
	compilerFlags.push_back((L"/D RT_TEMP1_INDEX=" + std::to_wstring(int(RtWindowBuffers::TEMP1))).c_str());
	compilerFlags.push_back((L"/D RT_TEMP2_INDEX=" + std::to_wstring(int(RtWindowBuffers::TEMP2))).c_str());
	compilerFlags.push_back((L"/D NORMAL_MATERIAL_DEPTH_INDEX=" + std::to_wstring(int(RtWindowBuffers::NORMAL_MATERIAL_DEPTH))).c_str());
	compilerFlags.push_back((L"/D HISTORY_LENGTH_INDEX=" + std::to_wstring(int(RtWindowBuffers::HISTORY_LENGTH))).c_str());
	compilerFlags.push_back((L"/D DISOCCLUSIONS_INDEX=" + std::to_wstring(int(RtWindowBuffers::DISOCCLUSIONS))).c_str());
	compilerFlags.push_back((L"/D MAX_BOUNCES=" + std::to_wstring(4)).c_str());
	compilerFlags.push_back((L"/D PRIMARY_TMIN=" + std::to_wstring(0.001f)).c_str());
	compilerFlags.push_back((L"/D PRIMARY_TMAX=" + std::to_wstring(1000.0f)).c_str());

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

	// Create path tracing pass
	{
		IDxcBlob* shaderBlob = mShaderCompiler.CompileShader(gameShaderFile.c_str(), L"", L"lib_6_3", flagsPointers);

		createRaytracingPSO(shaderBlob);
	}

	// Create denoising pass
	{
		IDxcBlob* shaderBlob = mShaderCompiler.CompileShader(gameShaderFile.c_str(), L"Denoise", L"cs_6_2", flagsPointers);

		SAFE_RELEASE(mDenoisingPSO);
		mDenoisingPSO = createComputePSO(*shaderBlob, mGlobalRootSignature);
	}

}

void Renderer::createFontAtlas(int characterHeight, int extraHorizontalSpacing, FileSystem& fileSystem)
{
	// Create atlas using STB Truetype lib
	stbtt_fontinfo font;

	FileHandle fontFile = fileSystem.openFile(L"assets\\MorePerfectDOSVGA.ttf");

	unsigned char* fontFileData = fileSystem.readAll(fontFile);
	stbtt_InitFont(&font, fontFileData, 0);

	// Get font metrics
	float scale;
	int ascent, baseline;
	scale = stbtt_ScaleForPixelHeight(&font, float(characterHeight));
	stbtt_GetFontVMetrics(&font, &ascent, 0, 0);
	baseline = (int)(ascent * scale);

	// Figure out the maximal character width for selected character height
	// !! Hack: Only consider characters with codes up to 127 (not 255). Those are most used
	// and we don't want the weird extra characters in range 128-255 to deform our atlas
	int characterWidth = 0;
	int maxCharacterHeight = 0;
	for (int charIndex = 0; charIndex < 128; charIndex++) {
		int codepoint = kUseCP437FontAtlas ? utils::mapCharToCP437(charIndex) : charIndex;
		int x0, y0, x1, y1;
		stbtt_GetCodepointBitmapBoxSubpixel(&font, codepoint, scale, scale, 0, 0, &x0, &y0, &x1, &y1);
		characterWidth = std::max(characterWidth, x1 - x0 + extraHorizontalSpacing);
		maxCharacterHeight = std::max(maxCharacterHeight, y1 - y0);
	}

	if (characterWidth == 0 || maxCharacterHeight == 0) utils::validate(E_FAIL, L"Couldn't figure out a maximal size of characters in selected font!");

	const int fontAtlasWidth = kFontAtlasCharactersPerSide * characterWidth;
	const int fontAtlasHeight = kFontAtlasCharactersPerSide * characterHeight;

	unsigned char* fontAtlasRawData = new unsigned char[fontAtlasWidth * fontAtlasHeight];

	// Make glyph buffer double the necessary size to support glyphs that extend outside, or aren't centered in their box
	int glyphBufferWidth = characterWidth * 2;
	int glyphBufferHeight = maxCharacterHeight * 2;
	unsigned char* glyphBuffer = new unsigned char[glyphBufferWidth * glyphBufferHeight];

	// Render glyphs from 0 to 255
	for (int charIndex = 0; charIndex < 256; charIndex++) {

		int codepoint = kUseCP437FontAtlas ? utils::mapCharToCP437(charIndex) : charIndex;

		// Clear the glyph data
		memset(glyphBuffer, 0, glyphBufferWidth * glyphBufferHeight);

		// Get glyph metrics (size and origin). Apply shift along X axis if desired
		float xpos = float(extraHorizontalSpacing / 2); //< Shift in X axis. Divide the integer (not float) to prevent subpixel movements that can cause blur
		int advance, lsb, x0, y0, x1, y1;
		float x_shift = xpos - (float)floor(xpos);

		stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &lsb);
		stbtt_GetCodepointBitmapBoxSubpixel(&font, codepoint, scale, scale, x_shift, 0, &x0, &y0, &x1, &y1);
		int glyphWidth = x1 - x0;
		int glyphHeight = y1 - y0;
		int glyphOriginX = (int)xpos + x0;
		int glyphOriginY = baseline + y0;

		// Calculate glyph box position, centered in the glyph buffer (might extend outside)
		int glyphBoxPosX = (glyphBufferWidth - characterWidth) / 2;
		int glyphBoxPosY = (glyphBufferHeight - characterHeight) / 2;

		// Calculate glyph origin in glyph buffer, may be outside of its box (extending to neighbouring characters or lines), but must be inside of temp. glyph buffer
		int glyphBufferOriginX = glyphBoxPosX + glyphOriginX;
		int glyphBufferOriginY = glyphBoxPosY + glyphOriginY;

		if (glyphBufferOriginX < 0 || glyphBufferOriginY < 0) utils::validate(E_FAIL, L"Glyph rendering failed, origin was outside of the temp. buffer!");

		// Rasterize glyph into temp. glpyh buffer
		unsigned char* glyphBufferPtr = glyphBuffer + (glyphBufferOriginY * glyphBufferWidth) + glyphBufferOriginX;
		if (glyphWidth > 0 && glyphHeight > 0) stbtt_MakeCodepointBitmapSubpixel(&font, glyphBufferPtr, glyphWidth, glyphHeight, glyphBufferWidth, scale, scale, x_shift, 0, codepoint);

		// Copy glyph to atlas (or alpha-blend into string texture)
		int glyphAtlasX = (charIndex % kFontAtlasCharactersPerSide) * characterWidth;
		int glyphAtlasY = (charIndex / kFontAtlasCharactersPerSide) * characterHeight;
		int lineWidth = kFontAtlasCharactersPerSide * characterWidth;

		// Copy glyph to atlas, note that this chops off glyph areas that extend out of its box!
		for (int x = 0; x < characterWidth; x++) {
			for (int y = 0; y < characterHeight; y++) {

				auto atlasAddress = (glyphAtlasX + x) + ((glyphAtlasY + y) * lineWidth);
				fontAtlasRawData[atlasAddress] = *(glyphBuffer + (glyphBoxPosY + y) * glyphBufferWidth + glyphBoxPosX + x);

#if 0
				// Draw bounding box for debugging
				if (x == 0 || y == 0 || x == characterWidth - 1 || y == characterHeight - 1) fontAtlasRawData[atlasAddress] = 60;
#endif
			}
		}
	}

	// Remember dimensions of characters in created atlas
	mGameData.characterSize.y = characterHeight;
	mGameData.characterSize.x = characterWidth;

	// Allocate font atlas
	createTexture(mDevice, fontAtlasWidth, fontAtlasHeight, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, &mFontTexture);
	uploadTexture(mFontTexture, fontAtlasRawData, fontAtlasWidth, fontAtlasHeight, 1);

	// Create SRV for font atlas texture
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
		srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		mDevice->CreateShaderResourceView(mFontTexture, &srvDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::FontAtlas)));
	}

	// TODO: release fontAtlasRawData
}

void Renderer::initializeDx12(HWND hwnd, FileSystem& fileSystem)
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

	// Path tracing stuff
	{
		createShaderTable();
		createRtResources(fileSystem);
	}

}

void Renderer::createMaterialsBuffer()
{
	SAFE_DELETE_ARRAY(mMaterialData);
	mMaterialData = new Material[kMaxMaterials];

	// Allocate mMaterialsBuffer
	createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, 0, kMaxMaterials * sizeof(Material), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &mMaterialsBuffer);

	// Create upload heap
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mMaterialsBuffer, 0, 1);
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, uploadBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &mMaterialsUploadHeap);

	// Create SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = kMaxMaterials;
		srvDesc.Buffer.StructureByteStride = sizeof(Material);
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		mDevice->CreateShaderResourceView(mMaterialsBuffer, &srvDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::MaterialsBuffer)));
	}

}

void Renderer::updateMaterialsBuffer(Material* materialData, size_t materialsCount)
{
	if (materialsCount == 0) return;

	if (materialsCount >= kMaxMaterials) {
		utils::validate(E_FAIL, L"You're trying to add more materials than supported limit (kMaxMaterials)");
		return;
	}

	transitionBarrier(mMaterialsBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_SUBRESOURCE_DATA dataDesc = {};
	dataDesc.pData = materialData;
	dataDesc.RowPitch = materialsCount * sizeof(Material);
	dataDesc.SlicePitch = dataDesc.RowPitch;

	UpdateSubresources(mCmdList, mMaterialsBuffer, mMaterialsUploadHeap, 0, 0, 1, &dataDesc);
	transitionBarrier(mMaterialsBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void Renderer::createAllMaterials() {

	mMaterials.clear();

	for (int m = 1; m <= 4; m++) {
		for (int r = 1; r <= 4; r++) {
			for (int c = 0; c < _countof(kBlockColors); c++) {
				Material result;
				result.metalness = float(m) / 5.0f;
				result.roughness = (float(r) / 5.0f) * 0.5f; //< Limit roughness to 0.5
				result.albedo = kBlockColors[c];
				result.emissive = glm::vec3(0.0f, 0.0f, 0.0f);
				result.id = UINT(mMaterials.size()) + 1;
				mMaterials.push_back(result);
			}
		}
	}
}

void Renderer::calculateRTWindowSize()
{
	// Figure out buffer sizes needed for playing field
	int pixelsWidth = (int)(mGameData.characterSize.y * mFieldWidth * mHorizontalStretch);
	mRtFieldCharactersWidth = pixelsWidth / mGameData.characterSize.x + 1;

	mRtFieldPixelsWidth = mRtFieldCharactersWidth * mGameData.characterSize.x;
	mRtFieldPixelsHeight = mFieldHeight * mGameData.characterSize.y;
}

void Renderer::createDisocclusionBuffer()
{
	if (mDisocclusionBufferData != nullptr) delete[] mDisocclusionBufferData;
	mDisocclusionBufferData = new glm::vec4[mRtFieldPixelsWidth * mRtFieldPixelsHeight];

	// Upload to GPU
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mRtWindowBuffers[int(RtWindowBuffers::DISOCCLUSIONS)], 0, 1);
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, uploadBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &mDisocclusionBufferUploadHeap);
}

void Renderer::createVertexBuffer(ID3D12Device* device, UINT64 verticesCount, UINT64 vertexSize, void* vertexData, D3D12_VERTEX_BUFFER_VIEW& vertexBufferView, ID3D12Resource*& vertexBuffer, std::wstring debugName)
{
	// Create the vertex buffer resource
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, verticesCount * vertexSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &vertexBuffer);

	// Copy the vertex data to the vertex buffer
	UINT8* pVertexDataBegin;
	D3D12_RANGE readRange = {};
	HRESULT hr = vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
	utils::validate(hr, L"Error: failed to map vertex buffer!");

	memcpy(pVertexDataBegin, vertexData, verticesCount * vertexSize);
	vertexBuffer->Unmap(0, nullptr);

	// Initialize the vertex buffer view
	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = static_cast<UINT>(vertexSize);
	vertexBufferView.SizeInBytes = static_cast<UINT>(verticesCount * vertexSize);
}

void Renderer::createIndexBuffer(ID3D12Device* device, UINT64 indicesCount, UINT64 indexSize, DXGI_FORMAT indexFormat, void* indexData, D3D12_INDEX_BUFFER_VIEW& indexBufferView, ID3D12Resource*& indexBuffer, std::wstring debugName)
{
	// Create the index buffer resource
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, (UINT)indicesCount * indexSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &indexBuffer);

	// Copy the index data to the index buffer
	UINT8* pIndexDataBegin;
	D3D12_RANGE readRange = {};
	HRESULT hr = indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin));
	utils::validate(hr, L"Error: failed to map index buffer!");

	memcpy(pIndexDataBegin, indexData, indicesCount * indexSize);
	indexBuffer->Unmap(0, nullptr);

	// Initialize the index buffer view
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = static_cast<UINT>(indicesCount * indexSize);
	indexBufferView.Format = indexFormat;
}

void Renderer::createModelBuffers(ModelData* data, D3D12ModelData* d3d12Data, std::wstring debugName)
{
	utils::validate(d3d12Data == nullptr ? E_FAIL : NOERROR, L"Trying to create vertex buffer, but d3d12 model was null");
	utils::validate(data == nullptr ? E_FAIL : NOERROR, L"Trying to create BLAS, but model was null");

	createVertexBuffer(mDevice, data->verticesCount, kVertexSize, data->vertices, d3d12Data->vertexBufferView, d3d12Data->vertexBuffer, debugName + L" Vertex Buffer");
	createIndexBuffer(mDevice, data->indicesCount, kIndexSize, kIndexFormat, (void*)data->indices, d3d12Data->indexBufferView, d3d12Data->indexBuffer, debugName + L" Index Buffer");
}

void Renderer::createModelBuffers(ModelInstance& model, std::wstring debugName)
{
	utils::validate(model.d3d12Data == nullptr ? E_FAIL : NOERROR, L"Trying to create vertex buffer, but d3d12 model was null");
	utils::validate(model.data == nullptr ? E_FAIL : NOERROR, L"Trying to create BLAS, but model was null");

	createModelBuffers(model.data, model.d3d12Data, debugName);
}

std::vector<glm::vec3> Renderer::createNormals(ModelData modelData) {
	std::vector<glm::vec3> result;

	for (size_t i = 0; i < modelData.indicesCount / 3; i++) {
		glm::vec3 a = modelData.vertices[modelData.indices[i * 3 + 0]];
		glm::vec3 b = modelData.vertices[modelData.indices[i * 3 + 1]];
		glm::vec3 c = modelData.vertices[modelData.indices[i * 3 + 2]];

		glm::vec3 ab = glm::normalize(b - a);
		glm::vec3 ac = glm::normalize(c - a);
		glm::vec3 n = glm::normalize(glm::cross(ac, ab));

		result.push_back(n);
	}

	return result;
}

void Renderer::createNormalsBuffer(const std::vector<glm::vec3>& normals) {

	mNormalsBufferLength = (UINT)normals.size();

	// Allocate mNormalsBuffer
	createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, 0, normals.size() * sizeof(glm::vec3), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, &mNormalsBuffer);

	// Create upload heap
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mNormalsBuffer, 0, 1);
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, uploadBufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &mNormalsBufferUploadHeap);

	D3D12_SUBRESOURCE_DATA normalsDataDesc = {};
	normalsDataDesc.pData = normals.data();
	normalsDataDesc.RowPitch = normals.size() * sizeof(glm::vec3);
	normalsDataDesc.SlicePitch = normalsDataDesc.RowPitch;

	// Schedule a copy from the upload heap to the Buffer resource
	UpdateSubresources(mCmdList, mNormalsBuffer, mNormalsBufferUploadHeap, 0, 0, 1, &normalsDataDesc);

	// Transition the texture to a shader resource
	transitionBarrier(mNormalsBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = normals.size();
	srvDesc.Buffer.StructureByteStride = sizeof(glm::vec3);
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	mDevice->CreateShaderResourceView(mNormalsBuffer, &srvDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::NormalsBuffer)));
}

Renderer::AccelerationStructureBuffer Renderer::createBottomLevelAS(ID3D12Resource* vertexBuffer, UINT verticesCount, UINT64 VertexBufferStrideInBytes, ID3D12Resource* indexBuffer, UINT indicesCount, DXGI_FORMAT indexBufferFormat)
{
	AccelerationStructureBuffer resultBLAS = {};

	// Describe the geometry that goes in the bottom acceleration structure(s)
	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc;
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer->GetGPUVirtualAddress();
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = VertexBufferStrideInBytes;
	geometryDesc.Triangles.VertexCount = verticesCount;
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.IndexBuffer = indexBuffer->GetGPUVirtualAddress();
	geometryDesc.Triangles.IndexFormat = indexBufferFormat;
	geometryDesc.Triangles.IndexCount = indicesCount;
	geometryDesc.Triangles.Transform3x4 = 0;
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	// Get the size requirements for the BLAS buffers
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
	ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	ASInputs.pGeometryDescs = &geometryDesc;
	ASInputs.NumDescs = 1;
	ASInputs.Flags = buildFlags;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

	ASPreBuildInfo.ScratchDataSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ScratchDataSizeInBytes);
	ASPreBuildInfo.ResultDataMaxSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ResultDataMaxSizeInBytes);

	// Create the BLAS scratch buffer
	createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), ASPreBuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &resultBLAS.pScratch);

	// Create the BLAS buffer
	createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), ASPreBuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, &resultBLAS.pResult);

	// Describe and build the bottom level acceleration structure
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = ASInputs;
	buildDesc.ScratchAccelerationStructureData = resultBLAS.pScratch->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = resultBLAS.pResult->GetGPUVirtualAddress();

	mCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Wait for the BLAS build to complete
	D3D12_RESOURCE_BARRIER uavBarrier;
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = resultBLAS.pResult;
	uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	mCmdList->ResourceBarrier(1, &uavBarrier);

	return resultBLAS;
}

void Renderer::createBottomLevelAS(ModelData* data, D3D12ModelData* d3d12Data) {
	utils::validate(d3d12Data == nullptr ? E_FAIL : NOERROR, L"Trying to create BLAS, but d3d12 model was null");
	utils::validate(data == nullptr ? E_FAIL : NOERROR, L"Trying to create BLAS, but model was null");
	d3d12Data->asBuffer = createBottomLevelAS(d3d12Data->vertexBuffer, (UINT)data->verticesCount, d3d12Data->vertexBufferView.StrideInBytes, d3d12Data->indexBuffer, (UINT)data->indicesCount, d3d12Data->indexBufferView.Format);
}

void Renderer::createBottomLevelAS(ModelInstance& model) {
	utils::validate(model.d3d12Data == nullptr ? E_FAIL : NOERROR, L"Trying to create BLAS, but d3d12 model was null");
	utils::validate(model.data == nullptr ? E_FAIL : NOERROR, L"Trying to create BLAS, but model was null");
	model.d3d12Data->asBuffer = createBottomLevelAS(model.d3d12Data->vertexBuffer, (UINT)model.data->verticesCount, model.d3d12Data->vertexBufferView.StrideInBytes, model.d3d12Data->indexBuffer, (UINT)model.data->indicesCount, model.d3d12Data->indexBufferView.Format);
}

void Renderer::createRtResources(FileSystem& fileSystem)
{	
	mIsFirstFrame = true;

	createFontAtlas(32, 1, fileSystem);
	createMaterialsBuffer();
	calculateRTWindowSize();

	for (int i = 0; i < kRtWindowBuffersCount; i++) {
		SAFE_RELEASE(mRtWindowBuffers[i]);
		createTexture(mDevice, mRtFieldPixelsWidth, mRtFieldPixelsHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &mRtWindowBuffers[i]);

		// Create UAV
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			mDevice->CreateUnorderedAccessView(mRtWindowBuffers[i], nullptr, &uavDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::RAW_OUTPUT) + i));
		}
	}

	createDisocclusionBuffer();
	createAllMaterials();

	// Prepare caches for AS builds
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 1024, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &mTLASInstanceDescriptorsCache);
	createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), 4 * 1024 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &mTLASScratchBuffersCache);
	createBuffer(mDevice, D3D12_HEAP_TYPE_DEFAULT, std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT), 4 * 1024 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, &mTLASResultsCache);

	// Create vertex and index buffers for our models
	mCubeModelData.indices = cubeIndices;
	mCubeModelData.vertices = cubeVertices;
	mCubeModelData.verticesCount = _countof(cubeVertices);
	mCubeModelData.indicesCount = _countof(cubeIndices);

	createModelBuffers(&mCubeModelData, &mCubeModelD3DData, L"Cube Model");
	createNormalsBuffer(createNormals(mCubeModelData));

	// Build BLASes
	createBottomLevelAS(&mCubeModelData, &mCubeModelD3DData);

	// Prepare arena instance
	Material white = { glm::vec3(1, 1, 1), 0.1f, glm::vec3(0, 0, 0), 0.5f, 0 };
	mArenaInstance.d3d12Data = &mCubeModelD3DData;
	mArenaInstance.data = &mCubeModelData;
	mArenaInstance.instanceId = 0;
	mArenaInstance.rtMask = 0xFF;
	mArenaInstance.transform = glm::mat3x4(0.0f);
	mArenaInstance.transform[0][0] = 20;
	mArenaInstance.transform[1][1] = 20;
	mArenaInstance.transform[2][2] = -2;
	mArenaInstance.material = white;
}

void Renderer::createShaderTable()
{
	/*
	The Shader Table layout is as follows:
		Entry 0 - Ray Generation shader
		Entry 1 - Miss shader
		Entry 2 - Closest Hit shader
	All shader records in the Shader Table must have the same size, so shader record size will be based on the largest required entry.
	The entry size must be aligned up to D3D12_RAYTRACING_SHADER_BINDING_TABLE_RECORD_BYTE_ALIGNMENT
	*/

	uint32_t shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	uint32_t shaderTableSize = 0;

	mShaderTableRecordSize = shaderIdSize;

	// TODO: desc.MissShaderTable.StartAddress has to be aligned to 64. We just force mShaderTableRecordSize to be aligned 
	// to 64 as well, instead of 32. This is wasteful, but ok for now.
	mShaderTableRecordSize = ALIGN(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, mShaderTableRecordSize);
	//mShaderTableRecordSize = ALIGN(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, mShaderTableRecordSize);

	shaderTableSize = (mShaderTableRecordSize * 3);		// 3 shader records in the table
	shaderTableSize = ALIGN(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, shaderTableSize);

	// Create the shader table buffer
	createBuffer(mDevice, D3D12_HEAP_TYPE_UPLOAD, 0, shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, &mShaderTable);

	// Map the buffer
	uint8_t* pData;
	HRESULT hr = mShaderTable->Map(0, nullptr, (void**)&pData);
	utils::validate(hr, L"Error: failed to map shader table!");

	// Shader Record 0 - Ray Generation program and local root parameter data (descriptor table with constant buffer and IB/VB pointers)
	memcpy(pData, mRTPSOInfo->GetShaderIdentifier(L"RayGen"), shaderIdSize);

	// Shader Record 1 - Miss program (no local root arguments to set)
	pData += mShaderTableRecordSize;
	memcpy(pData, mRTPSOInfo->GetShaderIdentifier(L"Miss"), shaderIdSize);

	// Shader Record 2 - Closest Hit program and local root parameter data (descriptor table with constant buffer and IB/VB pointers)
	pData += mShaderTableRecordSize;
	memcpy(pData, mRTPSOInfo->GetShaderIdentifier(L"HitGroup"), shaderIdSize);

	// Unmap
	mShaderTable->Unmap(0, nullptr);
}

void Renderer::createRaytracingPSO(IDxcBlob* programBlob)
{
	// Need 8 subobjects:
	// 1 for RGS program
	// 1 for Miss program
	// 1 for CHS program
	// 1 for Hit Group
	// 2 for Shader Config (config and association)
	// 1 for Global Root Signature
	// 1 for Pipeline Config	
	UINT index = 0;
	std::vector<D3D12_STATE_SUBOBJECT> subobjects;
	subobjects.resize(8);

	// Add state subobject for the RGS
	D3D12_EXPORT_DESC rgsExportDesc = {};
	rgsExportDesc.Name = L"RayGen";
	rgsExportDesc.ExportToRename = L"RayGen";
	rgsExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	rgsLibDesc = {};
	rgsLibDesc.DXILLibrary.BytecodeLength = programBlob->GetBufferSize();
	rgsLibDesc.DXILLibrary.pShaderBytecode = programBlob->GetBufferPointer();
	rgsLibDesc.NumExports = 1;
	rgsLibDesc.pExports = &rgsExportDesc;

	D3D12_STATE_SUBOBJECT rgs = {};
	rgs.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	rgs.pDesc = &rgsLibDesc;

	subobjects[index++] = rgs;

	// Add state subobject for the Miss shader
	D3D12_EXPORT_DESC msExportDesc = {};
	msExportDesc.Name = L"Miss";
	msExportDesc.ExportToRename = L"Miss";
	msExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	msLibDesc = {};
	msLibDesc.DXILLibrary.BytecodeLength = programBlob->GetBufferSize();
	msLibDesc.DXILLibrary.pShaderBytecode = programBlob->GetBufferPointer();
	msLibDesc.NumExports = 1;
	msLibDesc.pExports = &msExportDesc;

	D3D12_STATE_SUBOBJECT ms = {};
	ms.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	ms.pDesc = &msLibDesc;

	subobjects[index++] = ms;

	// Add state subobject for the Closest Hit shader
	D3D12_EXPORT_DESC chsExportDesc = {};
	chsExportDesc.Name = L"ClosestHit";
	chsExportDesc.ExportToRename = L"ClosestHit";
	chsExportDesc.Flags = D3D12_EXPORT_FLAG_NONE;

	D3D12_DXIL_LIBRARY_DESC	chsLibDesc = {};
	chsLibDesc.DXILLibrary.BytecodeLength = programBlob->GetBufferSize();
	chsLibDesc.DXILLibrary.pShaderBytecode = programBlob->GetBufferPointer();
	chsLibDesc.NumExports = 1;
	chsLibDesc.pExports = &chsExportDesc;

	D3D12_STATE_SUBOBJECT chs = {};
	chs.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	chs.pDesc = &chsLibDesc;

	subobjects[index++] = chs;

	// Add a state subobject for the hit group
	D3D12_HIT_GROUP_DESC hitGroupDesc = {};
	hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
	hitGroupDesc.HitGroupExport = L"HitGroup";

	D3D12_STATE_SUBOBJECT hitGroup = {};
	hitGroup.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
	hitGroup.pDesc = &hitGroupDesc;

	subobjects[index++] = hitGroup;

	// Add a state subobject for the shader payload configuration
	D3D12_RAYTRACING_SHADER_CONFIG shaderDesc = {};
	shaderDesc.MaxPayloadSizeInBytes = 64;
	shaderDesc.MaxAttributeSizeInBytes = 8;

	D3D12_STATE_SUBOBJECT shaderConfigObject = {};
	shaderConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	shaderConfigObject.pDesc = &shaderDesc;

	subobjects[index++] = shaderConfigObject;

	// Create a list of the shader export names that use the payload
	const WCHAR* shaderExports[] = { L"RayGen", L"Miss", L"HitGroup" };

	// Add a state subobject for the association between shaders and the payload
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderPayloadAssociation = {};
	shaderPayloadAssociation.NumExports = _countof(shaderExports);
	shaderPayloadAssociation.pExports = shaderExports;
	shaderPayloadAssociation.pSubobjectToAssociate = &subobjects[(index - 1)];

	D3D12_STATE_SUBOBJECT shaderPayloadAssociationObject = {};
	shaderPayloadAssociationObject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
	shaderPayloadAssociationObject.pDesc = &shaderPayloadAssociation;

	subobjects[index++] = shaderPayloadAssociationObject;

	// Create a list of the shader export names that use the root signature
	const WCHAR* rootSigExports[] = { L"RayGen", L"HitGroup", L"Miss" };

	D3D12_STATE_SUBOBJECT globalRootSig;
	globalRootSig.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	globalRootSig.pDesc = &mGlobalRootSignature;

	subobjects[index++] = globalRootSig;

	// Add a state subobject for the ray tracing pipeline config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
	pipelineConfig.MaxTraceRecursionDepth = 10; //< TODO: Actual required value

	D3D12_STATE_SUBOBJECT pipelineConfigObject = {};
	pipelineConfigObject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	pipelineConfigObject.pDesc = &pipelineConfig;

	subobjects[index++] = pipelineConfigObject;

	// Describe the Ray Tracing Pipeline State Object
	D3D12_STATE_OBJECT_DESC pipelineDesc = {};
	pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	pipelineDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
	pipelineDesc.pSubobjects = subobjects.data();

	// Create the RT Pipeline State Object (RTPSO)
	HRESULT hr = mDevice->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&mRTPSO));
	utils::validate(hr, L"Error: failed to create state object!");

	// Get the RTPSO properties
	hr = mRTPSO->QueryInterface(IID_PPV_ARGS(&mRTPSOInfo));
	utils::validate(hr, L"Error: failed to get RTPSO info object!");
}

void Renderer::updateRayTracing()
{
	// Process model instances
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> rtInstances;
	mMaterialDataCurrentOffset = 0;

	// Always put arena as the first instance
	rtInstances.push_back(createRTInstanceDesc(mArenaInstance, mMaterialData, mMaterialDataCurrentOffset));
	for (auto i : mInstances) rtInstances.push_back(createRTInstanceDesc(i, mMaterialData, mMaterialDataCurrentOffset));

	// Build TLAS
	createTopLevelAS(rtInstances);

	// Create TLAS SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = mTlasGPUAddress;

	mDevice->CreateShaderResourceView(nullptr, &srvDesc, getDescriptorHandle(UINT(DescriptorHeapConstants::TLAS)));

	// Upload materials to GPU
	updateMaterialsBuffer(mMaterialData, mMaterialDataCurrentOffset);

	updateBlockDifferencesList();
}

void Renderer::createTopLevelAS(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances)
{
	uint32_t instanceBufferSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * (uint32_t)instances.size();
	D3D12_GPU_VIRTUAL_ADDRESS instanceBufferGPUAddress = mTLASInstanceDescriptorsCache->GetGPUVirtualAddress();

	// Copy the instance data to the buffer
	D3D12_RANGE instanceBufferRange;
	instanceBufferRange.Begin = 0;
	instanceBufferRange.End = instanceBufferRange.Begin + instanceBufferSize;

	UINT8* pData;
	mTLASInstanceDescriptorsCache->Map(0, &instanceBufferRange, (void**)&pData);
	memcpy(pData, instances.data(), instanceBufferSize);
	mTLASInstanceDescriptorsCache->Unmap(0, &instanceBufferRange);

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	// Get the size requirements for the TLAS buffers
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
	ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	ASInputs.InstanceDescs = instanceBufferGPUAddress;
	ASInputs.NumDescs = (UINT)instances.size();
	ASInputs.Flags = buildFlags;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

	ASPreBuildInfo.ResultDataMaxSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ResultDataMaxSizeInBytes);
	ASPreBuildInfo.ScratchDataSizeInBytes = ALIGN(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ScratchDataSizeInBytes);

	// Set TLAS size
	mTlasSize = ASPreBuildInfo.ResultDataMaxSizeInBytes;
	mTlasGPUAddress = mTLASResultsCache->GetGPUVirtualAddress();

	// Describe and build the TLAS
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = ASInputs;
	buildDesc.ScratchAccelerationStructureData = mTLASScratchBuffersCache->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = mTlasGPUAddress;

	mCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Wait for the TLAS build to complete
	D3D12_RESOURCE_BARRIER uavBarrier;
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = mTLASResultsCache;
	uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	mCmdList->ResourceBarrier(1, &uavBarrier);
}

void Renderer::updateBlockDifferencesList() {

	// Transition the texture to a shader resource
	transitionBarrier(mRtWindowBuffers[int(RtWindowBuffers::DISOCCLUSIONS)], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = mDisocclusionBufferData;
	textureData.RowPitch = mRtFieldPixelsWidth * 16;
	textureData.SlicePitch = textureData.RowPitch * mRtFieldPixelsHeight;

	// Schedule a copy from the upload heap to the Texture2D resource
	UpdateSubresources(mCmdList, mRtWindowBuffers[int(RtWindowBuffers::DISOCCLUSIONS)], mDisocclusionBufferUploadHeap, 0, 0, 1, &textureData);

	// Transition the texture to a shader resource
	transitionBarrier(mRtWindowBuffers[int(RtWindowBuffers::DISOCCLUSIONS)], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

D3D12_RAYTRACING_INSTANCE_DESC Renderer::createRTInstanceDesc(const ModelInstance& model) {

	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
	instanceDesc.InstanceID = model.instanceId;
	instanceDesc.InstanceContributionToHitGroupIndex = 0;
	instanceDesc.InstanceMask = model.rtMask;
	memcpy(&instanceDesc.Transform[0][0], &model.transform[0][0], sizeof(glm::mat3x4));
	instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
	instanceDesc.AccelerationStructure = model.d3d12Data->asBuffer.pResult->GetGPUVirtualAddress();

	return instanceDesc;
}

D3D12_RAYTRACING_INSTANCE_DESC Renderer::createRTInstanceDesc(const ModelInstance& model, std::vector<Material>& materials) {

	// Push instance material to correct place in the buffer
	if (materials.size() <= model.instanceId) materials.resize(model.instanceId + 1);
	materials[model.instanceId] = model.material;

	return createRTInstanceDesc(model);
}

D3D12_RAYTRACING_INSTANCE_DESC Renderer::createRTInstanceDesc(const ModelInstance& model, Material* materials, size_t& materialsOffset) {

	// Push instance material to correct place in the buffer
	if (model.instanceId >= kMaxMaterials) utils::validate(E_FAIL, L"Your instance ID was too large due to materials count limit");

	materialsOffset = std::max(materialsOffset, size_t(model.instanceId + 1));
	materials[model.instanceId] = model.material;

	return createRTInstanceDesc(model);
}

void Renderer::processChangedBlocksListToDisocclusionMap(std::vector<glm::ivec2>& differences) {

	glm::ivec2 blockSize = glm::ivec2(mRtFieldPixelsWidth / float(mFieldWidth), mRtFieldPixelsHeight / float(mFieldHeight));
	memset(mDisocclusionBufferData, 0, mRtFieldPixelsHeight * mRtFieldPixelsWidth * 4 * 4);

	for (auto uv : differences) {

		uv.x *= blockSize.x;
		uv.y *= blockSize.y;

		for (int i = uv.x - 3 * blockSize.x; i < uv.x + 4 * blockSize.x; i++) {
			for (int j = uv.y - 3 * blockSize.y; j < uv.y + 4 * blockSize.y; j++) {

				if (i >= 0 && i < int(mRtFieldPixelsWidth) && j >= 0 && j < int(mRtFieldPixelsHeight)) {
					mDisocclusionBufferData[j * mRtFieldPixelsWidth + i] = glm::vec4(1, 1, 1, 1);
				}
			}
		}
	}
}
