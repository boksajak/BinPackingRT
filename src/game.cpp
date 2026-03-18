#include "game.h"
#include "utils.h"

void Game::Initialize(HWND hwnd)
{
	// Init settings
	mEnableSounds = true;
	mDesiredPhosphor = mSelectedPhosphor;
	mInvertDisplay = false;
	mDesiredInvertDisplay = mInvertDisplay;

	mRenderer.SetColorGrading(crtPhosphorColors[int(mSelectedPhosphor)], mInvertDisplay);

#ifdef _DEBUG
	mFileSystem.initDevelopment();
#else
	mFileSystem.initUseBigFile(L"stuff.bin");
#endif

	mRenderer.Initialize(hwnd, mFileSystem, kFieldWidth, kFieldHeight);
	mAudio.Initialize(hwnd);

	// Load sounds
	for (int i = 0; i < _countof(mSoundFiles); i++)
	{
		mSoundHandles[i] = mAudio.LoadSound(mFileSystem, L"assets\\" + mSoundFiles[i].second);
	}

	initializeGame();
}

void Game::KeyDown(WPARAM wParam) {

	switch (wParam) {
	case VK_ADD:
		userInput = UserInput::LEVEL_UP;
		return;
	case VK_SUBTRACT:
		userInput = UserInput::LEVEL_DOWN;
		return;
	case VK_ESCAPE:
		userInput = UserInput::PAUSE;
		break;
	case 80: // P
		mRTOn = !mRTOn;
		return;
	case 112: // p
		mRTOn = !mRTOn;
		return;
	}

	if (mGameState != GameState::RUNNING)
		userInput = UserInput::START_GAME;
}

void Game::ReloadShaders() 
{
	mRenderer.ReloadShaders();
}

void Game::getUserInput()
{
	if (mGameState != GameState::RUNNING) return;

	const float kInputTimeStep = 50;
	if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - mLastUserInputTime).count() < kInputTimeStep) return;

	if (GetKeyState(VK_LEFT) & 0x8000)
		userInput = UserInput::LEFT;
	else if (GetKeyState(VK_RIGHT) & 0x8000)
		userInput = UserInput::RIGHT;
	else if ((GetKeyState(VK_SPACE) & 0x8000) || (GetKeyState(VK_DOWN) & 0x8000))
		userInput = UserInput::DROP;
	//else if (GetKeyState(VK_ESCAPE) & 0x8000)
	//	userInput = UserInput::PAUSE;
	else if (GetKeyState(VK_RETURN) & 0x8000)
		userInput = UserInput::ROTATE;
	else if ((GetKeyState(77) & 0x8000) || (GetKeyState(109) & 0x8000))
		userInput = UserInput::TOGGLE_MUTE;
	else if ((GetKeyState(VK_CONTROL) & 0x8000) && ((GetKeyState(90) & 0x8000) || (GetKeyState(122) & 0x8000)))
		userInput = UserInput::UNDO;

	if (userInput != UserInput::NOTHING)
		mConsecutiveUserInputsCount++;
	else
		mConsecutiveUserInputsCount = 0;

	// Disable repeat for certain actions
	if (mConsecutiveUserInputsCount > 1 && userInput == UserInput::ROTATE)
		userInput = UserInput::NOTHING;

	if (mConsecutiveUserInputsCount > 1 && userInput == UserInput::UNDO)
		userInput = UserInput::NOTHING;

	if (mConsecutiveUserInputsCount > 1 && userInput == UserInput::TOGGLE_MUTE)
		userInput = UserInput::NOTHING;

	// Pause after first key stroke for one game tick to prevent unwanted repetition
	if (mConsecutiveUserInputsCount == 2 && mLastUserInput == userInput)
		userInput = UserInput::NOTHING;

	mLastUserInput = userInput;
	mLastUserInputTime = std::chrono::high_resolution_clock::now();

}

