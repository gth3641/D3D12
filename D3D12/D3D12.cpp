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


int main()
{
    if (DX_CONTEXT.Init() && DX_WINDOW.Init())
    {
        // (A) ONNX 초기화는 '한 곳'에서만. 여기서 하려면 ↓ 유지하고 DX_Manager 쪽은 삭제
        DX_ONNX.Init(L"./Resources/Onnx/udnie-9.onnx",
            DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue());

        DX_IMAGE.Init();
        DX_MANAGER.Init();
        { auto* c = DX_CONTEXT.InitCommandList(); DX_MANAGER.UploadGPUResource(c); DX_CONTEXT.ExecuteCommandList(); }

        while (!DX_WINDOW.ShouldClose())
        {
            DX_WINDOW.Update();
            DX_MANAGER.BeginFrame();

            //ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
            //// 1) 전처리 제출
            //{
            //    cmd = DX_CONTEXT.InitCommandList();
            //    DX_MANAGER.RenderOffscreen(cmd);
            //    DX_MANAGER.RecordPreprocess(cmd);   // 내부에서 Input: UAV→GENERIC_READ까지 처리
            //    DX_CONTEXT.ExecuteCommandList();
            //}

            //// 2) ORT 실행
            //DX_ONNX.Run();

            //// 3) 큐 동기화
            //DX_CONTEXT.SignalAndWait();

            //// 4) 후처리 + 블릿 + 프레젠트
            //{
            //    cmd = DX_CONTEXT.InitCommandList();
            //    DX_WINDOW.BeginFrame(cmd);        
            //    DX_MANAGER.RecordPostprocess(cmd);
            //    DX_MANAGER.BlitToBackbuffer(cmd);
            //    DX_WINDOW.EndFrame(cmd);
            //    DX_CONTEXT.ExecuteCommandList();
            //    DX_WINDOW.Present();
            //}

            auto* cmd = DX_CONTEXT.InitCommandList();

            DX_MANAGER.RenderOffscreen(cmd);      // Scene RTV -> 끝에서 NPSR로 전환
            DX_MANAGER.RecordPreprocess(cmd);     // Scene SRV -> Input UAV -> UAV barrier -> GENERIC_READ
            DX_CONTEXT.ExecuteCommandList();      // 네 Execute가 내부 Wait까지 하니 OK

            DX_ONNX.Run();                        // DML 실행 (Input/Output에 기록)

            cmd = DX_CONTEXT.InitCommandList();
            DX_MANAGER.RecordPostprocess(cmd);    // Output SRV -> OnnxTex UAV -> (업샘플/후처리) -> OnnxTex PSR
            DX_WINDOW.BeginFrame(cmd);            // 백버퍼 RTV 세팅 + 클리어
            DX_MANAGER.BlitToBackbuffer(cmd);     // ★ OnnxTex를 화면에 풀스크린 블릿
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


//int main()
//{
//	DX_DEBUG_LAYER.Init();
//
//	if (DX_CONTEXT.Init() == true && DX_WINDOW.Init() == true)
//	{
//		if (DX_IMAGE.Init() == false)
//		{
//			return -1;
//		}
//
//		if (DX_MANAGER.Init() == false)
//		{
//			return -1;
//		}
//
//		//DX_ONNX.RunTest();
//		//bool basd = DX_ONNX.RunCpuSmokeTest();
//
//		// Copy CPU Resource --> GPU Resource
//		ID3D12GraphicsCommandList7* cmdList = DX_CONTEXT.InitCommandList();
//
//		if (DX_ONNX.Init(L"./Resources/Onnx/udnie-9.onnx",DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue()) == false)
//		{
//			return -1;
//		}
//
//		DX_MANAGER.UploadGPUResource(cmdList);
//		DX_CONTEXT.ExecuteCommandList();
//		//bool asdf = DX_ONNX.RunGpuSmokeTest(DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue());
//
//		// === Vertex buffer view ===
//		D3D12_VERTEX_BUFFER_VIEW vbv1 = DirectXManager::GetVertexBufferView(DX_MANAGER.GetRenderingObject1().GetVertexBuffer(), DX_MANAGER.GetRenderingObject1().GetVertexCount(), sizeof(Vertex));
//		D3D12_VERTEX_BUFFER_VIEW vbv2 = DirectXManager::GetVertexBufferView(DX_MANAGER.GetRenderingObject2().GetVertexBuffer(), DX_MANAGER.GetRenderingObject2().GetVertexCount(), sizeof(Vertex));
//
//		DX_WINDOW.SetFullScreen(false);
//		DX_MANAGER.CreateOnnxResources(DX_WINDOW.GetWidth(), DX_WINDOW.GetHeight());
//
//		while (DX_WINDOW.ShouldClose() == false)
//		{
//			//Process pending window message
//			DX_WINDOW.Update();
//
//			//Begin drawing
//			cmdList = DX_CONTEXT.InitCommandList();
//
//			//Draw
//			DX_WINDOW.BegineFrame(cmdList);
//
//			// === PSO ===
//			cmdList->SetPipelineState(DX_MANAGER.GetPipelineStateObj());
//			cmdList->SetGraphicsRootSignature(DX_MANAGER.GetRootSignature());
//			// === IA ===
//
//
//			cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//			// === RS ===
//			D3D12_VIEWPORT vp = DX_WINDOW.CreateViewport();
//			RECT scRect = DX_WINDOW.CreateScissorRect();
//			cmdList->RSSetScissorRects(1, &scRect);
//			cmdList->RSSetViewports(1, &vp);
//
//			// === OM ===
//			static float bf_ff = 0.f;
//			float bf[] = { bf_ff , bf_ff , bf_ff , bf_ff };
//			cmdList->OMSetBlendFactor(bf);
//
//			// === Update ===
//			static  float color[] = { 0.f, 0.f, 0.f };
//
//			struct ScreenCB
//			{
//				float ViewSize[2];
//			};
//			ScreenCB scb{ (float)DX_WINDOW.GetWidth(), (float)DX_WINDOW.GetHeight() };
//
//			// === ROOT ===
//			cmdList->SetGraphicsRoot32BitConstants(0, 3, color, 0);
//			cmdList->SetGraphicsRoot32BitConstants(1, 2, &scb, 0);
//
//			// === Draw ===
//			ID3D12DescriptorHeap* srvHeap = DX_IMAGE.GetSrvheap();
//			cmdList->SetDescriptorHeaps(1, &srvHeap);
//
//			cmdList->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject1().GetTestIndex()));
//			cmdList->IASetVertexBuffers(0, 1, &vbv1);
//			cmdList->DrawInstanced(DX_MANAGER.GetRenderingObject1().GetVertexCount(), 1, 0, 0);
//
//			cmdList->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject2().GetTestIndex()));
//			cmdList->IASetVertexBuffers(0, 1, &vbv2);
//			cmdList->DrawInstanced(DX_MANAGER.GetRenderingObject2().GetVertexCount(), 1, 0, 0);
//
//			DX_MANAGER.RecordOnnxPass(cmdList); // ← 전처리→Run→후처리→합성
//
//			DX_WINDOW.EndFrame(cmdList);
//
//			// a lot of setup
//			// a draw
//
//			//Finish drawing and present
//			DX_CONTEXT.ExecuteCommandList();
//			DX_WINDOW.Present();
//			// Show me stuff
//
//		}
//
//		DX_CONTEXT.Flush(DXWindow::GetFrameCount());
//
//		// Close
//
//		DX_MANAGER.Shutdown();
//		DX_IMAGE.Shutdown();
//		DX_ONNX.Shutdown();
//		DX_WINDOW.Shutdown();
//		DX_CONTEXT.Shutdown();
//	}
//
//	DX_DEBUG_LAYER.Shutdown();
//
//	return 0;
//}



//while (!DX_WINDOW.ShouldClose())
//{
//    // 0) 윈도우 메시지 처리(반드시!)
//    bool resize = DX_WINDOW.ShouldResize();
//    DX_WINDOW.Update();
//
//    // 1) 리사이즈 대응 등 프레임 사전 처리
//    DX_MANAGER.BeginFrame(resize);
//
//    // 2) 오프스크린 렌더 + Readback로 복사까지 커맨드 기록/제출
//    {
//        ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
//        DX_WINDOW.BegineFrame(cmd);
//        // === PSO ===
//        cmd->SetPipelineState(DX_MANAGER.GetPipelineStateObj());
//        cmd->SetGraphicsRootSignature(DX_MANAGER.GetRootSignature());
//        // === IA ===
//
//
//        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//        // === RS ===
//        D3D12_VIEWPORT vp = DX_WINDOW.CreateViewport();
//        RECT scRect = DX_WINDOW.CreateScissorRect();
//        cmd->RSSetScissorRects(1, &scRect);
//        cmd->RSSetViewports(1, &vp);
//
//        // === OM ===
//        static float bf_ff = 0.f;
//        float bf[] = { bf_ff , bf_ff , bf_ff , bf_ff };
//        cmd->OMSetBlendFactor(bf);
//
//        // === Update ===
//        static  float color[] = { 0.f, 0.f, 0.f };
//
//        struct ScreenCB
//        {
//            float ViewSize[2];
//        };
//        ScreenCB scb{ (float)DX_WINDOW.GetWidth(), (float)DX_WINDOW.GetHeight() };
//
//        // === ROOT ===
//        cmd->SetGraphicsRoot32BitConstants(0, 3, color, 0);
//        cmd->SetGraphicsRoot32BitConstants(1, 2, &scb, 0);
//
//        // === Draw ===
//        ID3D12DescriptorHeap* srvHeap = DX_IMAGE.GetSrvheap();
//        cmd->SetDescriptorHeaps(1, &srvHeap);
//
//        cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject1().GetTestIndex()));
//        cmd->IASetVertexBuffers(0, 1, &DX_MANAGER.vbv1);
//        cmd->DrawInstanced(DX_MANAGER.GetRenderingObject1().GetVertexCount(), 1, 0, 0);
//
//        cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject2().GetTestIndex()));
//        cmd->IASetVertexBuffers(0, 1, &DX_MANAGER.vbv2);
//        cmd->DrawInstanced(DX_MANAGER.GetRenderingObject2().GetVertexCount(), 1, 0, 0);
//        //
//
//        DX_WINDOW.EndFrame(cmd);
//        DX_CONTEXT.ExecuteCommandList();     // 제출
//        DX_WINDOW.Present();
//    }
//
//
//}