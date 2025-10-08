#include "Window.h"
#include "Manager/OnnxManager.h"
#include "Manager/DirectXManager.h"
#include <comdef.h>



#pragma comment(lib, "User32.lib")

#ifdef UNICODE
#pragma message("UNICODE is defined")
#else
#pragma message("UNICODE is NOT defined")
#endif

bool DXWindow::Init()
{
	WNDCLASSEXW wcex{};

	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_OWNDC;
	wcex.lpfnWndProc = &DXWindow::OnWindowMessage;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = GetModuleHandleW(nullptr);
	wcex.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wcex.hbrBackground = nullptr;
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = L"D3D12ExWndCls";
	wcex.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

	m_wndClass = RegisterClassExW(&wcex);

	if (m_wndClass == 0)
	{
		return false;
	}

	//현재 위치 기준으로 스크린을 띄운다.
	POINT pos{ 0, 0 };
	GetCursorPos(&pos);
	HMONITOR monitor = MonitorFromPoint(pos, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	GetMonitorInfoW(monitor, &monitorInfo);

	m_window = CreateWindowExW(
		WS_EX_OVERLAPPEDWINDOW | WS_EX_APPWINDOW, 
		(LPCWSTR)m_wndClass, 
		L"D3D12", 
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
		monitorInfo.rcWork.left + 100,
		monitorInfo.rcWork.top + 100,
		1920, 
		1080, 
		nullptr, 
		nullptr, 
		wcex.hInstance, 
		nullptr);

	if (m_window == nullptr)
	{
		return false;
	}

	DXGI_SWAP_CHAIN_DESC1 swd{};
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC sfd{};

	swd.Width = 1920;
	swd.Height = 1080;
	swd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swd.Stereo = false;
	swd.SampleDesc.Count = 1;
	swd.SampleDesc.Quality = 0;
	swd.BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swd.BufferCount = 2;
	swd.Scaling = DXGI_SCALING_STRETCH;
	swd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;//DXGI_SWAP_EFFECT_DISCARD;
	swd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	swd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	sfd.Windowed = true;

	auto& factory = DX_CONTEXT.GetFactory();
	ComPointer<IDXGISwapChain1> sc1;
	HRESULT hr = factory->CreateSwapChainForHwnd(DX_CONTEXT.GetCommandQueue(), m_window, &swd, &sfd, nullptr, &sc1);
	if (!sc1.QueryInterface(m_swapChain))
	{
		return false;
	}
	
	// Create RTV Heap
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc{};
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	descHeapDesc.NumDescriptors = FrameCount;
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	descHeapDesc.NodeMask = 0;

	if (FAILED(DX_CONTEXT.GetDevice()->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&m_rtvDescHeap))))
	{
		return false;
	}

	// Create handle to view
	auto firstHandle = m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
	auto handleIncrement = DX_CONTEXT.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (size_t i = 0; i < FrameCount; ++i)
	{
		m_rtvHandles[i] = firstHandle;
		m_rtvHandles[i].ptr += handleIncrement * i;
	}

	// Get buffers
	if (GetBuffers() == false)
	{
		return false;
	}

	return true;
}

void DXWindow::Update()
{
	if (DX_WINDOW.ShouldResize())
	{
		DX_CONTEXT.Flush(DXWindow::GetFrameCount());

		DX_WINDOW.Resize();
		DX_MANAGER.Resize();

		DX_CONTEXT.Flush(DXWindow::GetFrameCount());
	}

	MessageUpdate();
	LogicUpdate();
}

