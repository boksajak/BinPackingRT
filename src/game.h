#include "common.h"
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

	// File system
	FileSystem mFileSystem;

	// Renderer
	Renderer mRenderer;

	// Game Controls
	bool mRTOn = false;
};