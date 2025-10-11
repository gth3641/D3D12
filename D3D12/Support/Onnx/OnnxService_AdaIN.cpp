#include "OnnxService_AdaIN.h"
#include "Util/OnnxDefine.h"
#include "Manager/OnnxManager.h"
#include "Manager/DirectXManager.h"
#include "Support/Image.h"


static void WriteSceneSRVToSlot0(ID3D12Resource2* sceneColor, OnnxGPUResources* onnxGPUResource)
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.Format = sceneColor->GetDesc().Format;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;
	dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->SceneSRV_CPU);
}

static void WriteStyleSRVToSlot6(ID3D12Resource* styleTex, DXGI_FORMAT fmt, OnnxGPUResources* onnxGPUResource)
{
	if (!styleTex) return;
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.Format = fmt;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;
	DX_CONTEXT.GetDevice()->CreateShaderResourceView(styleTex, &s, onnxGPUResource->StyleSRV_CPU);
}



void OnnxService_AdaIN::RecordPreprocess_AdaIN(
	ID3D12GraphicsCommandList7* cmd, 
	ID3D12DescriptorHeap* heap, 
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	ID3D12Resource2* sceneColor,
	Image& styleImage
)
{
	if (heap == nullptr || onnxResource == nullptr || sceneColor == nullptr || onnxGPUResource == nullptr )
	{
		return;
	}

	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PrePSO.Get());

	const UINT Slice = (UINT)((sizeof(UINT) * 8 + 255) & ~255);

	// ----- (A) CONTENT -----
	{
		UINT inWc, inHc, inCc;
		const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,3,H,W]
		inCc = (UINT)ish[1]; inHc = (UINT)ish[2]; inWc = (UINT)ish[3];

		UINT flagsC = 0;
		auto fmtC = sceneColor->GetDesc().Format;
		if (fmtC == DXGI_FORMAT_B8G8R8A8_UNORM || fmtC == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			flagsC |= PRE_BGR_SWAP; // BGR swap

		struct CB { UINT W, H, C, Flags; } cb{ inWc, inHc, inCc, flagsC };
		uint8_t* base = nullptr;
		onnxGPUResource->CB->Map(0, nullptr, (void**)&base);
		memcpy(base + 0, &cb, sizeof(cb));
		onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress() + 0);

		// t0/u0
		WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
		cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);
		cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU);

		static D3D12_RESOURCE_STATES sInContentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (sInContentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			auto t = CD3DX12_RESOURCE_BARRIER::Transition(
				DX_ONNX.GetInputBufferContent().Get(),
				sInContentState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmd->ResourceBarrier(1, &t);
			sInContentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}

		if (inWc && inHc) {
			const UINT TG = 8;
			cmd->Dispatch((inWc + TG - 1) / TG, (inHc + TG - 1) / TG, 1);
			auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
			cmd->ResourceBarrier(1, &uav);
		}
	}

	// ----- (B) STYLE -----
	{
		ID3D12Resource* styleTex = styleImage.GetTexture();
		auto sDesc = styleTex->GetDesc();

		UINT inWs = 0, inHs = 0, inCs = 3;
		{
			const auto& ish = DX_ONNX.GetInputShapeStyle(); // [1,3,H,W]
			if (ish.size() == 4 && ish[2] > 0 && ish[3] > 0) {
				inCs = (UINT)ish[1]; inHs = (UINT)ish[2]; inWs = (UINT)ish[3];
			}
			else {
				inWs = (UINT)sDesc.Width; inHs = (UINT)sDesc.Height; inCs = 3;
			}
		}

		/*char dbg[256];
		sprintf_s(dbg, "[STYLE DISPATCH] W=%u H=%u C=%u (tex=%llu x %u)\n",
			inWs, inHs, inCs, (unsigned long long)sDesc.Width, sDesc.Height);
		OutputDebugStringA(dbg);*/

		UINT flags = 0;
		auto fmt = styleImage.GetTextureData().giPixelFormat;
		if (fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			flags |= PRE_BGR_SWAP;

		struct CB { UINT W, H, C, Flags; } cb{ inWs, inHs, inCs, flags };
		uint8_t* base = nullptr;
		onnxGPUResource->CB->Map(0, nullptr, (void**)&base);
		memcpy(base + Slice, &cb, sizeof(cb));
		onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress() + Slice);

		// StyleTex: PSR -> NPSR
		static D3D12_RESOURCE_STATES sStyleTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		if (sStyleTexState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
			auto b = CD3DX12_RESOURCE_BARRIER::Transition(
				styleTex, sStyleTexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cmd->ResourceBarrier(1, &b);
			sStyleTexState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}

		// In RecordPreprocess (STYLE section)
		WriteStyleSRVToSlot6(styleTex, styleImage.GetTextureData().giPixelFormat, onnxGPUResource);
		cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->StyleSRV_GPU);   // t0
		cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputStyleUAV_GPU); // u0

		// Ensure UAV state
		static D3D12_RESOURCE_STATES sInStyleState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (sInStyleState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			auto t = CD3DX12_RESOURCE_BARRIER::Transition(
				DX_ONNX.GetInputBufferStyle().Get(),
				sInStyleState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmd->ResourceBarrier(1, &t);
			sInStyleState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}

		// Dispatch preprocess (now planar CHW)
		const UINT TG = 8;
		cmd->Dispatch((inWs + TG - 1) / TG, (inHs + TG - 1) / TG, 1);
		auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferStyle().Get());
		cmd->ResourceBarrier(1, &uav);
	}
}