void Game::updateGamePlay(long long elapsedTime)
{
	getUserInput();

	switch (mGameState) {
	case GameState::PAUSE:
		if (userInput == UserInput::START_GAME) {
			mGameState = GameState::RUNNING;
		}
		return;
	case GameState::GAME_OVER_WAIT:
	{
		const float kWaitTime = 1500;
		if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - mGameOverTime).count() > kWaitTime) mGameState = GameState::GAME_OVER;
	}
	break;
	case GameState::GAME_OVER:
		if (userInput == UserInput::START_GAME) {
			mLevel = 1;
			mScore = 0;
			mRemovedLinesOnThisLevel = 0;
			mUndosLeft = 3;
			mGameState = GameState::NOT_STARTED;
			clearPlayingField();
			mActiveBlock = nullptr;
		}
		break;
	case GameState::NOT_STARTED:
		if (userInput == UserInput::START_GAME) {
			clearPlayingField();
			pickNextBlock();
			mGameTime = 0;
			mRemovedLinesOnThisLevel = 0;
			mScore = 0;
			mUndosLeft = 3;
			mWasLevelUp = false;
			mGameState = GameState::RUNNING;
			sound(SoundEffect::GAME_START);
		}
		else if (userInput == UserInput::LEVEL_UP) {
			mLevel++;
			sound(SoundEffect::BLOCK_MOVE);
		}
		else if (userInput == UserInput::LEVEL_DOWN) {
			mLevel--;
			sound(SoundEffect::BLOCK_MOVE);
			if (mLevel == 0) mLevel = 1;
		}
		break;
	case GameState::RUNNING:
		mGameTime += elapsedTime;

		// Set game speed based on difficulty level
		const long long gameTickLength = long long(1 * 1000000 / (1 + 0.5 * double(mLevel)));

		if (mGameTime >= gameTickLength) {

			if (mActiveBlock != nullptr) {
				if (canBlockFall()) {
					moveBlock(glm::vec2(0, 1));
				}
				else {
					copyBlockToField();
					sound(SoundEffect::BLOCK_PLACED);
					mActiveBlock = nullptr;
				}
			}
			else 
			{
				if (!removeLines()) {
					if (!placeNewBlock()) {
						mGameState = GameState::GAME_OVER_WAIT;
						sound(SoundEffect::GAME_OVER);
						mGameOverTime = std::chrono::high_resolution_clock::now();
					}
					else {
						if (mWasLevelUp) sound(SoundEffect::LEVEL_UP);
						mWasLevelUp = false;
					}
				}
				else {
					sound(SoundEffect::LINES_REMOVED);
				}
			}

			mGameTime = 0;
		}

		if (mActiveBlock) {
			switch (userInput) {
			case UserInput::LEFT:
				if (checkBlockCollision(glm::ivec2(-1, 0), blockRotation)) moveBlock(glm::vec2(-1, 0));
				break;
			case UserInput::RIGHT:
				if (checkBlockCollision(glm::ivec2(1, 0), blockRotation)) moveBlock(glm::vec2(1, 0));
				break;
			case UserInput::DROP:
				if (checkBlockCollision(glm::ivec2(0, 1), blockRotation)) moveBlock(glm::vec2(0, 1));
				break;
			case UserInput::ROTATE:
				BlockRotation newRotation = BlockRotation((int(blockRotation) + 1) % int(BlockRotation::COUNT));
				if (checkBlockCollision(glm::ivec2(0, 0), newRotation)) {
					blockRotation = newRotation;
					sound(SoundEffect::BLOCK_ROTATES);
				}
				break;
			}
		}

		if (userInput == UserInput::UNDO && mUndosLeft > 0) {
			if (!lastPlacedBlock.empty()) {
				for (auto x : lastPlacedBlock) *x = false;
				lastPlacedBlock.clear();
				mUndosLeft--;
			}
		}
		else if (userInput == UserInput::PAUSE && mGameTime > 100000) {
			mGameState = GameState::PAUSE;
		}

		if (userInput == UserInput::TOGGLE_MUTE) mEnableSounds = !mEnableSounds;

		break;
	}

	userInput = UserInput::NOTHING;
}

