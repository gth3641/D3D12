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

#if DEBUG_TIME
#define DEBUG_TIME_EXPR(OUT_NAME)\
    endSecStart = std::chrono::system_clock::now() - startTime;\
    Util::Print((float)endSecStart.count(), OUT_NAME);
#else
#define DEBUG_TIME_EXP(OUT_NAME)  ((void)0)
#endif

constexpr OnnxType ONNX_TYPE = OnnxType::FastNeuralStyle;

void Shutdown()
{
	DX_MANAGER.Shutdown();
	DX_IMAGE.Shutdown();
	DX_ONNX.Shutdown();
	DX_WINDOW.Shutdown();
	DX_CONTEXT.Shutdown();
	DX_INPUT.Shutdown();

	DX_DEBUG_LAYER.Shutdown();
}

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

	if (DX_ONNX.Init(ONNX_TYPE, DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue()) == false)
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
#if DEBUG_TIME
		Util::Print(deltaTime, "TICK");
#endif
		{
			ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
			DX_MANAGER.RenderOffscreen(cmd);
			DX_MANAGER.RecordPreprocess(cmd);          
			DX_CONTEXT.ExecuteCommandList();        

			DEBUG_TIME_EXPR("ONNX START");

#if DEBUG_DUMP
			DX_MANAGER.Debug_DumpBuffer(DX_ONNX.GetInputBufferContent().Get(), "CONTENT");
			DX_MANAGER.Debug_DumpBuffer(DX_ONNX.GetInputBufferStyle().Get(), "STYLE");
#endif
		}

		DX_ONNX.Run();
		DEBUG_TIME_EXPR("ONNX RUNNING");

		{
			ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
			DX_MANAGER.RecordPostprocess(cmd);
			DEBUG_TIME_EXPR("RecordPostprocess");
			DX_MANAGER.BlitToBackbuffer(cmd);
			DEBUG_TIME_EXPR("BlitToBackbuffer");
			DX_CONTEXT.ExecuteCommandList();
			DEBUG_TIME_EXPR("ExecuteCommandList");
			DX_WINDOW.Present();
			DEBUG_TIME_EXPR("ONNX END");
		}
	}

	Shutdown();
    return 0;
}

