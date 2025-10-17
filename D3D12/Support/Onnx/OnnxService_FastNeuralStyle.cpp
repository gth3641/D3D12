#include "OnnxService_FastNeuralStyle.h"
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
	dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->SceneSRV_CPU);
}


void OnnxService_FastNeuralStyle::RecordPreprocess_FastNeuralStyle(
	ID3D12GraphicsCommandList7* cmd, 
	ID3D12DescriptorHeap* heap, 
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	ID3D12Resource2* sceneColor,
	Image& styleImage
)
{
	if (heap == nullptr || onnxResource == nullptr || sceneColor == nullptr || onnxGPUResource == nullptr)
		return;

	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PrePSO.Get());

	// 256바이트 정렬된 CB 크기
	const UINT kCBAligned = ((sizeof(UINT) * 8 + 255) & ~255);

	UINT inWc = 0, inHc = 0, inCc = 0;
	{
		const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,3,H,W]
		inCc = (UINT)ish[1]; inHc = (UINT)ish[2]; inWc = (UINT)ish[3];

		UINT flagsC = PRE_MUL_255;
		auto fmtC = sceneColor->GetDesc().Format;
		if (fmtC == DXGI_FORMAT_B8G8R8A8_UNORM || fmtC == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			flagsC |= PRE_BGR_SWAP; // BGR 스왑

		if (fmtC == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || fmtC == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			flagsC |= LINEAR_TO_SRGB;
		
		PreCBData cb
		{ 
			inWc, inHc, inCc, flagsC,
			0, 0, 0, 0
		};
		uint8_t* base = nullptr;
		onnxGPUResource->CB->Map(0, nullptr, (void**)&base);
		std::memcpy(base + 0, &cb, sizeof(cb)); // 오프셋 0만 사용
		onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress() + 0);

		// t0/u0 바인딩
		WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
		cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);       // t0
		cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU);// u0

		// UAV 상태 보장
		static D3D12_RESOURCE_STATES sInContentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		if (sInContentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
			auto t = CD3DX12_RESOURCE_BARRIER::Transition(
				DX_ONNX.GetInputBufferContent().Get(),
				sInContentState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmd->ResourceBarrier(1, &t);
			sInContentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}

		// 디스패치
		if (inWc && inHc) {
			const UINT TG = 8;
			cmd->Dispatch((inWc + TG - 1) / TG, (inHc + TG - 1) / TG, 1);
			auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
			cmd->ResourceBarrier(1, &uav);
		}
	}
}

void OnnxService_FastNeuralStyle::RecordPostprocess_FastNeuralStyle(
	ID3D12GraphicsCommandList7* cmd, 
	ID3D12DescriptorHeap* heap, 
	OnnxPassResources* onnxResource, 
	OnnxGPUResources* onnxGPUResource,
	D3D12_RESOURCE_STATES& mOnnxTexState
)
{
	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());  // 단일 RS 사용
	cmd->SetPipelineState(onnxResource->PostPSO.Get());

	// 1) 출력 shape (NCHW)
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

	// 3) ModelOut SRV 갱신 (요소 수 = W*H*C)
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

	// 4) CB 업데이트 (SrcW/H/C, DstW/H, Gain/Bias)
	const UINT dstW = onnxResource->Width;
	const UINT dstH = onnxResource->Height;

	/*struct CBData {
		UINT SrcW, SrcH, SrcC, Flags;
		UINT DstW, DstH, _r1, _r2;
		float Gain, Bias, _f0, _f1;
	};*/
	PostCBData cb{ srcW, srcH, srcC, 0x10, dstW, dstH, 0, 0, 1.0f, 0.0f, 0, 0 };

	void* p = nullptr;
	onnxGPUResource->CB->Map(0, nullptr, &p);
	std::memcpy(p, &cb, sizeof(cb)); // 오프셋 0만 사용
	onnxGPUResource->CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

	// 5) 바인딩: t0=ModelOut SRV, u0=OnnxTex UAV
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->OnnxTexUAV_GPU);

	// 6) 디스패치
	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((dstW + TGX - 1) / TGX, (dstH + TGY - 1) / TGY, 1);

	// 7) 상태 원복
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

void OnnxService_FastNeuralStyle::CreateOnnxResources_FastNeuralStyle(UINT W, UINT H,
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

	// 1) ONNX IO 준비: 콘텐츠만 (W,H)
	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H);

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

	// 4) 디스크립터 힙 (필요 슬롯만; 여유 포함 8개 정도)
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 8; // Scene SRV, InC UAV, OnnxTex UAV/SRV, ModelOut SRV, InputC SRV(debug) 등
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&onnxGPUResource->Heap));

	}

	auto gpuStart = onnxGPUResource->Heap->GetGPUDescriptorHandleForHeapStart();
	auto cpuGPU = onnxGPUResource->Heap->GetCPUDescriptorHandleForHeapStart();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto nthGPU = [&](UINT i) { auto h = gpuStart; h.ptr += i * inc; return h; };
	auto nthCPU_GPU = [&](UINT i) { auto h = cpuGPU;  h.ptr += i * inc; return h; };

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

	// (2) OnnxTex UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		onnxGPUResource->OnnxTexUAV_CPU = nthCPU_GPU(2);
		onnxGPUResource->OnnxTexUAV_GPU = nthGPU(2);
		dev->CreateUnorderedAccessView(onnxGPUResource->OnnxTex.Get(), nullptr, &u, onnxGPUResource->OnnxTexUAV_CPU);
	}

	// (3) OnnxTex SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->OnnxTexSRV_CPU = nthCPU_GPU(3);
		onnxGPUResource->OnnxTexSRV_GPU = nthGPU(3);
		dev->CreateShaderResourceView(onnxGPUResource->OnnxTex.Get(), &s, onnxGPUResource->OnnxTexSRV_CPU);
	}

	// (4) ModelOut SRV (후처리에서 NumElements 갱신)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = 1; // 후처리에서 갱신
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->ModelOutSRV_CPU = nthCPU_GPU(4);
		onnxGPUResource->ModelOutSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	// (5) InputContent SRV (debug)
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

		onnxGPUResource->InputContentSRV_CPU = nthCPU_GPU(5);
		onnxGPUResource->InputContentSRV_GPU = nthGPU(5);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferContent().Get(), &s, onnxGPUResource->InputContentSRV_CPU);
	}

	// 6) CB (전/후처리 공용, 1슬라이스만 사용)
	{
		const UINT kCBAligned = ((sizeof(UINT) * 8 + 255) & ~255);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(kCBAligned); // *1
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&onnxGPUResource->CB));
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	onnxResource->Width = W; onnxResource->Height = H;
}