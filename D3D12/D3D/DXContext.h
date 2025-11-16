#pragma once

#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Util/Util.h"

#define DX_CONTEXT DXContext::Get()

class DXContext
{
public:
	DXContext(const DXContext&) = delete;
	DXContext& operator=(const DXContext&) = delete;

	inline static DXContext& Get()
	{
		static DXContext instance;
		return instance;
	}

private:
	DXContext() = default;

public:

	bool Init();
	void Shutdown();

	void SignalAndWait();
	ID3D12GraphicsCommandList7* InitCommandList();
	ID3D12GraphicsCommandList7* GetCommandList() { return m_cmdList; }
	void ExecuteCommandList();

	inline void Flush(size_t count)
	{
		for (size_t i = 0; i < count; i++)
		{
			SignalAndWait();
		}
	}
	inline ComPointer<IDXGIFactory7>& GetFactory() { return m_dxgiFactory; }
	inline ComPointer<ID3D12Device12>& GetDevice() { return m_device; }
	inline ComPointer<ID3D12CommandQueue>& GetCommandQueue() { return m_cmdQueue; }

private:
	ComPointer<IDXGIFactory7> m_dxgiFactory;

	ComPointer<ID3D12Device12> m_device; 
	ComPointer<ID3D12CommandQueue> m_cmdQueue;

	ComPointer <ID3D12CommandAllocator> m_cmdAllocator;
	ComPointer<ID3D12GraphicsCommandList7> m_cmdList;

	ComPointer<ID3D12Fence> m_fence;
	UINT64 m_fenceValue = 0;
	HANDLE m_fenceEvent = nullptr;

};

