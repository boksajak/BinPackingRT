#include "common.h"
#include "audio.h"
#include "fileSystem.h"
#include "renderer.h"

class Game
{
public:

	void Initialize(HWND hwnd);
	void Cleanup();
	void KeyDown(WPARAM wParam);

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

	// Blocks
	enum class BlockRotation {
		ROT_0,
		ROT_90,
		ROT_180,
		ROT_270,
		COUNT
	};
	const bool(*mActiveBlock)[4];
	unsigned char mActiveBlockMaterialIndex;
	glm::ivec2 blockPosition;
	glm::ivec2 blockSize;
	BlockRotation blockRotation;
	glm::ivec2 nextBlockSize;
	BlockRotation nextBlockRotation;

	const bool mono[4][4] = { true,  false, false, false,
						  false, false, false, false,
						  false, false, false, false,
						  false, false, false, false, };

	const bool duo[4][4] = { true,  true,  false, false,
							  false, false, false, false,
							  false, false, false, false,
							  false, false, false, false, };

	const bool T[4][4] = { true,  true,  true,  false,
							  false, true,  false, false,
							  false, false, false, false,
							  false, false, false, false, };

	const bool I[4][4] = { true,  false,  false,  false,
							  true, false,  false, false,
							  true, false, false, false,
							  true, false, false, false, };

	const bool L[4][4] = { true,  true,  true,  false,
								false, false,  true, false,
								false, false, false, false,
								false, false, false, false, };

	const bool J[4][4] = { true,  true,  true,  false,
								true, false,  false, false,
								false, false, false, false,
								false, false, false, false, };

	const bool O[4][4] = { true,  true,  false,  false,
								true, true,  false, false,
								false, false, false, false,
								false, false, false, false, };

	const bool spaceship[4][4] = { true,  true,  true,  true,
								false, true,  true, false,
								false, false, false, false,
								false, false, false, false, };

	const bool Z[4][4] = { true,  true,  false,  false,
								false, true,  true, false,
								false, false, false, false,
								false, false, false, false, };

	const bool S[4][4] = { false,  true,  true,  false,
								true, true,  false, false,
								false, false, false, false,
								false, false, false, false, };

	struct Block {
		const bool(*shapeData)[4];
	} blocks[10] = { mono, duo, T, I, L, J, O, spaceship, Z, S };

	Block mNextBlock;

	// Playing field
	static const unsigned int kFieldWidth = 20;
	static const unsigned int kFieldHeight = 20;
	unsigned char mField[kFieldWidth][kFieldHeight];
	unsigned char mPreviousField[kFieldWidth][kFieldHeight];
	char* mPlayingFieldStringBuffer = nullptr;
	bool* mPlayingFieldHighlightColorMask = nullptr;

	// RT stuff
	unsigned int mRtFieldPixelsWidth;
	unsigned int mRtFieldPixelsHeight;
	unsigned int mRtFieldCharactersWidth;

	// Game Controls
	bool mEnableSounds = true;
	bool mRTOn = true;
	bool mShowFPS = false;

	// Game state and input
	enum class GameState {
		NOT_STARTED,
		RUNNING,
		GAME_OVER_WAIT,
		GAME_OVER,
		PAUSE
	} mGameState;

	unsigned int mScore;
	unsigned int mLevel;
	unsigned int mRemovedLinesOnThisLevel;
	unsigned int mUndosLeft = 0;
	std::chrono::high_resolution_clock::time_point mGameOverTime;
	long long mGameTime;
	bool mWasLevelUp;
	std::vector<unsigned char*> lastPlacedBlock;
	std::vector<glm::ivec2> mBlockDifferences;

	// Gameplay
	void initializeGame();
	void updateGamePlay(long long elapsedTime);
	void clearPlayingField();
	void pickNextBlock();
	bool isInsideField(glm::ivec2 pos);
	bool canBlockFall();
	bool checkBlockCollision(glm::ivec2 additionalOffset, BlockRotation rotation);
	char* playingFieldToString(bool*& highlightColorMask, size_t& highlightColorMaskLength, unsigned int rtFieldCharactersWidth);
	glm::ivec2 getBlockOffset(int i, int j, glm::ivec2 size, BlockRotation rotation);
	char* nextBlockToString();
	bool getOverlayText(char* bufferA, size_t bufferLengthA, char* bufferB, size_t bufferLengthB);
	glm::ivec2 calculateBlockSize(Block block);
	void moveBlock(glm::ivec2 offset);
	void copyBlockToField();
	bool removeLines();
	void pickNextColorScheme();
	bool placeNewBlock();
	void updateBlockDifferencesList();
	void createTopLevelAS(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances);

	static const unsigned int kRemovedLinesToLevelUp = 10;

	// Input system
	enum class UserInput {
		NOTHING,
		START_GAME,
		LEFT,
		RIGHT,
		ROTATE,
		DROP,
		PAUSE,
		LEVEL_UP,
		LEVEL_DOWN,
		UNDO,
		TOGGLE_MUTE,
	} userInput;

	std::chrono::high_resolution_clock::time_point mLastUserInputTime;
	unsigned int mConsecutiveUserInputsCount;
	UserInput mLastUserInput;
	void getUserInput();

	// Display settings
	enum class CrtPhosphorTypes {
		Amber,
		Green,
		White,
		Count
	};
	CrtPhosphorTypes mSelectedPhosphor = CrtPhosphorTypes::Amber;
	CrtPhosphorTypes mDesiredPhosphor;
	glm::vec3 crtPhosphorColors[int(CrtPhosphorTypes::Count)] = { glm::vec3(1, 0.75, 0), glm::vec3(0.33, 1.0, 0.0), glm::vec3(0.9, 0.9, 0.9) };
	bool mInvertDisplay;
	bool mDesiredInvertDisplay;

	// Rendering chars
	// Special chars (in CP437)
	const char kTopLeftCornerChar = char(201);
	const char kHorizontalDoubleBarChar = char(205);
	const char kTopRightCornerChar = char(187);
	const char kVerticalDoubleBarChar = char(186);
	const char kBottomLeftCornerChar = char(200);
	const char kBottomRightCornerChar = char(188);
	const char kCenterDotChar = char(250);
	const char kCenterBlockChar = char(254);
	const char kHalfBlockTopChar = char(223);
	const char kHalfBlockRightChar = char(222);
	const char kHalfBlockLeftChar = char(221);
	const char kBlockSolidChar = char(219);
	const char kBlockLightChar = char(176);
	const char kBlockMediumChar = char(177);
	const char kBlockDarkChar = char(178);

};