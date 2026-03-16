#include "common.h"
#include "audio.h"
#include "fileSystem.h"
#include "renderer.h"

class Game
{
public:

	void Initialize(HWND hwnd);
	void Cleanup();
	
	void ReloadShaders();
	bool Update(HWND hwnd, const float elapsedTime);
	
private:

	// Components
	Audio mAudio;
	FileSystem mFileSystem;
	Renderer mRenderer;

	// Sounds
	enum class SoundEffect {
		GAME_START = 0,
		NEW_BLOCK,
		BLOCK_MOVE,
		BLOCK_PLACED,
		BLOCK_ROTATES,
		HIGH_SCORE,
		GAME_OVER,
		LINES_REMOVED,
		LEVEL_UP,
		COUNT
	};

	const std::pair<SoundEffect, std::wstring> mSoundFiles[size_t(SoundEffect::COUNT)] = {
		{ SoundEffect::GAME_START, L"game_start2.wav" },
		{ SoundEffect::NEW_BLOCK, L"new_block.wav" },
		{ SoundEffect::BLOCK_MOVE, L"block_move2.wav" },
		{ SoundEffect::BLOCK_PLACED, L"block_placed2.wav" },
		{ SoundEffect::BLOCK_ROTATES, L"block_rotates.wav" },
		{ SoundEffect::HIGH_SCORE, L"hi_score.wav" },
		{ SoundEffect::GAME_OVER, L"game_over.wav" },
		{ SoundEffect::LEVEL_UP, L"level_up.wav" },
		{ SoundEffect::LINES_REMOVED, L"lines_removed.wav" }
	};

	size_t mSoundHandles[size_t(SoundEffect::COUNT)];

	void sound(SoundEffect s);

	// Game Controls
	bool mRTOn = false;
};