bool Game::Update(HWND hwnd, const float elapsedTime)
{
	// Set margins and layout parameters
	float top = 0.05f;
	float left = 0.05f;
	float right = 0.05f;
	float bottom = 0.05f;

	int characterWidth;
	int characterHeight;
	mRenderer.GetCharacterSize(characterWidth, characterHeight);
	const float characterWidthRelative = float(characterWidth) / frameWidth;
	const float characterHeightRelative = float(characterHeight) / frameHeight;

	const int gameStatsCharactersOffset = 2;
	unsigned int playingFieldWidth = mRTOn ? mRtFieldCharactersWidth + 2 : kFieldWidth + 2;
	const int mainAreaWidthCharacters = (playingFieldWidth + 2) + gameStatsCharactersOffset + (kRemovedLinesToLevelUp + 2);
	const float playingFieldLeft = (1.0f - (mainAreaWidthCharacters * characterWidthRelative)) * 0.5f;
	const float playingFieldTop = top + 4 * characterHeightRelative;
	const float playingFieldBottom = playingFieldTop + (kFieldHeight + 2) * characterHeightRelative;
	const float playingFieldRight = playingFieldLeft + (playingFieldWidth + 2) * characterWidthRelative;

	unsigned int rtFieldPosX = (unsigned int)glm::floor(playingFieldLeft * float(frameWidth) + characterWidth);
	unsigned int rtFieldPosY = (unsigned int)glm::floor(playingFieldTop * float(frameHeight) + characterHeight);

	// Prepare game GUI
	{
		// Update the gameplay
		updateGamePlay(elapsedTime * 1000.0f);

		// Change phospohor type
		if (mDesiredPhosphor != mSelectedPhosphor || mDesiredInvertDisplay != mInvertDisplay) {
			mSelectedPhosphor = mDesiredPhosphor;
			mInvertDisplay = mDesiredInvertDisplay;
			mRenderer.SetColorGrading(crtPhosphorColors[int(mSelectedPhosphor)], mInvertDisplay);
		}

		// Draw game
		mRenderer.ResetStrings();

		// Figure out more layout params
		const float horizontalStretch = 0.83f;
		int pixelsWidth = (int)(characterHeight * kFieldWidth * horizontalStretch);
		mRtFieldCharactersWidth = pixelsWidth / characterWidth + 1;
		mRtFieldPixelsWidth = mRtFieldCharactersWidth * characterWidth;
		mRtFieldPixelsHeight = kFieldHeight * characterHeight;

		const unsigned int kRemovedLinesToLevelUp = 10;
		const float gameStatsLeft = playingFieldRight + gameStatsCharactersOffset * characterWidthRelative;
		const float overlayTop = playingFieldBottom + 1 * characterHeightRelative;

		// Draw header
		float headerPosY = top;
		std::string header = "============================= Bin Packing Path Traced =============================";
		mRenderer.AddString(header, mRenderer.GetCenteredTextX(header.c_str(), header.length(), characterWidth, frameWidth), unsigned int(headerPosY* frameHeight));

		// Draw footer
		std::string footer = "Refracted Ray \1 " + std::string(mRTOn ? "2026" : "1989");
		mRenderer.AddString(footer, left, 1.0f - bottom - characterHeightRelative);

		// Draw help
		//if (mGameState == GameState::NOT_STARTED || mGameState == GameState::PAUSE)
		{
			std::string controls = "Move:        [\x1B\x1A Keys]\nRotate:        [ENTER]\nDrop:          [SPACE]\nPause:           [ESC]";
			controls += "\nSound " + std::string(mEnableSounds ? "off:" : "on: ") + "         [M]";
			controls += "\nPath Tracing " + std::string(mRTOn ? "off:" : "on!:") + "  [P]";
			mRenderer.AddString(controls, left, playingFieldTop + 3 * characterHeightRelative, glm::vec4(1, 1, 1, 1), glm::vec4(0, 0, 0, 0), true);
		}

		// Draw game stats
		int spaces = mRTOn ? mRtFieldCharactersWidth - 7 : 13;
		char scoreBuffer[80];
		sprintf_s(scoreBuffer, " Score: %*i", spaces, mScore);
		mRenderer.AddString(scoreBuffer, playingFieldLeft, playingFieldTop - characterHeightRelative);

		// Draw playing field
		bool* highlightColorMask;
		size_t highlightColorMaskLength;
		char* playingField = playingFieldToString(highlightColorMask, highlightColorMaskLength, mRtFieldCharactersWidth);
		mRenderer.AddString(playingField, playingFieldLeft, playingFieldTop, glm::vec4(1, 1, 1, 1), glm::vec4(0, 0, 0, 0), true, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), highlightColorMask, highlightColorMaskLength);

		// Draw progress
		if (mGameState != GameState::NOT_STARTED) {
			char progressBuffer[kRemovedLinesToLevelUp * 2 + 16];
			int progressBufferIdx = 0;
			progressBuffer[progressBufferIdx++] = kBlockSolidChar;

			for (unsigned int i = 0; i < mRemovedLinesOnThisLevel; i++)
				progressBuffer[progressBufferIdx++] = kBlockSolidChar;

			for (unsigned int i = 0; i < kRemovedLinesToLevelUp - mRemovedLinesOnThisLevel; i++)
				progressBuffer[progressBufferIdx++] = (i == 0 && mGameState == GameState::RUNNING) ? kBlockLightChar : ' ';

			progressBuffer[progressBufferIdx++] = kBlockSolidChar;
			progressBuffer[progressBufferIdx++] = '\n';
			for (unsigned int i = 0; i < kRemovedLinesToLevelUp + 2; i++)
				progressBuffer[progressBufferIdx++] = kHalfBlockTopChar;

			progressBuffer[progressBufferIdx++] = 0;

			mRenderer.AddString(progressBuffer, gameStatsLeft, playingFieldBottom - 2 * characterHeightRelative, glm::vec4(1, 1, 1, 1), glm::vec4(0, 0, 0, 0), true);
		}

		// Draw game stats
		char gameStatsBuffer[64];
		sprintf_s(gameStatsBuffer, "Level: %5i\nCTRL+Zs: %3i", mLevel, mUndosLeft);
		mRenderer.AddString(gameStatsBuffer, gameStatsLeft, playingFieldTop + characterHeightRelative);

		// Draw next block
		mRenderer.AddString((std::string("Next Block:\n") + nextBlockToString()), gameStatsLeft, playingFieldTop + characterHeightRelative * 7, glm::vec4(1, 1, 1, 1), glm::vec4(0, 0, 0, 0), true);

		// Draw overlay text
		char overlayTextBufferA[128];
		char overlayTextBufferB[128];
		if (getOverlayText(overlayTextBufferA, _countof(overlayTextBufferA), overlayTextBufferB, _countof(overlayTextBufferB))) {
			mRenderer.AddString(overlayTextBufferA, mRenderer.GetCenteredTextX(overlayTextBufferA, _countof(overlayTextBufferA), characterWidth, frameWidth), unsigned int(overlayTop * frameHeight));
			mRenderer.AddString(overlayTextBufferB, mRenderer.GetCenteredTextX(overlayTextBufferB, _countof(overlayTextBufferB), characterWidth, frameWidth), unsigned int((overlayTop + characterHeightRelative) * frameHeight));
		}

	}

	// Prepare RT rendering
	{
		mRenderer.ClearAllCubes();

		for (int j = 0; j < kFieldHeight; j++) {
			for (int i = 0; i < kFieldWidth; i++) {
				unsigned char blockMaterialIndex = mField[i][j];
				bool isFieldOccupied = (blockMaterialIndex > 0);

				if (!isFieldOccupied && mActiveBlock) {
					glm::ivec2 blockOffset = getBlockOffset(i - blockPosition.x, j - blockPosition.y, blockSize, blockRotation);

					if (blockOffset.x >= 0 && blockOffset.y >= 0 && blockOffset.x <= blockSize.x && blockOffset.y <= blockSize.y) {
						isFieldOccupied = mActiveBlock[blockOffset.y][blockOffset.x];
					}

					blockMaterialIndex = mActiveBlockMaterialIndex;
				}

				if (isFieldOccupied) {
					mRenderer.AddCube(blockMaterialIndex - 1, glm::uvec2(i, j));
				}
			}
		}

		updateBlockDifferencesList();
	}

	mRenderer.Update(hwnd, elapsedTime, kFieldHeight, rtFieldPosX, rtFieldPosY, mBlockDifferences, mRTOn);

	return true;
}

