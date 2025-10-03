#include <iostream>

#include "Support/ImageLoader.h"
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Support/Shader.h"
#include "Support/Window.h"

#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"
#include "Manager/OnnxManager.h"

#include "DebugD3D12/DebugLayer.h"

#include "D3D/DXContext.h"
#include "Util/Util.h"


#define USE_ONNX

#ifdef USE_ONNX
int main()
{
    if (DX_CONTEXT.Init() && DX_WINDOW.Init())
    {
        DX_ONNX.Init(L"./Resources/Onnx/udnie-9.onnx",
            DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue());

        DX_IMAGE.Init();
        DX_MANAGER.Init();
        { auto* c = DX_CONTEXT.InitCommandList(); DX_MANAGER.UploadGPUResource(c); DX_CONTEXT.ExecuteCommandList(); }

        while (!DX_WINDOW.ShouldClose())
        {
            DX_WINDOW.Update();

            auto* cmd = DX_CONTEXT.InitCommandList();

            DX_MANAGER.RenderOffscreen(cmd);     
            DX_MANAGER.RecordPreprocess(cmd);    
            DX_CONTEXT.ExecuteCommandList();     

            DX_ONNX.Run();                       

            cmd = DX_CONTEXT.InitCommandList();
            DX_MANAGER.RecordPostprocess(cmd);   
            DX_WINDOW.BeginFrame(cmd);           
            DX_MANAGER.BlitToBackbuffer(cmd);    
            DX_WINDOW.EndFrame(cmd);
            DX_CONTEXT.ExecuteCommandList();
            DX_WINDOW.Present();
        }

        DX_MANAGER.Shutdown();
        DX_IMAGE.Shutdown();
        DX_ONNX.Shutdown();
        DX_WINDOW.Shutdown();
        DX_CONTEXT.Shutdown();
    }

    DX_DEBUG_LAYER.Shutdown();
    return 0;
}

#else
int main()
{
	DX_DEBUG_LAYER.Init();

	if (DX_CONTEXT.Init() == true && DX_WINDOW.Init() == true)
	{
		if (DX_IMAGE.Init() == false)
		{
			return -1;
		}

		if (DX_MANAGER.Init() == false)
		{
			return -1;
		}

		// Copy CPU Resource --> GPU Resource
		ID3D12GraphicsCommandList7* cmdList = DX_CONTEXT.InitCommandList();

		if (DX_ONNX.Init(L"./Resources/Onnx/udnie-9.onnx",DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue()) == false)
		{
			return -1;
		}

		DX_MANAGER.UploadGPUResource(cmdList);
		//bool asdf = DX_ONNX.RunGpuSmokeTest(DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue());

		// === Vertex buffer view ===

		//DX_WINDOW.SetFullScreen(false);
		DX_MANAGER.CreateOnnxResources(DX_WINDOW.GetWidth(), DX_WINDOW.GetHeight());

		while (DX_WINDOW.ShouldClose() == false)
		{
			//Process pending window message
			DX_WINDOW.Update();

			D3D12_VERTEX_BUFFER_VIEW vbv1 = DirectXManager::GetVertexBufferView(
				DX_MANAGER.GetRenderingObject1().GetVertexBuffer(), 
				DX_MANAGER.GetRenderingObject1().GetVertexCount(), 
				sizeof(Vertex));
			D3D12_VERTEX_BUFFER_VIEW vbv2 = DirectXManager::GetVertexBufferView(
				DX_MANAGER.GetRenderingObject2().GetVertexBuffer(), 
				DX_MANAGER.GetRenderingObject2().GetVertexCount(), 
				sizeof(Vertex));

			//Begin drawing
			cmdList = DX_CONTEXT.InitCommandList();

			//Draw
			DX_WINDOW.BeginFrame(cmdList);
			DX_MANAGER.UploadGPUResource(cmdList);
			// === PSO ===
			cmdList->SetPipelineState(DX_MANAGER.GetPipelineStateObj());
			cmdList->SetGraphicsRootSignature(DX_MANAGER.GetRootSignature());
			// === IA ===


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

			// === Draw ===
			ID3D12DescriptorHeap* srvHeap = DX_IMAGE.GetSrvheap();
			cmdList->SetDescriptorHeaps(1, &srvHeap);

			cmdList->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject2().GetTestIndex()));
			cmdList->IASetVertexBuffers(0, 1, &vbv2);
			cmdList->DrawInstanced(DX_MANAGER.GetRenderingObject2().GetVertexCount(), 1, 0, 0);

			cmdList->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject1().GetTestIndex()));
			cmdList->IASetVertexBuffers(0, 1, &vbv1);
			cmdList->DrawInstanced(DX_MANAGER.GetRenderingObject1().GetVertexCount(), 1, 0, 0);

			DX_WINDOW.EndFrame(cmdList);

			// a lot of setup
			// a draw

			//Finish drawing and present
			DX_CONTEXT.ExecuteCommandList();
			DX_WINDOW.Present();
			// Show me stuff

		}

		DX_CONTEXT.Flush(DXWindow::GetFrameCount());

		// Close

		DX_MANAGER.Shutdown();
		DX_IMAGE.Shutdown();
		DX_ONNX.Shutdown();
		DX_WINDOW.Shutdown();
		DX_CONTEXT.Shutdown();
	}

	DX_DEBUG_LAYER.Shutdown();

	return 0;
}
#endif // USE_ONNX

