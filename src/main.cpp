#include "common.h"
#include "utils.h"
#include "game.h"

// Windows DPI Scaling
#include <ShellScalingApi.h>
#pragma comment(lib, "shcore.lib")

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	PAINTSTRUCT ps;
	HDC hdc;
	RECT clientRect;
	LPCREATESTRUCT pCreateStruct;

	Game* game = reinterpret_cast<Game*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (message) {
	case WM_CREATE:
		// Save the pointer passed in to CreateWindow as lParam.
		pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_KEYUP:
		switch (wParam)
		{
		case VK_F5:
			if (game != nullptr) {
				game->ReloadShaders();
			}
			break;
		}
		break;
	case WM_KEYDOWN:
		if (game != nullptr) {
			game->KeyDown(wParam);
		}
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

HRESULT Create(LONG width, LONG height, HINSTANCE& instance, HWND& window, LPCWSTR title, Game* game)
{
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

	// Register the window class
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = L"Bin Packing Path Traced";
	wcex.hIcon = nullptr;
	wcex.hIconSm = nullptr;

	if (!RegisterClassEx(&wcex)) {
		utils::validate(E_FAIL, L"Error: failed to register window!");
	}

	// Get the desktop resolution
	RECT desktop;
	const HWND hDesktop = GetDesktopWindow();
	GetWindowRect(hDesktop, &desktop);

	int x = (desktop.right - width) / 2;
	int y = (desktop.bottom - height) / 3;

	// Create the window
	RECT rc = { 0, 0, width, height };
	AdjustWindowRect(&rc, style, FALSE);

	window = CreateWindow(wcex.lpszClassName, title, style, x, y, (rc.right - rc.left), (rc.bottom - rc.top), NULL, NULL, instance, game);
	if (!window) return E_FAIL;

	// Show the window
	ShowWindow(window, SW_SHOWDEFAULT);
	UpdateWindow(window);

	return S_OK;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	HRESULT hr = EXIT_SUCCESS;

	{
		MSG msg = { 0 };
		HWND hWnd = { 0 };

		// Tell Windows that we're DPI aware (we handle scaling ourselves, e.g. the scaling of GUI)
		SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

		Game game;

		// Initialize window
		HRESULT hr = Create(frameWidth, frameHeight, hInstance, hWnd, L"Bin Packing Path Traced", &game);
		utils::validate(hr, L"Error: failed to create window!");

		game.Initialize(hWnd);

		std::chrono::steady_clock::time_point lastFrameTime = {};

		// Main loop
		while (WM_QUIT != msg.message)
		{
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			// Calculate frame time
			const float elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - lastFrameTime).count() * 0.001f;
			lastFrameTime = std::chrono::steady_clock::now();

			// Break the loop here when the game is over
			if (!game.Update(hWnd, elapsedTime)) break;
		}

		game.Cleanup();
	}

	return hr;
}