void Game::Cleanup()
{
	mAudio.Cleanup();
	mRenderer.Cleanup();
	mFileSystem.Cleanup();
}

void Game::sound(SoundEffect s)
{
	if (!mEnableSounds) return;

	mAudio.SoundPlay(mSoundHandles[size_t(s)]);
}

glm::ivec2 Game::getBlockOffset(int i, int j, glm::ivec2 size, BlockRotation rotation)
{
	glm::ivec2 blockOffset(i, j);

	if (rotation == BlockRotation::ROT_270) {
		blockOffset = glm::ivec2(size.x - blockOffset.y, blockOffset.x);
	}
	else if (rotation == BlockRotation::ROT_180) {
		blockOffset = glm::ivec2(size.x - blockOffset.x, size.y - blockOffset.y);
	}
	else if (rotation == BlockRotation::ROT_90) {
		blockOffset = glm::ivec2(blockOffset.y, size.y - blockOffset.x);
	}

	return blockOffset;
}

char* Game::nextBlockToString() {

	const int windowWidth = 4;
	const int windowHeight = 4;

	static char gameString[(windowWidth + 3) * (windowHeight + 2) + 1];

	unsigned int idx = 0;
	gameString[idx++] = kTopLeftCornerChar;
	for (unsigned int i = 0; i < windowWidth; i++) {
		gameString[idx++] = kHorizontalDoubleBarChar;
	}
	gameString[idx++] = kTopRightCornerChar;
	gameString[idx++] = '\n';

	for (int j = 0; j < windowHeight; j++) {
		gameString[idx++] = kVerticalDoubleBarChar;
		for (int i = 0; i < windowWidth; i++) {
			glm::ivec2 blockOffset = getBlockOffset(i, j, nextBlockSize, nextBlockRotation);
			bool hit = blockOffset.x >= 0 && blockOffset.y >= 0 && ((mGameState == GameState::RUNNING || mGameState == GameState::PAUSE) && mNextBlock.shapeData[blockOffset.y][blockOffset.x]);
			gameString[idx++] = hit ? kCenterBlockChar : ' ';
		}
		gameString[idx++] = kVerticalDoubleBarChar;
		gameString[idx++] = '\n';
	}

	gameString[idx++] = kBottomLeftCornerChar;
	for (int i = 0; i < windowWidth; i++) {
		gameString[idx++] = kHorizontalDoubleBarChar;
	}
	gameString[idx++] = kBottomRightCornerChar;

	gameString[idx++] = '\n';
	gameString[idx++] = 0;

	return gameString;
}