void OnnxService_AdaIN::RecordPostprocess_AdaIN(
	ID3D12GraphicsCommandList7* cmd, 
	ID3D12DescriptorHeap* heap, 
	OnnxPassResources* onnxResource, 
	OnnxGPUResources* onnxGPUResource,
	D3D12_RESOURCE_STATES& mOnnxTexState
)
{
	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PostPSO.Get());

	// 1) 실제 출력 shape (NCHW)
	const auto& osh = DX_ONNX.GetOutputShape();
	if (osh.size() != 4) return;
	const UINT srcC = (UINT)osh[1];
	const UINT srcH = (UINT)osh[2];
	const UINT srcW = (UINT)osh[3];

	// 2) 상태 전이: Output -> NPSR(SRV), OnnxTex -> UAV
	static D3D12_RESOURCE_STATES sOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (sOutputState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(),
			sOutputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		sOutputState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			onnxGPUResource->OnnxTex.Get(),
			mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// 3) ModelOut SRV(슬롯5) 요소 수 갱신
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN; // structured
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = srcW * srcH * srcC;
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		DX_CONTEXT.GetDevice()->CreateShaderResourceView(
			DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	// 4) CB 업데이트 (SrcW/H/C, DstW/H)
	const UINT dstW = onnxResource->Width;
	const UINT dstH = onnxResource->Height;

	struct CBData {
		UINT SrcW, SrcH, SrcC, _r0;
		UINT DstW, DstH, _r1, _r2;
		float Gain, Bias, _f0, _f1;
	};
	CBData cb{ srcW, srcH, srcC, 0, dstW, dstH, 0, 0, 1.0f, 0.0f, 0, 0 }; // ★ 먼저 크게

	void* p = nullptr;
	onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

	// 5) 바인딩: t0=ModelOut SRV, u0=OnnxTex UAV
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->OnnxTexUAV_GPU);

	// 6) 디스패치
	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((dstW + TGX - 1) / TGX, (dstH + TGY - 1) / TGY, 1);

	// 7) 상태 원복: Output -> UAV, OnnxTex -> PSR
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		sOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			onnxGPUResource->OnnxTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

}

