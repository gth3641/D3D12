#include <iostream>

#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Support/Window.h"

#include "DebugD3D12/DebugLayer.h"

#include "D3D/DXContext.h"

int main()
{
	DX_DEBUG_LAYER.Init();

	if (DX_CONTEXT.Init() == true && DX_WINDOW.Init() == true)
	{
		const char* hello = "Hello World!";

		D3D12_HEAP_PROPERTIES hpUpload{};
		hpUpload.Type = D3D12_HEAP_TYPE_UPLOAD;
		hpUpload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		hpUpload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		hpUpload.CreationNodeMask = 0;
		hpUpload.VisibleNodeMask = 0;

		D3D12_HEAP_PROPERTIES hpDefault{};
		hpDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
		hpDefault.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		hpDefault.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		hpDefault.CreationNodeMask = 0;
		hpDefault.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
		rd.Width = 1024;
		rd.Height = 1;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count = 1;
		rd.SampleDesc.Quality = 0;
		rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		rd.Flags = D3D12_RESOURCE_FLAG_NONE;

		ComPointer<ID3D12Resource2> uploadBuffer, vertexBuffer;
		DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpUpload, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&uploadBuffer));
		DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vertexBuffer));
		
		// Copy void* --> CPU Resource
		void* uploadBufferAddress;
		D3D12_RANGE uploadRange;
		uploadRange.Begin = 0;
		uploadRange.End = 1023;
		uploadBuffer->Map(0, &uploadRange, &uploadBufferAddress);
		memcpy(uploadBufferAddress, hello, strlen(hello) + 1);
		uploadBuffer->Unmap(0, &uploadRange);

		// Copy CPU Resource --> GPU Resource
		auto* cmdList = DX_CONTEXT.InitCommandList();
		cmdList->CopyBufferRegion(vertexBuffer, 0, uploadBuffer, 0, 1024);
		DX_CONTEXT.ExecuteCommandList();

		DX_WINDOW.SetFullScreen(true);
		while (DX_WINDOW.ShouldClose() == false)
		{
			//Process pending window message
			DX_WINDOW.Update();

			// Handle resizing
			if (DX_WINDOW.ShouldResize())
			{
				DX_CONTEXT.Flush(DXWindow::GetFrameCount());
				DX_WINDOW.Resize();
			}

			//Begin drawing
			cmdList = DX_CONTEXT.InitCommandList();

			//Draw
			DX_WINDOW.BegineFrame(cmdList);
			DX_WINDOW.EndFrame(cmdList);



			// a lot of setup
			// a draw

			//Finish drawing and present
			DX_CONTEXT.ExecuteCommandList();
			DX_WINDOW.Preset();
			// Show me stuff

		}

		DX_CONTEXT.Flush(DXWindow::GetFrameCount());

		DX_WINDOW.Shutdown();
		DX_CONTEXT.Shutdown();
	}

	DX_DEBUG_LAYER.Shutdown();
}
