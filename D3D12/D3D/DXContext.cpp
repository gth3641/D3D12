#include "DXContext.h"
#include <iostream>

bool DXContext::Init()
{
	HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_dxgiFactory));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Factory device. " << hr << std::endl;
		return false;
	}

	// 1. Create the D3D12 device
	hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12 device. " << hr << std::endl;
		return false;
	}

	// 2. Create command queue
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
	cmdQueueDesc.NodeMask = 0;
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	hr = m_device->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&m_cmdQueue));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create command queue. " << hr << std::endl;
		return false;
	}

	// 3. Create a fence for synchronization
	hr = m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create fence. " << hr << std::endl;
		return false;
	}

	// 4. Create an event handle for the fence
	m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);
	if (m_fenceEvent == nullptr)
	{
		std::cerr << "Failed to create fence event." << std::endl;
		return false;
	}

	// 5. Create a command allocator
	hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocator));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create command allocator. " << hr << std::endl;
		return false;
	}

	// 6. Create a command list
	hr = m_device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_cmdList));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create command list. " << hr << std::endl;
		return false;
	}



	return true;
}


void DXContext::Shutdown()
{
	m_cmdList.Release();
	m_cmdList = nullptr;

	m_cmdAllocator.Release();
	m_cmdAllocator = nullptr;

	if (m_fenceEvent != nullptr)
	{
		CloseHandle(m_fenceEvent);
	}
	m_fenceEvent = nullptr;
	m_fence.Release();
	m_fence = nullptr;

	m_cmdQueue.Release();
	m_cmdQueue = nullptr;

	m_device.Release();
	m_device = nullptr;

	m_dxgiFactory.Release();
	m_dxgiFactory = nullptr;
}

void DXContext::SignalAndWait()
{
	m_cmdQueue->Signal(m_fence, ++m_fenceValue);
	if (SUCCEEDED(m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent)))
	{
		if (WaitForSingleObject(m_fenceEvent, 20000) != WAIT_OBJECT_0)
		{
			std::exit(EXIT_FAILURE);
		}
	}
	else
	{
		std::exit(EXIT_FAILURE);
	}

}

ID3D12GraphicsCommandList7* DXContext::InitCommandList()
{
	m_cmdAllocator->Reset();
	m_cmdList->Reset(m_cmdAllocator, nullptr);

	return m_cmdList;
}

void DXContext::ExecuteCommandList()
{
	HRESULT hr = m_cmdList->Close();
	if (FAILED(hr))
	{
		return;
	}

	ID3D12CommandList* lists[] = { m_cmdList };
	m_cmdQueue->ExecuteCommandLists(1, lists);

	SignalAndWait();
}
