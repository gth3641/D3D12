#include <iostream>

#include "Support/ImageLoader.h"
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Support/Shader.h"
#include "Support/Window.h"

#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"
#include "Manager/OnnxManager.h"
#include "Manager/InputManager.h"

#include "DebugD3D12/DebugLayer.h"
#include "Util/Util.h"

#include "D3D/DXContext.h"

#include <chrono>
#include <cmath>

#define DEBUG_DUMP 0
#define DEBUG_TIME 1

static constexpr bool kDebugFillOnnxTex = false;		// 이전 단계: 텍스처 채우기
static constexpr bool kDebugPostprocessOnly = false;		// 이번 단계: 후처리만 단독 검증

int main()
{
	if (DX_INPUT.Init() == false)
	{
		return -1;
	}

	if (DX_CONTEXT.Init() == false)
	{
		return -1;
	}

	if (DX_WINDOW.Init() == false)
	{
		return -1;
	}

	if (DX_ONNX.Init(OnnxType::Sanet, DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue()) == false)
	{
		return -1;
	}

	std::chrono::system_clock::time_point preTick = std::chrono::system_clock::now();
	std::chrono::system_clock::time_point tick = preTick;

    DX_IMAGE.Init();
    DX_MANAGER.Init();
    { auto* c = DX_CONTEXT.InitCommandList(); DX_MANAGER.UploadGPUResource(c); DX_CONTEXT.ExecuteCommandList(); }

    while (!DX_WINDOW.ShouldClose())
    {
		preTick = tick;
		tick = std::chrono::system_clock::now();
		std::chrono::duration<float>sec = tick - preTick;
		float deltaTime = sec.count();

		std::chrono::system_clock::time_point startTime = std::chrono::system_clock::now();
		std::chrono::duration<float> endSecStart = std::chrono::system_clock::now() - startTime;

		bool updated = DX_WINDOW.MessageUpdate(deltaTime);

        DX_WINDOW.Update(deltaTime);
		//Util::Print(deltaTime, "TICK");
		{
			ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
			DX_MANAGER.RenderOffscreen(cmd);
			DX_MANAGER.RecordPreprocess(cmd);          
			DX_CONTEXT.ExecuteCommandList();        

			if(kDebugFillOnnxTex == true)
			{
				ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
				DX_MANAGER.Debug_FillOnnxTex(cmd);         // ← OnnxTex UAV에 그라디언트 채우기
				DX_MANAGER.BlitToBackbuffer(cmd);          // ← 화면으로 블릿
				DX_CONTEXT.ExecuteCommandList();
				DX_WINDOW.Present();
				continue; // ONNX.Run / RecordPostprocess 건너뛰기
			}

#if DEBUG_TIME
			endSecStart = std::chrono::system_clock::now() - startTime;
			Util::Print((float)endSecStart.count(), "ONNX START");
#endif
#if DEBUG_DUMP
			DX_MANAGER.Debug_DumpBuffer(DX_ONNX.GetInputBufferContent().Get(), "CONTENT");
			DX_MANAGER.Debug_DumpBuffer(DX_MANAGER.mPrevStylized.Get(), "PRE STYLE");
			DX_MANAGER.Debug_DumpBuffer(DX_ONNX.GetInputBufferStyle().Get(), "STYLE");
#endif
		}

		if (kDebugPostprocessOnly)
		{
			ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
			DX_MANAGER.Debug_PostprocessOnly(cmd);   // ← 후처리만 실행 (가짜 CHW 입력)
			DX_MANAGER.BlitToBackbuffer(cmd);        // ← 결과 화면으로
			DX_CONTEXT.ExecuteCommandList();
			DX_WINDOW.Present();
			continue; // ONNX.Run / RecordPostprocess 생략
		}
		

		//{
		//	// 3) 전처리 결과 시각화 (모자이크)
		//	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
		//	DX_MANAGER.Debug_ViewPreprocessCHW(cmd);
		//	DX_MANAGER.BlitToBackbuffer(cmd);
		//	DX_CONTEXT.ExecuteCommandList();
		//	DX_WINDOW.Present();
		//	continue; // ONNX.Run / RecordPostprocess 생략

		//}

		DX_ONNX.Run();
		//DX_CONTEXT.SignalAndWait();

#if DEBUG_TIME
		endSecStart = std::chrono::system_clock::now() - startTime;
		Util::Print((float)endSecStart.count(), "ONNX RUNNING");
#endif

		{
			ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
			DX_MANAGER.RecordPostprocess(cmd);

#if DEBUG_TIME
			endSecStart = std::chrono::system_clock::now() - startTime;
			Util::Print((float)endSecStart.count(), "RecordPostprocess");
#endif

			DX_MANAGER.BlitToBackbuffer(cmd);

#if DEBUG_TIME
			endSecStart = std::chrono::system_clock::now() - startTime;
			Util::Print((float)endSecStart.count(), "BlitToBackbuffer");
#endif

			DX_CONTEXT.ExecuteCommandList();

#if DEBUG_TIME
			endSecStart = std::chrono::system_clock::now() - startTime;
			Util::Print((float)endSecStart.count(), "ExecuteCommandList");
#endif

			DX_WINDOW.Present();

#if DEBUG_TIME
			endSecStart = std::chrono::system_clock::now() - startTime;
			Util::Print((float)endSecStart.count(), "ONNX END");
#endif
		}
	}

    DX_MANAGER.Shutdown();
    DX_IMAGE.Shutdown();
    DX_ONNX.Shutdown();
    DX_WINDOW.Shutdown();
    DX_CONTEXT.Shutdown();
	DX_INPUT.Shutdown();

    DX_DEBUG_LAYER.Shutdown();
    return 0;
}

