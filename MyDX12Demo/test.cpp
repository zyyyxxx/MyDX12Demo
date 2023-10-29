/*
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	// 创建窗口
	HWND hwnd;
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = "DirectX12Sample";
	RegisterClassEx(&wc);

	RECT windowRect = { 0, 0, 800, 600 };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
	hwnd = CreateWindowEx(0, "DirectX12Sample", "DirectX 12 Sample", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, NULL, NULL, hInstance, NULL);

	// 初始化 DirectX 12
	ID3D12Device* pDevice;
	ID3D12CommandQueue* pCommandQueue;
	//HRESULT WINAPI D3D12CreateDevice(
	//    _In_opt_ IUnknown * pAdapter,
	//    D3D_FEATURE_LEVEL MinimumFeatureLevel,
	//    _In_ REFIID riid, // Expected: ID3D12Device
	//    _COM_Outptr_opt_ void** ppDevice);
	D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue));

	// 显示窗口
	ShowWindow(hwnd, nCmdShow);

	// 主消息循环
	MSG msg = {};
	while (true)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
				break;
		}

		// 渲染逻辑

		// 在此处执行绘制命令

		// 交换前后缓冲区
		// pCommandQueue->Present();

		// 在这里添加帧同步逻辑，确保帧速率
	}

	// 清理资源
	// 释放 DirectX 12 对象

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}
*/