char* Game::playingFieldToString(bool*& highlightColorMask, size_t& highlightColorMaskLength, unsigned int rtFieldCharactersWidth)
{
	size_t rtOffBufferLength = (kFieldWidth + 3) * (kFieldHeight + 2) + 1;
	size_t rtOnBufferLength = (rtFieldCharactersWidth + 3) * (kFieldHeight + 2) + 1;

	if (!mPlayingFieldStringBuffer) mPlayingFieldStringBuffer = new char[std::max(rtOnBufferLength, rtOffBufferLength)];
	if (!mPlayingFieldHighlightColorMask) mPlayingFieldHighlightColorMask = new bool[std::max(rtOnBufferLength, rtOffBufferLength)];

	if (mRTOn) {

		highlightColorMask = nullptr;

		unsigned int idx = 0;
		mPlayingFieldStringBuffer[idx++] = kTopLeftCornerChar;
		for (unsigned int i = 0; i < rtFieldCharactersWidth; i++) {
			mPlayingFieldStringBuffer[idx++] = kHorizontalDoubleBarChar;
		}
		mPlayingFieldStringBuffer[idx++] = kTopRightCornerChar;
		mPlayingFieldStringBuffer[idx++] = '\n';

		for (unsigned int i = 0; i < kFieldHeight; i++) {
			mPlayingFieldStringBuffer[idx++] = kVerticalDoubleBarChar;
			for (unsigned int i = 0; i < mRtFieldCharactersWidth; i++) {
				mPlayingFieldStringBuffer[idx++] = ' ';
			}
			mPlayingFieldStringBuffer[idx++] = kVerticalDoubleBarChar;
			mPlayingFieldStringBuffer[idx++] = '\n';
		}

		mPlayingFieldStringBuffer[idx++] = kBottomLeftCornerChar;
		for (unsigned int i = 0; i < mRtFieldCharactersWidth; i++) {
			mPlayingFieldStringBuffer[idx++] = kHorizontalDoubleBarChar;
		}
		mPlayingFieldStringBuffer[idx++] = kBottomRightCornerChar;
		mPlayingFieldStringBuffer[idx++] = '\n';

		mPlayingFieldStringBuffer[idx++] = 0;
	}
	else {

		highlightColorMaskLength = rtOffBufferLength;
		highlightColorMask = mPlayingFieldHighlightColorMask;

		memset(mPlayingFieldHighlightColorMask, 0, rtOffBufferLength);

		unsigned int idx = 0;
		mPlayingFieldStringBuffer[idx++] = kTopLeftCornerChar;
		for (unsigned int i = 0; i < kFieldWidth; i++) {
			mPlayingFieldStringBuffer[idx++] = kHorizontalDoubleBarChar;
		}
		mPlayingFieldStringBuffer[idx++] = kTopRightCornerChar;
		mPlayingFieldStringBuffer[idx++] = '\n';

		for (int j = 0; j < kFieldHeight; j++) {
			mPlayingFieldStringBuffer[idx++] = kVerticalDoubleBarChar;
			for (int i = 0; i < kFieldWidth; i++) {

				bool isFieldOccupied = mField[i][j];

				if (!isFieldOccupied && mActiveBlock) {
					glm::ivec2 blockOffset = getBlockOffset(i - blockPosition.x, j - blockPosition.y, blockSize, blockRotation);

					if (blockOffset.x >= 0 && blockOffset.y >= 0 && blockOffset.x <= blockSize.x && blockOffset.y <= blockSize.y) {
						isFieldOccupied = mActiveBlock[blockOffset.y][blockOffset.x];
					}
				}

				if (!isFieldOccupied) mPlayingFieldHighlightColorMask[idx] = true;
				mPlayingFieldStringBuffer[idx++] = isFieldOccupied ? kCenterBlockChar : kCenterDotChar;
			}
			mPlayingFieldStringBuffer[idx++] = kVerticalDoubleBarChar;
			mPlayingFieldStringBuffer[idx++] = '\n';
		}

		mPlayingFieldStringBuffer[idx++] = kBottomLeftCornerChar;
		for (int i = 0; i < kFieldWidth; i++) {
			mPlayingFieldStringBuffer[idx++] = kHorizontalDoubleBarChar;
		}
		mPlayingFieldStringBuffer[idx++] = kBottomRightCornerChar;

		mPlayingFieldStringBuffer[idx++] = '\n';
		mPlayingFieldStringBuffer[idx++] = 0;
	}

	return mPlayingFieldStringBuffer;
}

