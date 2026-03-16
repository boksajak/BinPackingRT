#include "game.h"
#include "utils.h"

void Game::Initialize(HWND hwnd)
{
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

		// Draw header
		float headerPosY = top;
		std::string header = "============================= Bin Packing Path Traced =============================";
		mRenderer.AddString(header, mRenderer.GetCenteredTextX(header.c_str(), header.length(), characterWidth, frameWidth), unsigned int(headerPosY * frameHeight));

		// Draw footer
		std::string footer = "Refracted Ray \1 " + std::string(mRTOn ? "2026" : "1989");
		mRenderer.AddString(footer, left, 1.0f - bottom - characterHeightRelative);

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
	mAudio.SoundPlay(mSoundHandles[size_t(s)]);
}
