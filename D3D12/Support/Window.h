#pragma once

#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "D3D/DXContext.h"


#define DX_WINDOW DXWindow::Get()

class DXWindow
{
public:
	bool Init();
	void Update();
	void Present();
	void Shutdown();
	void Resize();
	void SetFullScreen(bool enabled);

	void BeginFrame(ID3D12GraphicsCommandList7* cmdList);
	void EndFrame(ID3D12GraphicsCommandList7* cmdList);

	D3D12_VIEWPORT CreateViewport();
	RECT CreateScissorRect();

	inline bool ShouldClose() const { return m_shouldClose; }
	inline bool ShouldResize() const { return m_shouldResize; }
	inline bool IsFullscreen() const { return m_isFullscreen; }

	inline UINT GetWidth() const { return m_width; }
	inline UINT GetHeight() const { return m_height; }

	static constexpr size_t FrameCount = 2;
	static constexpr size_t GetFrameCount()
	{
		return FrameCount;
	}

	void UpdateBackBuffer();

	ComPointer<ID3D12Resource2> GetBackbuffer() const;
	void GetBackbufferSize(UINT& outW, UINT& outH) const;

	size_t GetBackBufferIndex() { return m_currentBufferIndex; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle(size_t index);

	ComPointer<ID3D12DescriptorHeap> GetRtvDescHeap() { return m_rtvDescHeap; }

private:
	void MessageUpdate();
	void LogicUpdate();

	bool GetBuffers();
	void ReleaseBuffers();

	static LRESULT CALLBACK OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
	ATOM m_wndClass = 0;
	HWND m_window = nullptr;
	bool m_shouldClose = false;

	bool m_shouldResize = false;
	UINT m_width = 1920;
	UINT m_height = 1080;

	bool m_isFullscreen = false;

	ComPointer<IDXGISwapChain3> m_swapChain;
	ComPointer<ID3D12Resource2> m_buffers[FrameCount];
	size_t m_currentBufferIndex = 0;

	ComPointer<ID3D12DescriptorHeap> m_rtvDescHeap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[FrameCount];

public: // Singleton pattern to ensure only one instance exists 
	DXWindow(const DXWindow&) = delete;
	DXWindow& operator=(const DXWindow&) = delete;

	inline static DXWindow& Get()
	{
		static DXWindow instance;
		return instance;
	}

private:
	DXWindow() = default;

};


