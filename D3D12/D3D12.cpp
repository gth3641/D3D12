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

#include "Util/stb_image_write.h"

///==================================================================//
// Debug Macros
//===================================================================//
#define DEBUG_DUMP 0
#define DEBUG_TIME 0
#define DEBUG_PRINT_TIME 0
#define DEBUG_PRINT_IMG 0


//===================================================================//
// Resource Selection
//===================================================================//
#define OBJ_RES RES_GALLERY
#define USE_KEYBOARD 1
constexpr OnnxType ONNX_TYPE = OnnxType::FastNeuralStyle;


#if DEBUG_TIME
#define DEBUG_TIME_EXPR(OUT_NAME)\
    endSecStart = std::chrono::system_clock::now() - startTime;\
    Util::Print((float)endSecStart.count(), OUT_NAME);
#else
#define DEBUG_TIME_EXPR(OUT_NAME)  ((void)0)
#endif

#if   (OBJ_RES == RES_SPONZA)
extern const char* OBJ_RES_PTH = "./Resources/Sponza_A";
extern const char* OBJ_RES_NAME = "./Resources/Sponza_A/sponza.obj";
extern const int OBJ_RES_NUM = 1;
extern const int MAX_FRAME = 480;
#elif (OBJ_RES == RES_SAN_MIGUEL)
extern const char* OBJ_RES_PTH = "./Resources/San_Miguel";
extern const char* OBJ_RES_NAME = "./Resources/San_Miguel/san-miguel.obj";
extern const int OBJ_RES_NUM = 3;
extern const int MAX_FRAME = 240;
#elif (OBJ_RES == RES_GALLERY)
extern const char* OBJ_RES_PTH = "./Resources/gallery";
extern const char* OBJ_RES_NAME = "./Resources/gallery/gallery.obj";
extern const int OBJ_RES_NUM = 4;
extern const int MAX_FRAME = 480;
#elif (OBJ_RES == RES_ISCV2)
extern const char* OBJ_RES_PTH = "./Resources/bedroom";
extern const char* OBJ_RES_NAME = "./Resources/bedroom/iscv2.obj";
extern const int OBJ_RES_NUM = 5;
extern const int MAX_FRAME = 470;
#else
extern const char* OBJ_RES_PTH = "";
extern const char* OBJ_RES_NAME = "";
extern const int OBJ_RES_NUM = 0;
extern const int MAX_FRAME = 480;
#endif

extern int FrameNum = 0;

struct ReadbackDump {
	ComPointer<ID3D12Resource> readback;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
	UINT64 totalBytes = 0;     // ★ 추가
	UINT width = 0, height = 0;
};

static ReadbackDump EnqueueCopyToReadback(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmd,
	ID3D12Resource* srcTex,
	D3D12_RESOURCE_STATES srcStateBefore,
	UINT width, UINT height)
{
	ReadbackDump out{};
	out.width = width; out.height = height;

	auto desc = srcTex->GetDesc();
	assert(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

	UINT64 totalBytes = 0;
	device->GetCopyableFootprints(&desc, 0, 1, 0, &out.fp, nullptr, nullptr, &totalBytes);
	out.totalBytes = totalBytes;
	if (out.totalBytes == 0) {
		OutputDebugStringA("[Dump] totalBytes is 0. Check texture desc/MSAA.\n");
	}

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
	CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(out.totalBytes);
	(device->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&out.readback)));

	// 상태 전환 → 복사 → 원복
	CD3DX12_RESOURCE_BARRIER b1 = CD3DX12_RESOURCE_BARRIER::Transition(srcTex, srcStateBefore, D3D12_RESOURCE_STATE_COPY_SOURCE);
	cmd->ResourceBarrier(1, &b1);

	D3D12_TEXTURE_COPY_LOCATION dst{};
	dst.pResource = out.readback.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = out.fp;

	D3D12_TEXTURE_COPY_LOCATION src{};
	src.pResource = srcTex;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;

	cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	CD3DX12_RESOURCE_BARRIER b2 = CD3DX12_RESOURCE_BARRIER::Transition(srcTex, D3D12_RESOURCE_STATE_COPY_SOURCE, srcStateBefore);
	cmd->ResourceBarrier(1, &b2);

	return out;
}

static void SaveReadbackPNG(ReadbackDump& dump, const std::string& outPath)
{
	void* mapped = nullptr;
	D3D12_RANGE readRange{ 0, (SIZE_T)dump.totalBytes };
	HRESULT hr = dump.readback->Map(0, &readRange, &mapped);
	if (FAILED(hr) || mapped == nullptr) {
		char buf[256];
		std::snprintf(buf, sizeof(buf), "[Dump] Map failed or null. hr=0x%08X, totalBytes=%llu\n",
			(unsigned)hr, (unsigned long long)dump.totalBytes);
		OutputDebugStringA(buf);
		return;
	}

	const uint8_t* src = static_cast<const uint8_t*>(mapped);
	const UINT rowPitch = dump.fp.Footprint.RowPitch;
	const UINT W = dump.width, H = dump.height;

	std::vector<uint8_t> rgba(W * H * 4);
	for (UINT y = 0; y < H; ++y) {
		const uint8_t* row = src + y * rowPitch; // BGRA
		for (UINT x = 0; x < W; ++x) {
			size_t si = x * 4, di = (size_t(y) * W + x) * 4;
			rgba[di + 0] = row[si + 0]; // R
			rgba[di + 1] = row[si + 1]; // G
			rgba[di + 2] = row[si + 2]; // B
			rgba[di + 3] = row[si + 3]; // A
		}
	}
	dump.readback->Unmap(0, nullptr);

	stbi_write_png(outPath.c_str(), W, H, 4, rgba.data(), W * 4);
}


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
#if USE_KEYBOARD
	FrameNum = -1;
#elif
	FrameNum = 0;
#endif // USE_KEYBOARD

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
#if DEBUG_PRINT_TIME
		printf("%f, ", deltaTime);
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

			// === 여기서 백버퍼를 Readback으로 복사 '기록' ===
			ReadbackDump dump{};
			if (FrameNum < MAX_FRAME) 
			{
				dump = EnqueueCopyToReadback(
					DX_CONTEXT.GetDevice(),
					cmd,
					DX_WINDOW.GetBackbuffer().Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET,
					DX_WINDOW.GetWidth(),
					DX_WINDOW.GetHeight());
			}

			DEBUG_TIME_EXPR("BlitToBackbuffer");
			DX_CONTEXT.ExecuteCommandList();

#if DEBUG_PRINT_IMG
			if (FrameNum < MAX_FRAME)
			{
				char buf[64];
				sprintf_s(buf, "./Export/output%03d.png", FrameNum);  // buf에 결과 문자열 저장

				if (dump.readback) {
					DX_CONTEXT.SignalAndWait();
					SaveReadbackPNG(dump, buf);
				}
			}
#endif

#if USE_KEYBOARD
#elif
			++FrameNum;
#endif // USE_KEYBOARD

			DEBUG_TIME_EXPR("ExecuteCommandList");

            DX_CONTEXT.SignalAndWait();
			DX_WINDOW.Present();
			DEBUG_TIME_EXPR("ONNX END");


		}
	}

	Shutdown();
    return 0;
}

