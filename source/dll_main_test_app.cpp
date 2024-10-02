/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "version.h"
#include "dll_log.hpp"
#include "hook_manager.hpp"
#include "com_ptr.hpp"
#include <d3d11.h>

extern HMODULE g_module_handle;
extern std::filesystem::path g_reshade_dll_path;
extern std::filesystem::path g_reshade_base_path;
extern std::filesystem::path g_target_executable_path;

extern std::filesystem::path get_base_path(bool default_to_target_executable_path = false);
extern std::filesystem::path get_module_path(HMODULE module);

#define HR_CHECK(exp) { const HRESULT res = (exp); assert(SUCCEEDED(res)); }
#define VK_CHECK(exp) { const VkResult res = (exp); assert(res == VK_SUCCESS); }

#define VK_CALL_CMD(name, device, ...) reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name))(__VA_ARGS__)
#define VK_CALL_DEVICE(name, device, ...) reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name))(device, __VA_ARGS__)
#define VK_CALL_INSTANCE(name, instance, ...) reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))(__VA_ARGS__)

#define BACKGROUND_COLOR 0.0f, 0.0f, 0.0f, 0.0f

enum class DEVICE_API
{
	D3D9,
	D3D10,
	D3D11,
	D3D12,
	VULKAN,
	OPENGL
};

enum class TRACKING_TYPE
{
	NONE,
	SCREEN,
	WINDOW
};

struct scoped_module_handle
{
	scoped_module_handle(LPCWSTR name) : module(LoadLibraryW(name))
	{
		assert(module != nullptr);
		reshade::hooks::register_module(name);
	}
	~scoped_module_handle()
	{
		FreeLibrary(module);
	}

	const HMODULE module;
};

static LONG APIENTRY HookD3DKMTQueryAdapterInfo(const void *pData)
{
	struct D3DKMT_QUERYADAPTERINFO { UINT hAdapter; UINT Type; VOID *pPrivateDriverData; UINT PrivateDriverDataSize; };

	if (pData != nullptr && static_cast<const D3DKMT_QUERYADAPTERINFO *>(pData)->Type == 1 /* KMTQAITYPE_UMDRIVERNAME */)
	{
		if (false && *static_cast<const UINT *>(static_cast<const D3DKMT_QUERYADAPTERINFO *>(pData)->pPrivateDriverData) == 0 /* KMTUMDVERSION_DX9 */)
			return STATUS_INVALID_PARAMETER;
		if (false && *static_cast<const UINT *>(static_cast<const D3DKMT_QUERYADAPTERINFO *>(pData)->pPrivateDriverData) == 2 /* KMTUMDVERSION_DX11 */)
			return STATUS_INVALID_PARAMETER;
	}

	return reshade::hooks::call(HookD3DKMTQueryAdapterInfo)(pData);
}

BOOL CALLBACK GetMonitorData(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	std::vector<RECT> *MonitorRects = (std::vector<RECT> *)dwData;
	MonitorRects->emplace_back(*lprcMonitor);
	return TRUE;
}