void OnnxService_AdaIN::CreateOnnxResources_AdaIN(UINT W, UINT H,
	Image& styleImage,
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	ID3D12DescriptorHeap* heapCPU,
	ID3D12Resource2* sceneColor,
	D3D12_RESOURCE_STATES& mOnnxTexState,
	D3D12_RESOURCE_STATES& mOnnxInputState
)
{
	// ★ 스타일 텍스처 실제 크기 얻기
	ID3D12Resource* styleTex = styleImage.GetTexture();
	auto sDesc = styleTex->GetDesc();
	UINT styleW = (UINT)sDesc.Width;
	UINT styleH = sDesc.Height;

	// 1) ONNX IO 준비 (★ 4 인자)
	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H, styleW, styleH);

	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	// 2) 컴퓨트 파이프라인(전/후처리)
	if (!DX_MANAGER.CreateOnnxComputePipeline()) return;

	// 3) OnnxTex (최종 RGBA8)
	{
		D3D12_RESOURCE_DESC td{};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = W; td.Height = H;
		td.DepthOrArraySize = 1; td.MipLevels = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc = { 1,0 };
		td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&onnxGPUResource->OnnxTex));

		mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
	}

	// 4) 디스크립터 힙
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 12; // ★ 약간 여유
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&onnxGPUResource->Heap));

		D3D12_DESCRIPTOR_HEAP_DESC dCPU = d;
		dCPU.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dev->CreateDescriptorHeap(&dCPU, IID_PPV_ARGS(&heapCPU));
	}
	auto gpuStart = onnxGPUResource->Heap->GetGPUDescriptorHandleForHeapStart();
	auto cpuGPU = onnxGPUResource->Heap->GetCPUDescriptorHandleForHeapStart();
	auto cpuOnly = heapCPU->GetCPUDescriptorHandleForHeapStart();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto nthGPU = [&](UINT i) { auto h = gpuStart; h.ptr += i * inc; return h; };
	auto nthCPU_GPU = [&](UINT i) { auto h = cpuGPU;  h.ptr += i * inc; return h; };
	auto nthCPUONLY = [&](UINT i) { auto h = cpuOnly; h.ptr += i * inc; return h; };

	// (0) Scene SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = sceneColor->GetDesc().Format;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->SceneSRV_CPU = nthCPU_GPU(0);
		onnxGPUResource->SceneSRV_GPU = nthGPU(0);
		dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->SceneSRV_CPU);
	}
	// (1) InputContent UAV (structured float)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		auto bytes = DX_ONNX.GetInputBufferContent()->GetDesc().Width;
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		onnxGPUResource->InputContentUAV_CPU = nthCPU_GPU(1);
		onnxGPUResource->InputContentUAV_GPU = nthGPU(1);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferContent().Get(), nullptr, &u, onnxGPUResource->InputContentUAV_CPU);
	}
	// (2) InputStyle UAV (structured float)  바인딩용
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		auto bytes = DX_ONNX.GetInputBufferStyle()->GetDesc().Width;
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		onnxGPUResource->InputStyleUAV_CPU = nthCPU_GPU(2);
		onnxGPUResource->InputStyleUAV_GPU = nthGPU(2);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferStyle().Get(), nullptr, &u, onnxGPUResource->InputStyleUAV_CPU);
	}
	// (3) OnnxTex UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		onnxGPUResource->OnnxTexUAV_CPU = nthCPU_GPU(3);
		onnxGPUResource->OnnxTexUAV_GPU = nthGPU(3);
		dev->CreateUnorderedAccessView(onnxGPUResource->OnnxTex.Get(), nullptr, &u, onnxGPUResource->OnnxTexUAV_CPU);
	}
	// (4) OnnxTex SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->OnnxTexSRV_CPU = nthCPU_GPU(4);
		onnxGPUResource->OnnxTexSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(onnxGPUResource->OnnxTex.Get(), &s, onnxGPUResource->OnnxTexSRV_CPU);
	}
	// (5) ModelOut SRV (후처리에서 NumElements 갱신)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = 1;
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->ModelOutSRV_CPU = nthCPU_GPU(5);
		onnxGPUResource->ModelOutSRV_GPU = nthGPU(5);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}
	// (6) StyleTex SRV
	{
		onnxGPUResource->StyleSRV_CPU = nthCPU_GPU(6);
		onnxGPUResource->StyleSRV_GPU = nthGPU(6);
		// 실제 생성은 WriteStyleSRVToSlot6에서
	}
	// (7) InputContent SRV (debug)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		auto bytes = DX_ONNX.GetInputBufferContent()->GetDesc().Width;
		s.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->InputContentSRV_CPU = nthCPU_GPU(7);
		onnxGPUResource->InputContentSRV_GPU = nthGPU(7);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferContent().Get(), &s, onnxGPUResource->InputContentSRV_CPU);
	}
	// (8) InputStyle SRV (debug)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		auto bytes = DX_ONNX.GetInputBufferStyle()->GetDesc().Width;
		s.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->InputStyleSRV_CPU = nthCPU_GPU(8);
		onnxGPUResource->InputStyleSRV_GPU = nthGPU(8);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferStyle().Get(), &s, onnxGPUResource->InputStyleSRV_CPU);
	}
	// (9) RAW UAV(버퍼) ClearUAV용 핸들 (GPU/CPU heap에 쌍으로 생성)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uRaw{};
		uRaw.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uRaw.Format = DXGI_FORMAT_R32_TYPELESS;
		uRaw.Buffer.FirstElement = 0;
		auto bytes = DX_ONNX.GetInputBufferStyle()->GetDesc().Width;
		UINT elems = (UINT)(bytes / 4);
		uRaw.Buffer.NumElements = elems & ~3u; // 4의 배수로 내림
		uRaw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

		const UINT slot = 9;
		onnxGPUResource->InputStyleUAV_GPU_ForClear = nthGPU(slot);
		onnxGPUResource->InputStyleUAV_CPU_ForClear = nthCPUONLY(slot);

		// GPU-visible heap
		dev->CreateUnorderedAccessView(
			DX_ONNX.GetInputBufferStyle().Get(), nullptr, &uRaw, nthCPU_GPU(slot));
		// CPU-only heap
		dev->CreateUnorderedAccessView(
			DX_ONNX.GetInputBufferStyle().Get(), nullptr, &uRaw, onnxGPUResource->InputStyleUAV_CPU_ForClear);
	}

	// 5) CB
	{
		const UINT Slice = (UINT)((sizeof(UINT) * 8 + 255) & ~255);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(Slice * 2);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&onnxGPUResource->CB));
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	onnxResource->Width = W; onnxResource->Height = H;
}