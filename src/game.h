#include "common.h"


const unsigned int frameWidth = 1820;
const unsigned int frameHeight = 980;

class Game
{
public:

	void Initialize(HWND hwnd);
	void Cleanup();
	
	void ReloadShaders();
	bool Update(HWND hwnd, const float elapsedTime);
	
private:

	void initializeDx12(HWND hwnd);
	void createComputePasses();

	bool mReloadShaders = false;
};