void Game::initializeGame() {
	mGameState = GameState::NOT_STARTED;
	mActiveBlock = nullptr;
	mLevel = 1;
	mScore = 0;
	mConsecutiveUserInputsCount = 0;
	mLastUserInputTime = std::chrono::high_resolution_clock::now();
	mLastUserInput = UserInput::NOTHING;

	clearPlayingField();
}

bool Game::removeLines()
{
	int removedLines = 0;

	for (int j = 0; j < kFieldHeight; j++) {

		bool isComplete = true;
		for (int i = 0; i < kFieldWidth; i++) {
			if (!mField[i][j]) {
				isComplete = false;
				break;
			}
		}

		if (isComplete) {
			removedLines++;
			lastPlacedBlock.clear();
			for (int k = j; k > 0; k--) {
				for (int l = 0; l < kFieldWidth; l++) {
					mField[l][k] = mField[l][k - 1];
				}
			}
			for (int l = 0; l < kFieldWidth; l++) {
				mField[l][0] = 0;
			}
		}
	}

	mScore += (removedLines * 100) * unsigned int(std::ceil(float(removedLines) / 2.0f)) * mLevel;

	mRemovedLinesOnThisLevel += removedLines;

	if (mRemovedLinesOnThisLevel >= kRemovedLinesToLevelUp) {
		// Level Up!
		mLevel++;
		mRemovedLinesOnThisLevel = 0;
		mWasLevelUp = true;
		pickNextColorScheme();
	}

	return removedLines > 0;
}