int WINAPI CreateAndRunWindow(TRACKING_TYPE trackingtype, int screenid, HWND trackedwindow)
{
	LONG window_w = 1024;
	LONG window_h = 800;

	const LONG window_x = (GetSystemMetrics(SM_CXSCREEN) - window_w) / 2;
	const LONG window_y = (GetSystemMetrics(SM_CYSCREEN) - window_h) / 2;

	RECT window_rect = { window_x, window_y, window_x + window_w, window_y + window_h };
	AdjustWindowRect(&window_rect, window_x > 0 && window_y > 0 ? WS_OVERLAPPEDWINDOW : WS_POPUP, FALSE);

	std::vector<RECT> MonitorRects;
	EnumDisplayMonitors(NULL, NULL, GetMonitorData, (LPARAM)&MonitorRects);

	if (trackingtype == TRACKING_TYPE::SCREEN)
	{
		if (screenid >= 0 && screenid < MonitorRects.size())
		{
			window_rect = MonitorRects[screenid];
		}
		else
		{
			return 1;
		}
	}
	else if (trackingtype == TRACKING_TYPE::WINDOW)
	{
		RECT trackedwindowrect;
		GetWindowRect(trackedwindow, &trackedwindowrect);

		window_rect = trackedwindowrect;
	}
	else
	{
		return 1;
	}

	HINSTANCE hInstance = GetModuleHandle(NULL);
	g_module_handle = hInstance;
	g_reshade_dll_path = get_module_path(hInstance);
	g_target_executable_path = g_reshade_dll_path;
	g_reshade_base_path = get_base_path();

	std::error_code ec;
	reshade::log::open_log_file(g_reshade_base_path / L"ReShade.log", ec);

	reshade::hooks::register_module(L"user32.dll");

	reshade::hooks::install("D3DKMTQueryAdapterInfo", GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTQueryAdapterInfo"), HookD3DKMTQueryAdapterInfo);

	static UINT s_resize_w = 0, s_resize_h = 0;

	// Register window class
	WNDCLASS wc = { sizeof(wc) };
	wc.hIcon = LoadIcon(hInstance, TEXT("MAIN_ICON"));
	wc.hInstance = hInstance;
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpszClassName = TEXT("Test");
	wc.lpfnWndProc =
		[](HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
			switch (Msg)
			{
			case WM_DESTROY:
				PostQuitMessage(EXIT_SUCCESS);
				break;
			case WM_SIZE:
				s_resize_w = LOWORD(lParam);
				s_resize_h = HIWORD(lParam);
				break;
			}

			return DefWindowProc(hWnd, Msg, wParam, lParam);
		};

	RegisterClass(&wc);

	// Create and show window instance
	const HWND window_handle = CreateWindow(
		wc.lpszClassName, L"ExtractAndTranslate", window_x > 0 && window_y > 0 ? WS_OVERLAPPEDWINDOW : WS_POPUP,
		window_rect.left, window_rect.top, window_rect.right - window_rect.left, window_rect.bottom - window_rect.top, NULL, nullptr, hInstance, nullptr);

	SetWindowLong(window_handle, GWL_STYLE, 0);

	if (window_handle == nullptr)
		return 0;

	BLENDFUNCTION blend = { 0 };
	blend.BlendOp = AC_SRC_OVER;
	blend.SourceConstantAlpha = 255;
	blend.AlphaFormat = AC_SRC_ALPHA;

	UpdateLayeredWindow(window_handle, NULL, NULL, NULL, NULL, NULL, RGB(0, 0, 0), &blend, ULW_ALPHA);

	SetWindowLong(window_handle, GWL_EXSTYLE, GetWindowLong(window_handle, GWL_EXSTYLE) | WS_EX_LAYERED);

	COLORREF color = 0;
	BYTE alpha = 128;
	SetLayeredWindowAttributes(window_handle, color, alpha, LWA_COLORKEY);

	ShowWindow(window_handle, SW_NORMAL);

	// Avoid resize caused by 'ShowWindow' call
	s_resize_w = 0;
	s_resize_h = 0;

	MSG msg = {};

	DEVICE_API api = DEVICE_API::D3D11;

	const bool multisample = false;

	switch (api)
	{
	#pragma region D3D11 Implementation
	case DEVICE_API::D3D11:
	{
		const scoped_module_handle dxgi_module(L"dxgi.dll");
		const scoped_module_handle d3d11_module(L"d3d11.dll");

		// Initialize Direct3D 11
		com_ptr<ID3D11Device> device;
		com_ptr<ID3D11DeviceContext> immediate_context;
		com_ptr<IDXGISwapChain> swapchain;

		{   DXGI_SWAP_CHAIN_DESC desc = {};
			desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc = { multisample ? 4u : 1u, 0u };
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.BufferCount = 1;
			desc.OutputWindow = window_handle;
			desc.Windowed = true;
			desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

#ifndef NDEBUG
			const UINT flags = D3D11_CREATE_DEVICE_DEBUG;
#else
			const UINT flags = 0;
#endif
			HR_CHECK(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &desc, &swapchain, &device, nullptr, &immediate_context));
		}

		com_ptr<ID3D11Texture2D> backbuffer;
		HR_CHECK(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)));
		com_ptr<ID3D11RenderTargetView> target;
		HR_CHECK(device->CreateRenderTargetView(backbuffer.get(), nullptr, &target));

		while (true)
		{
			if (trackingtype == TRACKING_TYPE::SCREEN)
			{

			}
			else if (trackingtype == TRACKING_TYPE::WINDOW)
			{
				RECT trackedwindowrect;
				GetWindowRect(trackedwindow, &trackedwindowrect);
				SetWindowPos(window_handle, NULL, trackedwindowrect.left, trackedwindowrect.top, trackedwindowrect.right - trackedwindowrect.left, trackedwindowrect.bottom - trackedwindowrect.top, NULL);
			}

			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) && msg.message != WM_QUIT)
			{
				DispatchMessage(&msg);

				if (trackingtype == TRACKING_TYPE::WINDOW)
				{
					SendMessage(trackedwindow, msg.message, msg.wParam, msg.lParam);
				}
			}

			if (msg.message == WM_QUIT)
				break;

			if (s_resize_w != 0)
			{
				target.reset();
				backbuffer.reset();

				HR_CHECK(swapchain->ResizeBuffers(1, s_resize_w, s_resize_h, DXGI_FORMAT_UNKNOWN, 0));

				HR_CHECK(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)));
				HR_CHECK(device->CreateRenderTargetView(backbuffer.get(), nullptr, &target));

				s_resize_w = s_resize_h = 0;
			}

			const float color[4] = { BACKGROUND_COLOR };
			immediate_context->ClearRenderTargetView(target.get(), color);

			HR_CHECK(swapchain->Present(1, 0));
		}
	}
	#pragma endregion
		break;
	default:
		msg.wParam = EXIT_FAILURE;
		break;
	}

	reshade::hooks::uninstall();

	return static_cast<int>(msg.wParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
	if (__argc != 3)
	{
		return 1;
	}

	auto cmpscreen = strcmp(__argv[1], "SCREEN") == 0;
	auto cmpwindow = strcmp(__argv[1], "WINDOW") == 0;

	if (!cmpscreen && !cmpwindow)
	{
		return 1;
	}

	TRACKING_TYPE trackingtype = TRACKING_TYPE::NONE;
	int screenid = 0;
	HWND trackingwindow = NULL;

	if (cmpscreen)
	{
		trackingtype = TRACKING_TYPE::SCREEN;
		screenid = atoi(__argv[2]);
	}
	else if (cmpwindow)
	{
		trackingtype = TRACKING_TYPE::WINDOW;
		trackingwindow = (HWND)atoi(__argv[2]);
	}

	//TESTING WINDOW
	//trackingtype = TRACKING_TYPE::WINDOW;
	//trackingwindow = (HWND)1448712;

	return CreateAndRunWindow(trackingtype, screenid, trackingwindow);
}
