#include "OnnxService_Sanet.h"
#include "Util/OnnxDefine.h"

#include "Manager/OnnxManager.h"
#include "Manager/DirectXManager.h"

#include "Support/Image.h"
#include "Support/Shader.h"

enum class NormProfile { Raw01, ImageNet, CaffeBGR255, TanhIn };
static NormProfile kProfile = NormProfile::Raw01; 
static bool kUseBGR = false; 

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

static void WritePtSRVToSlot1(ID3D12Resource2* ptTex, OnnxGPUResources* onnxGPUResource)
{
	if (!ptTex) return;
	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.Format = ptTex->GetDesc().Format;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;
	dev->CreateShaderResourceView(ptTex, &s, onnxGPUResource->PtSRV_CPU); // slot1 갱신
}


void OnnxService_Sanet::RecordPreprocess_Sanet(
	ID3D12GraphicsCommandList7* cmd,
	ID3D12DescriptorHeap* heap,
	Image& styleImage,
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	ID3D12Resource2* sceneColor       
)
{
	if (!heap || !onnxResource || !onnxGPUResource || !sceneColor) return;

	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PrePSO.Get());

	// 공통 플래그
	UINT flags = 0;
	if (kUseBGR) flags |= PRE_BGR_SWAP;
	auto fmtC = sceneColor->GetDesc().Format;
	if (fmtC == DXGI_FORMAT_B8G8R8A8_UNORM || fmtC == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) flags |= PRE_BGR_SWAP;
	if (fmtC == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || fmtC == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) flags |= LINEAR_TO_SRGB;

	const bool twoInputs = true;

	if (twoInputs) {
		// ---- Pass #1 : Content -> InputContent (C=3)
		{
			const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,3,H,W]
			const UINT inW = (UINT)ish[3], inH = (UINT)ish[2], inC = 3;
			
			PreCBData cb{ inW,inH,inC,flags,0,0,0,0 };
			void* p = nullptr; onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
			cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

			// t0 = content, u0 = InputContent
			WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
			cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);
			cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU);

			auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
			cmd->ResourceBarrier(1, &uav);
			const UINT TG = 8; cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);
			uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
			cmd->ResourceBarrier(1, &uav);
		}
		// ---- Pass #2 : Style -> InputStyle (C=3)
		{
			ID3D12Resource2* styleTex = styleImage.GetTexture().Get();
			const auto& ishS = DX_ONNX.GetInputShapeStyle(); // [1,3,Hs,Ws]
			const UINT inW = (UINT)ishS[3], inH = (UINT)ishS[2], inC = 3;

			PreCBData cb{ inW,inH,inC,flags,0,0,0,0 };
			void* p = nullptr; onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
			cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

			WriteSceneSRVToSlot0(styleTex, onnxGPUResource);              // t0 = style
			cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);
			cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputStyleUAV_GPU); // u0 = style buffer

			auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferStyle());
			cmd->ResourceBarrier(1, &uav);
			const UINT TG = 8; cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);
			uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferStyle());
			cmd->ResourceBarrier(1, &uav);
		}
		return;
	}

	// ─── 1입력(C=6) 병합 모델 경로 ───
	{
		const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,6,H,W]
		const UINT inW = (UINT)ish[3], inH = (UINT)ish[2], inC = (UINT)ish[1];
		WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
		WritePtSRVToSlot1(styleImage.GetTexture().Get(), onnxGPUResource);
		PreCBData cb{ inW,inH,inC,flags,0,0,0,0 };
		void* p = nullptr; onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

		cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);        // SRV range(t0,t1)
		cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU); // u0

		auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
		cmd->ResourceBarrier(1, &uav);
		const UINT TG = 8; cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);
		uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
		cmd->ResourceBarrier(1, &uav);
	}
}