void Game::pickNextColorScheme()
{

	if (mDesiredInvertDisplay || mSelectedPhosphor == CrtPhosphorTypes::Green) {
		mDesiredPhosphor = CrtPhosphorTypes((int(mSelectedPhosphor) + 1) % int(CrtPhosphorTypes::Count));
		mDesiredInvertDisplay = false;
	}
	else {
		mDesiredInvertDisplay = true;
	}

	// Don't invert green display, it looks crazy
	if (mDesiredPhosphor == CrtPhosphorTypes::Green) mDesiredInvertDisplay = false;
}

glm::ivec2 Game::calculateBlockSize(Block block)
{
	glm::ivec2 blockSize = glm::ivec2(0, 0);

	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			glm::ivec2 blockOffset = glm::ivec2(i, j);
			if (block.shapeData[blockOffset.y][blockOffset.x]) {
				blockSize = glm::max(blockSize, blockOffset);
			}
		}
	}

	return blockSize;
}

void Game::moveBlock(glm::ivec2 offset)
{
	blockPosition += offset;
}

bool Game::placeNewBlock()
{
	mActiveBlock = mNextBlock.shapeData;
	blockRotation = nextBlockRotation;

	blockSize = nextBlockSize;
	blockPosition = glm::ivec2(kFieldWidth / 2 - blockSize.x / 2, 0);

	pickNextBlock();

	return checkBlockCollision(glm::ivec2(0, 0), blockRotation);
}

void Game::copyBlockToField()
{
	lastPlacedBlock.clear();

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {

			glm::ivec2 blockOffset = getBlockOffset(i, j, blockSize, blockRotation);
			if (blockOffset.x < 0 || blockOffset.y < 0) continue;

			glm::ivec2 destinationPos = blockPosition + glm::ivec2(i, j);
			if (!isInsideField(destinationPos)) continue;

			unsigned char* destination = &mField[destinationPos.x][destinationPos.y];
			const bool* source = &mActiveBlock[blockOffset.y][blockOffset.x];

			if (*source) {
				lastPlacedBlock.push_back(destination);
				*destination = mActiveBlockMaterialIndex;
			}
		}
	}
}

void Game::pickNextBlock()
{
	std::random_device r;
	std::default_random_engine e1(r());
	std::default_random_engine e2(r());
	std::default_random_engine e3(r());
	std::uniform_int_distribution<int> uniform_distBlockType(0, _countof(blocks) - 1);
	std::uniform_int_distribution<int> uniform_distRotation(0, int(BlockRotation::COUNT) - 1);
	std::uniform_int_distribution<int> uniform_distMaterial(1, mRenderer.GetAllMaterialsCount());

	mNextBlock = blocks[uniform_distBlockType(e1)];
	nextBlockRotation = BlockRotation(uniform_distRotation(e2));
	nextBlockSize = calculateBlockSize(mNextBlock);
	mActiveBlockMaterialIndex = uniform_distMaterial(e3);
}

bool Game::isInsideField(glm::ivec2 pos) {
	if (pos.y >= kFieldHeight) return false;
	if (pos.x >= kFieldWidth) return false;
	if (pos.y < 0) return false;
	if (pos.x < 0) return false;
	return true;
}