void DXWindow::Present()
{
	m_swapChain->Present(1, 0);
	m_currentBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DXWindow::Shutdown()
{
	ReleaseBuffers();

	m_rtvDescHeap.Release();
	m_rtvDescHeap = nullptr;

	m_swapChain.Release();
	m_swapChain = nullptr;
	
	if (m_window)
	{
		DestroyWindow(m_window);
	}

	if (m_wndClass)
	{
		UnregisterClassW((LPCWSTR)m_wndClass, GetModuleHandleW(nullptr));
	}
}

void DXWindow::Resize()
{
	ReleaseBuffers();

	RECT cr;
	if (GetClientRect(m_window, &cr))
	{
		m_width = cr.right - cr.left;
		m_height = cr.bottom - cr.top;

		m_swapChain->ResizeBuffers(GetFrameCount(), m_width, m_height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
		m_shouldResize = false;

	}

	GetBuffers();
}

void DXWindow::SetFullScreen(bool enabled)
{
	//Update window size
	DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	DWORD exStyle = WS_EX_OVERLAPPEDWINDOW | WS_EX_APPWINDOW;

	if (enabled)
	{
		style = WS_POPUP | WS_VISIBLE;
		exStyle = WS_EX_APPWINDOW;
	}

	SetWindowLongW(m_window, GWL_STYLE, style);
	SetWindowLongW(m_window, GWL_EXSTYLE, exStyle);

	//Adjust window size
	if (enabled)
	{
		HMONITOR monitor = MonitorFromWindow(m_window, MONITOR_DEFAULTTONEAREST);
		MONITORINFO monitorInfo{};
		monitorInfo.cbSize = sizeof(monitorInfo);

		if (GetMonitorInfoW(monitor, &monitorInfo))
		{
			SetWindowPos(m_window, nullptr,
				monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.top,
				monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				SWP_NOZORDER
			);
		}
	}
	else
	{
		ShowWindow(m_window, SW_MAXIMIZE);
	}

	m_isFullscreen = enabled;
}

void DXWindow::BeginFrame(ID3D12GraphicsCommandList7* cmdList)
{

	D3D12_RESOURCE_BARRIER barr;
	barr.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barr.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barr.Transition.pResource = m_buffers[m_currentBufferIndex];
	barr.Transition.Subresource = 0;
	barr.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barr.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	cmdList->ResourceBarrier(1, &barr);

	float clearColor[] = { 1.0f, 1.0f, 1.f, 1.f };
	cmdList->ClearRenderTargetView(m_rtvHandles[m_currentBufferIndex], clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &m_rtvHandles[m_currentBufferIndex], false, nullptr);
}

void DXWindow::EndFrame(ID3D12GraphicsCommandList7* cmdList)
{
	D3D12_RESOURCE_BARRIER barr;
	barr.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barr.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barr.Transition.pResource = m_buffers[m_currentBufferIndex];
	barr.Transition.Subresource = 0;
	barr.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barr.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	cmdList->ResourceBarrier(1, &barr);
}

D3D12_VIEWPORT DXWindow::CreateViewport()
{
	D3D12_VIEWPORT vp{};

	vp.TopLeftX = vp.TopLeftY = 0.f;
	vp.Width = (FLOAT)GetWidth();
	vp.Height = (FLOAT)GetHeight();
	vp.MinDepth = 0.f;
	vp.MaxDepth = 1.f;

	return vp;
}

RECT DXWindow::CreateScissorRect()
{
	RECT scRect;
	scRect.left = scRect.top = 0;
	scRect.right = GetWidth();
	scRect.bottom = GetHeight();

	return scRect;
}

void DXWindow::UpdateBackBuffer()
{
	m_currentBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

ComPointer<ID3D12Resource2> DXWindow::GetBackbuffer() const
{
	return m_buffers[m_currentBufferIndex];
}

void DXWindow::GetBackbufferSize(UINT& outW, UINT& outH) const
{
	ID3D12Resource* back = GetBackbuffer();
	auto desc = back->GetDesc();
	outW = static_cast<UINT>(desc.Width);
	outH = desc.Height;
}

D3D12_CPU_DESCRIPTOR_HANDLE DXWindow::GetRtvHandle(size_t index)
{
	return m_rtvHandles[index];
}


void DXWindow::MessageUpdate()
{
	MSG msg;

	while (PeekMessageW(&msg, m_window, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

void DXWindow::LogicUpdate()
{
	DX_MANAGER.Update();
}

bool DXWindow::GetBuffers()
{
	for (size_t i = 0; i < FrameCount; ++i)
	{
		if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_buffers[i]))))
		{
			return false;
		}

		D3D12_RENDER_TARGET_VIEW_DESC rtv{};
		rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtv.Texture2D.MipSlice = 0;
		rtv.Texture2D.PlaneSlice = 0;

		DX_CONTEXT.GetDevice()->CreateRenderTargetView(m_buffers[i], &rtv, m_rtvHandles[i]);
	}

	UpdateBackBuffer();

	return true;
}

void DXWindow::ReleaseBuffers()
{
	for (size_t i = 0; i < FrameCount; ++i)
	{
		m_buffers[i].Release();
	}
}

LRESULT DXWindow::OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_KEYDOWN:
		{
			if (wParam == VK_F11)
			{
				Get().SetFullScreen(!Get().IsFullscreen());
			}
			break;
		}

		case WM_SIZE:
		{
			if (lParam && (HIWORD(lParam) != Get().m_width || LOWORD(lParam) != Get().m_height))
			{
				DX_WINDOW.m_shouldResize = true;
			}
			break;
		}

		case WM_CLOSE:
		{
			DX_WINDOW.m_shouldClose = true;
			return 0;
		}


		default:
			break;
	}



	return DefWindowProcW(hwnd, msg, wParam, lParam);
}
