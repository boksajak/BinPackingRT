#include "game.h"
#include "utils.h"

void Game::Initialize(HWND hwnd)
{
	// Init settings
	mEnableSounds = true;

#ifdef _DEBUG
	mFileSystem.initDevelopment();
#else
	mFileSystem.initUseBigFile(L"stuff.bin");
#endif

	mRenderer.Initialize(hwnd, mFileSystem);
	mAudio.Initialize(hwnd);

	// Load sounds
	for (int i = 0; i < _countof(mSoundFiles); i++)
	{
		mSoundHandles[i] = mAudio.LoadSound(mFileSystem, L"assets\\" + mSoundFiles[i].second);
	}

	initializeGame();
}

void Game::ReloadShaders() 
{
	mRenderer.ReloadShaders();
}

bool Game::Update(HWND hwnd, const float elapsedTime)
{
	// Prepare game GUI
	{
		mRenderer.ResetStrings();

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

		// Figure out more layout params
		const float playingFieldTop = top + 4 * characterHeightRelative;

		const float horizontalStretch = 0.83f;
		int pixelsWidth = (int)(characterHeight * kFieldWidth * horizontalStretch);
		mRtFieldCharactersWidth = pixelsWidth / characterWidth + 1;
		mRtFieldPixelsWidth = mRtFieldCharactersWidth * characterWidth;
		mRtFieldPixelsHeight = kFieldHeight * characterHeight;

		const int gameStatsCharactersOffset = 2;
		const unsigned int kRemovedLinesToLevelUp = 10;
		unsigned int playingFieldWidth = mRTOn ? mRtFieldCharactersWidth + 2 : kFieldWidth + 2;
		const int mainAreaWidthCharacters = (playingFieldWidth + 2) + gameStatsCharactersOffset + (kRemovedLinesToLevelUp + 2);
		const float playingFieldLeft = (1.0f - (mainAreaWidthCharacters * characterWidthRelative)) * 0.5f;
		const float playingFieldBottom = playingFieldTop + (kFieldHeight + 2) * characterHeightRelative;
		const float playingFieldRight = playingFieldLeft + (playingFieldWidth + 2) * characterWidthRelative;
		const float gameStatsLeft = playingFieldRight + gameStatsCharactersOffset * characterWidthRelative;
		const float overlayTop = playingFieldBottom + 1 * characterHeightRelative;

		// Draw header
		float headerPosY = top;
		std::string header = "============================= Bin Packing Path Traced =============================";
		mRenderer.AddString(header, mRenderer.GetCenteredTextX(header.c_str(), header.length(), characterWidth, frameWidth), unsigned int(headerPosY * frameHeight));

		// Draw footer
		std::string footer = "Refracted Ray \1 " + std::string(mRTOn ? "2026" : "1989");
		mRenderer.AddString(footer, left, 1.0f - bottom - characterHeightRelative);

		// Draw help
		//if (mGameState == GameState::NOT_STARTED || mGameState == GameState::PAUSE)
		{
			std::string controls = "Move:        [\x1B\x1A Keys]\nRotate:        [ENTER]\nDrop:          [SPACE]\nPause:           [ESC]";
			controls += "\nSound " + std::string(mEnableSounds ? "off:" : "on: ") + "         [M]";
			controls += "\nPath Tracing " + std::string(mRTOn ? "off:" : "on:") + "   [P]";
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

	mRenderer.Update(hwnd, elapsedTime);

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
	mScore = 0;
	mActiveBlock = nullptr;
	mLevel = 1;
	mRemovedLinesOnThisLevel = 0;

	clearPlayingField();
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