void OnnxService_Sanet::RecordPostprocess_Sanet(
	ID3D12GraphicsCommandList7* cmd,
	ID3D12DescriptorHeap* heap,
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	D3D12_RESOURCE_STATES& mOnnxTexState
)
{
	if (!heap || !onnxResource || !onnxGPUResource) return;

	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PostPSO.Get());

	const auto& osh = DX_ONNX.GetOutputShape(); // [1,3,H,W]
	if (osh.size() != 4) return;
	const UINT srcC = (UINT)osh[1], srcH = (UINT)osh[2], srcW = (UINT)osh[3];

	static D3D12_RESOURCE_STATES sOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (sOutputState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(), sOutputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		sOutputState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			onnxGPUResource->OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// ModelOut SRV (NumElements = W*H*C)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = srcW * srcH * srcC;
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		DX_CONTEXT.GetDevice()->CreateShaderResourceView(
			DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	UINT flags = 0;

	PostCBData cb
	{ 
		srcW, srcH, srcC, flags,
		onnxResource->Width, onnxResource->Height, 0,0, 
		1.0f, 0.0f, 0.f, 0.f
	};

	void* p = nullptr; onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

	// t0=ModelOut, u0=OnnxTex
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->OnnxTexUAV_GPU);

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((onnxResource->Width + TGX - 1) / TGX, (onnxResource->Height + TGY - 1) / TGY, 1);

	auto b0 = CD3DX12_RESOURCE_BARRIER::Transition(
		DX_ONNX.GetOutputBuffer().Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmd->ResourceBarrier(1, &b0);
	sOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(
		onnxGPUResource->OnnxTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &b1);
	mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void OnnxService_Sanet::CreateOnnxResources_Sanet(UINT W, UINT H,
	Image& styleImage,
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	ID3D12DescriptorHeap* heapCPU,
	ID3D12Resource2* sceneColor,
	D3D12_RESOURCE_STATES& mOnnxTexState,
	D3D12_RESOURCE_STATES& mOnnxInputState
)
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	// style 이미지 실제 크기
	UINT sW = W, sH = H;
	if (auto* st = styleImage.GetTexture().Get()) { auto d = st->GetDesc(); sW = (UINT)d.Width; sH = d.Height; }

	// 2-입력 모델이면 style 크기까지 넘김
	DX_ONNX.PrepareIO(dev, W, H, sW, sH);

	if (!DX_MANAGER.CreateOnnxComputePipeline()) return;

	// OnnxTex (FP16)
	{
		D3D12_RESOURCE_DESC td{};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = W; td.Height = H; td.DepthOrArraySize = 1; td.MipLevels = 1;
		td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		td.SampleDesc = { 1,0 };
		td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&onnxGPUResource->OnnxTex));
		mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
	}

	// descriptor heap (shader-visible)
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 8;
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&onnxGPUResource->Heap));
	}
	auto gpuStart = onnxGPUResource->Heap->GetGPUDescriptorHandleForHeapStart();
	auto cpuStart = onnxGPUResource->Heap->GetCPUDescriptorHandleForHeapStart();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto nthGPU = [&](UINT i) { auto h = gpuStart; h.ptr += i * inc; return h; };
	auto nthCPU = [&](UINT i) { auto h = cpuStart; h.ptr += i * inc; return h; };

	// (0) Scene SRV (t0)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = sceneColor->GetDesc().Format;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->SceneSRV_CPU = nthCPU(0);
		onnxGPUResource->SceneSRV_GPU = nthGPU(0);
		dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->SceneSRV_CPU);
	}

	// (1) Style SRV (t0로 사용)
	{
		onnxGPUResource->StyleSRV_CPU = nthCPU(1);
		onnxGPUResource->StyleSRV_GPU = nthGPU(1);
		if (auto* st = styleImage.GetTexture().Get()) {
			D3D12_SHADER_RESOURCE_VIEW_DESC s{};
			s.Format = st->GetDesc().Format;
			s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			s.Texture2D.MipLevels = 1;
			dev->CreateShaderResourceView(st, &s, onnxGPUResource->StyleSRV_CPU);
		}
		else {
			dev->CreateShaderResourceView(nullptr, nullptr, onnxGPUResource->StyleSRV_CPU);
		}
	}

	// (2) InputContent UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		auto bytes = DX_ONNX.GetInputBufferContent()->GetDesc().Width;
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		onnxGPUResource->InputContentUAV_CPU = nthCPU(2);
		onnxGPUResource->InputContentUAV_GPU = nthGPU(2);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferContent().Get(), nullptr, &u, onnxGPUResource->InputContentUAV_CPU);
	}

	// (3) InputStyle UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		auto buf = DX_ONNX.GetInputBufferStyle();
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = buf ? (UINT)(buf->GetDesc().Width / sizeof(float)) : 0;
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		onnxGPUResource->InputStyleUAV_CPU = nthCPU(3);
		onnxGPUResource->InputStyleUAV_GPU = nthGPU(3);
		dev->CreateUnorderedAccessView(buf, nullptr, &u, onnxGPUResource->InputStyleUAV_CPU);
	}

	// (4) ModelOut SRV (후처리에서 NumElements 갱신)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = 1;
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		onnxGPUResource->ModelOutSRV_CPU = nthCPU(4);
		onnxGPUResource->ModelOutSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	// (5) OnnxTex UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		onnxGPUResource->OnnxTexUAV_CPU = nthCPU(5);
		onnxGPUResource->OnnxTexUAV_GPU = nthGPU(5);
		dev->CreateUnorderedAccessView(onnxGPUResource->OnnxTex.Get(), nullptr, &u, onnxGPUResource->OnnxTexUAV_CPU);
	}

	// (6) OnnxTex SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->OnnxTexSRV_CPU = nthCPU(6);
		onnxGPUResource->OnnxTexSRV_GPU = nthGPU(6);
		dev->CreateShaderResourceView(onnxGPUResource->OnnxTex.Get(), &s, onnxGPUResource->OnnxTexSRV_CPU);
	}

	// (7) CB
	{
		const UINT kCBAligned = ((sizeof(UINT) * 8 + 255) & ~255);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(kCBAligned);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&onnxGPUResource->CB));
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	onnxResource->Width = W;
	onnxResource->Height = H;
}