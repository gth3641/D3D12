#include "OnnxService_AdaIN.h"
#include "Util/OnnxDefine.h"

#include "Manager/OnnxManager.h"
#include "Manager/DirectXManager.h"

#include "Support/Image.h"
#include "Support/Shader.h"


static void WriteSceneSRVToSlot0(ID3D12Resource2* sceneColor, OnnxGPUResources* onnxGPUResource)
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.Format = sceneColor->GetDesc().Format;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;
	dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->m_SceneSRV_CPU);
}

static void WriteStyleSRVToSlot6(ID3D12Resource* styleTex, DXGI_FORMAT fmt, OnnxGPUResources* onnxGPUResource)
{
	if (!styleTex) return;
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.Format = fmt;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;
	DX_CONTEXT.GetDevice()->CreateShaderResourceView(styleTex, &s, onnxGPUResource->m_StyleSRV_CPU);
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
	cmd->SetComputeRootSignature(onnxResource->m_PreRS.Get());
	cmd->SetPipelineState(onnxResource->m_PrePSO.Get());

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

		PreCBData cb
		{ 
			inWc, inHc, inCc, flagsC,
			0, 0, 0, 0
		};

		uint8_t* base = nullptr;
		onnxGPUResource->m_CB->Map(0, nullptr, (void**)&base);
		memcpy(base + 0, &cb, sizeof(cb));
		onnxGPUResource->m_CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->m_CB->GetGPUVirtualAddress() + 0);

		// t0/u0
		WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
		cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->m_SceneSRV_GPU);
		cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->m_InputContentUAV_GPU);

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

		UINT flags = 0;
		auto fmt = styleImage.GetTextureData().giPixelFormat;
		if (fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			flags |= PRE_BGR_SWAP;

		PreCBData cb
		{ 
			inWs, inHs, inCs, flags,
			0, 0, 0, 0
		};
		uint8_t* base = nullptr;
		onnxGPUResource->m_CB->Map(0, nullptr, (void**)&base);
		memcpy(base + Slice, &cb, sizeof(cb));
		onnxGPUResource->m_CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->m_CB->GetGPUVirtualAddress() + Slice);

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
		cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->m_StyleSRV_GPU);   // t0
		cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->m_InputStyleUAV_GPU); // u0

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
	cmd->SetComputeRootSignature(onnxResource->m_PreRS.Get());
	cmd->SetPipelineState(onnxResource->m_PostPSO.Get());

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
			onnxGPUResource->m_OnnxTex.Get(),
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
			DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->m_ModelOutSRV_CPU);
	}

	// 4) CB 업데이트 (SrcW/H/C, DstW/H)
	const UINT dstW = onnxResource->m_Width;
	const UINT dstH = onnxResource->m_Height;

	UINT Flags = 0;
	if (DX_ONNX.GetOnnxType() == OnnxType::Sanet)
	{
		Flags |= 0x1;
	}

	PostCBData cb
	{
		srcW, srcH, srcC, Flags,
		dstW, dstH, 0, 0, 
		1.0f, 0.0f, 0, 0 
	}; 

	void* p = nullptr;
	onnxGPUResource->m_CB->Map(0, nullptr, &p); 
	std::memcpy(p, &cb, sizeof(cb)); 
	onnxGPUResource->m_CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->m_CB->GetGPUVirtualAddress());
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->m_ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->m_OnnxTexUAV_GPU);

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((dstW + TGX - 1) / TGX, (dstH + TGY - 1) / TGY, 1);

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
			onnxGPUResource->m_OnnxTex.Get(),
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
	ID3D12Resource* styleTex = styleImage.GetTexture();
	auto sDesc = styleTex->GetDesc();
	UINT styleW = (UINT)sDesc.Width;
	UINT styleH = sDesc.Height;

	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H, styleW, styleH);

	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	if (!DX_MANAGER.CreateOnnxComputePipeline()) return;
	// OnnxTex 
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
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&onnxGPUResource->m_OnnxTex));

		mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
	}

	// 디스크립터 힙
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 12; 
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&onnxGPUResource->m_Heap));

		D3D12_DESCRIPTOR_HEAP_DESC dCPU = d;
		dCPU.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dev->CreateDescriptorHeap(&dCPU, IID_PPV_ARGS(&heapCPU));
	}
	auto gpuStart = onnxGPUResource->m_Heap->GetGPUDescriptorHandleForHeapStart();
	auto cpuGPU = onnxGPUResource->m_Heap->GetCPUDescriptorHandleForHeapStart();
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
		onnxGPUResource->m_SceneSRV_CPU = nthCPU_GPU(0);
		onnxGPUResource->m_SceneSRV_GPU = nthGPU(0);
		dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->m_SceneSRV_CPU);
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

		onnxGPUResource->m_InputContentUAV_CPU = nthCPU_GPU(1);
		onnxGPUResource->m_InputContentUAV_GPU = nthGPU(1);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferContent().Get(), nullptr, &u, onnxGPUResource->m_InputContentUAV_CPU);
	}
	// (2) InputStyle UAV (structured float)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		auto bytes = DX_ONNX.GetInputBufferStyle()->GetDesc().Width;
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		onnxGPUResource->m_InputStyleUAV_CPU = nthCPU_GPU(2);
		onnxGPUResource->m_InputStyleUAV_GPU = nthGPU(2);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferStyle().Get(), nullptr, &u, onnxGPUResource->m_InputStyleUAV_CPU);
	}
	// (3) OnnxTex UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		onnxGPUResource->m_OnnxTexUAV_CPU = nthCPU_GPU(3);
		onnxGPUResource->m_OnnxTexUAV_GPU = nthGPU(3);
		dev->CreateUnorderedAccessView(onnxGPUResource->m_OnnxTex.Get(), nullptr, &u, onnxGPUResource->m_OnnxTexUAV_CPU);
	}
	// (4) OnnxTex SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->m_OnnxTexSRV_CPU = nthCPU_GPU(4);
		onnxGPUResource->m_OnnxTexSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(onnxGPUResource->m_OnnxTex.Get(), &s, onnxGPUResource->m_OnnxTexSRV_CPU);
	}
	// (5) ModelOut SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = 1;
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->m_ModelOutSRV_CPU = nthCPU_GPU(5);
		onnxGPUResource->m_ModelOutSRV_GPU = nthGPU(5);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->m_ModelOutSRV_CPU);
	}
	// (6) StyleTex SRV
	{
		onnxGPUResource->m_StyleSRV_CPU = nthCPU_GPU(6);
		onnxGPUResource->m_StyleSRV_GPU = nthGPU(6);
	}
	// (7) InputContent SRV
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

		onnxGPUResource->m_InputContentSRV_CPU = nthCPU_GPU(7);
		onnxGPUResource->m_InputContentSRV_GPU = nthGPU(7);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferContent().Get(), &s, onnxGPUResource->m_InputContentSRV_CPU);
	}
	// (8) InputStyle SRV
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

		onnxGPUResource->m_InputStyleSRV_CPU = nthCPU_GPU(8);
		onnxGPUResource->m_InputStyleSRV_GPU = nthGPU(8);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferStyle().Get(), &s, onnxGPUResource->m_InputStyleSRV_CPU);
	}
	// (9) RAW UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uRaw{};
		uRaw.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uRaw.Format = DXGI_FORMAT_R32_TYPELESS;
		uRaw.Buffer.FirstElement = 0;
		auto bytes = DX_ONNX.GetInputBufferStyle()->GetDesc().Width;
		UINT elems = (UINT)(bytes / 4);
		uRaw.Buffer.NumElements = elems & ~3u;
		uRaw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

		const UINT slot = 9;
		onnxGPUResource->m_InputStyleUAV_GPU_ForClear = nthGPU(slot);
		onnxGPUResource->m_InputStyleUAV_CPU_ForClear = nthCPUONLY(slot);

		dev->CreateUnorderedAccessView(
			DX_ONNX.GetInputBufferStyle().Get(), nullptr, &uRaw, nthCPU_GPU(slot));
		dev->CreateUnorderedAccessView(
			DX_ONNX.GetInputBufferStyle().Get(), nullptr, &uRaw, onnxGPUResource->m_InputStyleUAV_CPU_ForClear);
	}

	// 10) CB
	{
		const UINT Slice = (UINT)((sizeof(UINT) * 8 + 255) & ~255);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(Slice * 2);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&onnxGPUResource->m_CB));
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	onnxResource->m_Width = W; onnxResource->m_Height = H;
}