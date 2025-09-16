#include <iostream>

#include "Support/ImageLoader.h"
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Support/Shader.h"
#include "Support/Window.h"

#include "Manager/DirectXManager.h"

#include "DebugD3D12/DebugLayer.h"

#include "D3D/DXContext.h"
#include "Util/Util.h"

int main()
{
	DX_DEBUG_LAYER.Init();

	if (DX_CONTEXT.Init() == true && DX_WINDOW.Init() == true)
	{
		if (DX_MANAGER.Init() == false)
		{
			return -1;
		}

		// Copy CPU Resource --> GPU Resource
		ID3D12GraphicsCommandList7* cmdList = DX_CONTEXT.InitCommandList();
		DX_MANAGER.UploadGPUResource(cmdList);
		DX_CONTEXT.ExecuteCommandList();

		// === Vertex buffer view ===
		D3D12_VERTEX_BUFFER_VIEW vbv1 = DirectXManager::GetVertexBufferView(DX_MANAGER.GetRenderingObject1().GetVertexBuffer(), DX_MANAGER.GetRenderingObject1().GetVertexCount(), sizeof(Vertex));
		D3D12_VERTEX_BUFFER_VIEW vbv2 = DirectXManager::GetVertexBufferView(DX_MANAGER.GetRenderingObject2().GetVertexBuffer(), DX_MANAGER.GetRenderingObject2().GetVertexCount(), sizeof(Vertex));

		DX_WINDOW.SetFullScreen(false);
		while (DX_WINDOW.ShouldClose() == false)
		{
			//Process pending window message
			DX_WINDOW.Update();

			//Begin drawing
			cmdList = DX_CONTEXT.InitCommandList();

			//Draw
			DX_WINDOW.BegineFrame(cmdList);

			// === PSO ===
			cmdList->SetPipelineState(DX_MANAGER.GetPipelineStateObj());
			cmdList->SetGraphicsRootSignature(DX_MANAGER.GetRootSignature());
			ID3D12DescriptorHeap* heaps[] = { DX_MANAGER.GetRenderingObject1().GetSrvheap() , DX_MANAGER.GetRenderingObject2().GetSrvheap() };
			//cmdList->SetDescriptorHeaps(1, &DX_MANAGER.GetRenderingObject1().GetSrvheap());
			// === IA ===
			//cmdList->IASetVertexBuffers(0, 1, &vbv1);
			cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			// === RS ===
			D3D12_VIEWPORT vp = DX_WINDOW.CreateViewport();
			RECT scRect = DX_WINDOW.CreateScissorRect();
			cmdList->RSSetScissorRects(1, &scRect);
			cmdList->RSSetViewports(1, &vp);

			// === OM ===
			static float bf_ff = 0.f;
			float bf[] = { bf_ff , bf_ff , bf_ff , bf_ff };
			cmdList->OMSetBlendFactor(bf);

			// === Update ===
			static  float color[] = { 0.f, 0.f, 0.f };

			struct ScreenCB
			{
				float ViewSize[2];
			};
			ScreenCB scb{ (float)DX_WINDOW.GetWidth(), (float)DX_WINDOW.GetHeight() };

			// === ROOT ===
			cmdList->SetGraphicsRoot32BitConstants(0, 3, color, 0);
			cmdList->SetGraphicsRoot32BitConstants(1, 2, &scb, 0);
			//cmdList->SetGraphicsRootDescriptorTable(2, DX_MANAGER.GetRenderingObject1().GetSrvheap()->GetGPUDescriptorHandleForHeapStart());

			// === Draw ===
			//cmdList->DrawInstanced(DX_MANAGER.GetRenderingObject1().GetVertexCount(), 1, 0, 0);

			ID3D12DescriptorHeap* h1 = DX_MANAGER.GetRenderingObject1().GetSrvheap();
			cmdList->SetDescriptorHeaps(1, &h1);
			cmdList->SetGraphicsRootDescriptorTable(2, h1->GetGPUDescriptorHandleForHeapStart());
			cmdList->IASetVertexBuffers(0, 1, &vbv1);
			cmdList->DrawInstanced(DX_MANAGER.GetRenderingObject1().GetVertexCount(), 1, 0, 0);

			ID3D12DescriptorHeap* h2 = DX_MANAGER.GetRenderingObject2().GetSrvheap();
			cmdList->SetDescriptorHeaps(1, &h2);
			cmdList->SetGraphicsRootDescriptorTable(2, h2->GetGPUDescriptorHandleForHeapStart());
			cmdList->IASetVertexBuffers(0, 1, &vbv2);
			cmdList->DrawInstanced(DX_MANAGER.GetRenderingObject2().GetVertexCount(), 1, 0, 0);



			DX_WINDOW.EndFrame(cmdList);

			// a lot of setup
			// a draw

			//Finish drawing and present
			DX_CONTEXT.ExecuteCommandList();
			DX_WINDOW.Preset();
			// Show me stuff

		}

		DX_CONTEXT.Flush(DXWindow::GetFrameCount());

		// Close

		DX_MANAGER.Shutdown();
		DX_WINDOW.Shutdown();
		DX_CONTEXT.Shutdown();
	}

	DX_DEBUG_LAYER.Shutdown();

	return 0;
}