bool Game::checkBlockCollision(glm::ivec2 additionalOffset, BlockRotation rotation) {

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {

			glm::ivec2 blockOffset = getBlockOffset(i, j, blockSize, rotation);
			if (blockOffset.x < 0 || blockOffset.y < 0) continue;

			const bool* source = &mActiveBlock[blockOffset.y][blockOffset.x];

			if (*source) {
				glm::ivec2 fieldOffset = blockPosition + glm::ivec2(i, j) + additionalOffset;
				if (!isInsideField(fieldOffset)) return false;

				unsigned char* destination = &mField[fieldOffset.x][fieldOffset.y];
				if (*destination) return false;
			}
		}
	}

	return true;
}

bool Game::canBlockFall() {
	return checkBlockCollision(glm::ivec2(0, 1), blockRotation);
}

void Game::clearPlayingField() {
	for (int i = 0; i < kFieldWidth; i++) {
		for (int j = 0; j < kFieldHeight; j++) {
			mField[i][j] = 0;
		}
	}
}

bool Game::getOverlayText(char* bufferA, size_t bufferLengthA, char* bufferB, size_t bufferLengthB) 
{
	const char* newGame = "New game. Use + and - keys to select difficulty (%i).";
	const char* gameOver = "Game over. You scored %i points.";
	const char* gamePaused = "Game paused.";
	const char* anyKeyToStart = "Press any other key to start.";
	const char* anyKeyToContinue = "Press any key to continue.";

	switch (mGameState) {
	case GameState::NOT_STARTED:
		sprintf_s(bufferA, bufferLengthA, newGame, mLevel);
		sprintf_s(bufferB, bufferLengthB, anyKeyToStart, mLevel);
		return true;
	case GameState::GAME_OVER_WAIT:
		sprintf_s(bufferA, bufferLengthA, gameOver, mScore);
		bufferB[0] = 0;
		return true;
	case GameState::GAME_OVER:
		sprintf_s(bufferA, bufferLengthA, gameOver, mScore);
		sprintf_s(bufferB, bufferLengthB, anyKeyToContinue, mLevel);
		return true;
	case GameState::PAUSE:
		sprintf_s(bufferA, bufferLengthA, gamePaused);
		sprintf_s(bufferB, bufferLengthB, anyKeyToContinue, mLevel);
		return true;
	default:
		bufferA[0] = 0;
		bufferB[0] = 0;
		return false;
	}
}

void Game::updateBlockDifferencesList()
{
	mBlockDifferences.clear();

	for (int i = 0; i < kFieldWidth; i++) {
		for (int j = 0; j < kFieldHeight; j++) {

			unsigned char blockMaterialIndex = mField[i][j];
			bool isFieldOccupied = (blockMaterialIndex > 0);

			if (!isFieldOccupied && mActiveBlock) {
				glm::ivec2 blockOffset = getBlockOffset(i - blockPosition.x, j - blockPosition.y, blockSize, blockRotation);

				if (blockOffset.x >= 0 && blockOffset.y >= 0 && blockOffset.x <= blockSize.x && blockOffset.y <= blockSize.y) {
					isFieldOccupied = mActiveBlock[blockOffset.y][blockOffset.x];
				}

				if (isFieldOccupied) blockMaterialIndex = mActiveBlockMaterialIndex;
			}

			if (mPreviousField[i][j] != blockMaterialIndex) {
				mBlockDifferences.push_back(glm::ivec2(i, j));
			}
		}
	}

	// Remember current playing field
	for (int i = 0; i < kFieldWidth; i++) {
		for (int j = 0; j < kFieldHeight; j++) {

			unsigned char blockMaterialIndex = mField[i][j];
			bool isFieldOccupied = (blockMaterialIndex > 0);

			if (!isFieldOccupied && mActiveBlock) {
				glm::ivec2 blockOffset = getBlockOffset(i - blockPosition.x, j - blockPosition.y, blockSize, blockRotation);

				if (blockOffset.x >= 0 && blockOffset.y >= 0 && blockOffset.x <= blockSize.x && blockOffset.y <= blockSize.y) {
					isFieldOccupied = mActiveBlock[blockOffset.y][blockOffset.x];
				}

				if (isFieldOccupied) blockMaterialIndex = mActiveBlockMaterialIndex;
			}

			mPreviousField[i][j] = blockMaterialIndex;
		}
	}
}
