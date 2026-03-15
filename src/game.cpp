#include "game.h"
#include "shaders/shared.h"

void Game::Initialize(HWND hwnd)
{
	//mShaderCompiler.Initialize();
	initializeDx12(hwnd);
}

void Game::ReloadShaders() {
	mReloadShaders = true;
}

bool Game::Update(HWND hwnd, const float elapsedTime)
{

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

void Game::createComputePasses()
{


}

void Game::initializeDx12(HWND hwnd)
{

}