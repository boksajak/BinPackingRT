#include "common.h"
#include "fileSystem.h"
#include <xaudio2.h>

typedef size_t SoundHandle;

struct WAVData
{
	const WAVEFORMATEX* wfx;
	const uint8_t* startAudio;
	uint32_t audioBytes;
	uint32_t loopStart;
	uint32_t loopLength;
	const uint32_t* seek;
	uint32_t seekCount;
};

struct SoundData {
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	XAUDIO2_BUFFER buffer = { 0 };
	XAUDIO2_BUFFER_WMA xwmaBuffer = { 0 };
	WAVData waveData;
};

class Audio
{
public:

	void Initialize(HWND hwnd);
	void Cleanup();

	SoundHandle LoadSound(FileSystem& fileSystem, std::wstring fileName);
	void SoundPlay(SoundHandle soundHandle);

private:

	std::vector<SoundData> sounds;

	IXAudio2* mpXAudio2 = nullptr;
	IXAudio2MasteringVoice* mpMasterVoice = nullptr;
};