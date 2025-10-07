#include "OnnxService_Udnie.h"
#include "Util/OnnxDefine.h"
#include "Manager/OnnxManager.h"
#include "Manager/DirectXManager.h"
#include "Support/Image.h"


void OnnxService_Udnie::RecordPreprocess_Udnie(
	ID3D12GraphicsCommandList7* cmd, 
	ID3D12DescriptorHeap* heap, 
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	D3D12_RESOURCE_STATES& mOnnxInputState
)
{
	// 힙/RS/PSO
	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PrePSO.Get());

	// 모델 입력 W,H,C 계산 (NCHW 또는 NHWC 가정)
	UINT inW = 1, inH = 1, inC = 3;
	{
		const auto& ish = DX_ONNX.GetInputShapeContent();
		if (ish.size() == 4) {
			// NCHW: [N,C,H,W] 또는 NHWC: [N,H,W,C]
			int64_t d0 = ish[0], d1 = ish[1], d2 = ish[2], d3 = ish[3];
			if (d1 <= 4) { inC = (UINT)d1; inH = (UINT)d2; inW = (UINT)d3; } // NCHW
			else { inH = (UINT)d1; inW = (UINT)d2; inC = (UINT)d3; }         // NHWC
		}
		else if (ish.size() == 3) {
			// CHW / HWC
			int64_t d0 = ish[0], d1 = ish[1], d2 = ish[2];
			if (d0 <= 4) { inC = (UINT)d0; inH = (UINT)d1; inW = (UINT)d2; } // CHW
			else { inH = (UINT)d0; inW = (UINT)d1; inC = (UINT)d2; }         // HWC
		}
		if (inC == 0) inC = 3;
	}

	UINT dstW = onnxResource->Width;                 // 창 폭(= OnnxTex 폭)
	UINT dstH = onnxResource->Height;                // 창 높이(= OnnxTex 높이)

	// CB 업데이트 (W=네트워크 입력 너비, H=높이, C=채널)
	struct CB
	{
		UINT SrcW, SrcH, SrcC, DstW, DstH, _pad[3];
	} cb{ inW, inH, inC, dstW, dstH };
	void* p = nullptr;
	onnxGPUResource->CB->Map(0, nullptr, &p);
	memcpy(p, &cb, sizeof(cb));
	onnxGPUResource->CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

	// 루트 바인딩: t0=Scene SRV, u0=InputNCHW UAV
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU);

	// 디스패치: 모델 입력 해상도 기준
	const UINT TGX = 8, TGY = 8;
	// 0) InputBuffer: (현재 상태) -> UAV (전처리에서 쓰기)
	if (mOnnxInputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetInputBufferContent().Get(),
			mOnnxInputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &toUav);
		mOnnxInputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// (기존 코드 그대로: 힙/RS/PSO 설정, CB 쓰기, 디스패치)
	cmd->Dispatch((inW + TGX - 1) / TGX, (inH + TGY - 1) / TGY, 1);

	// 1) UAV 쓰기 가시화
	auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
	cmd->ResourceBarrier(1, &uav);

	// 2) InputBuffer: UAV -> GENERIC_READ (★ ORT가 바로 읽게)
	{
		auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetInputBufferContent().Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		cmd->ResourceBarrier(1, &toRead);
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

}

void OnnxService_Udnie::RecordPostprocess_Udnie(
	ID3D12GraphicsCommandList7* cmd, 
	ID3D12DescriptorHeap* heap, 
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	D3D12_RESOURCE_STATES& mOnnxTexState)
{
	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PostPSO.Get());

	// Src = 모델 출력(224x224x3), Dst = 화면 크기
	const UINT srcW = 224, srcH = 224, srcC = 3;
	const UINT dstW = onnxResource->Width;      // CreateOnnxResources(W,H)에서 받은 화면 크기
	const UINT dstH = onnxResource->Height;

	struct CBData {
		UINT SrcW, SrcH, SrcC, _r0;
		UINT DstW, DstH, _r1, _r2;
	} CB{ srcW, srcH, srcC, 0, dstW, dstH, 0, 0 };

	void* p = nullptr;
	onnxGPUResource->CB->Map(0, nullptr, &p);
	std::memcpy(p, &CB, sizeof(CB));
	onnxGPUResource->CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

	// t0 = Output SRV(CHW), u0 = OnnxTex UAV
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->OnnxTexUAV_GPU);

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((dstW + TGX - 1) / TGX, (dstH + TGY - 1) / TGY, 1);


	// 3) Output: NPSR -> UAV (다음 프레임 DML이 다시 쓰게 복귀)
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
	}

	// 4) OnnxTex: UAV -> PSR (블릿에서 SRV로 샘플)
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			onnxGPUResource->OnnxTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
}

void OnnxService_Udnie::CreateOnnxResources_Udnie(
	UINT W, UINT H, 
	Image& styleImage, 
	OnnxPassResources* onnxResource, 
	OnnxGPUResources* onnxGPUResource, 
	ID3D12DescriptorHeap* heapCPU, 
	ID3D12Resource2* sceneColor, 
	D3D12_RESOURCE_STATES& mOnnxTexState, 
	D3D12_RESOURCE_STATES& mOnnxInputState)
{
	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H);
	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	if (!DX_MANAGER.CreateOnnxComputePipeline()) return;

	// === OnnxTex (최종 RGBA8) ===
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

	// === 디스크립터 힙: Scene SRV, Input UAV, Output SRV, OnnxTex UAV, OnnxTex SRV ===
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 5; // 0..4
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&onnxGPUResource->Heap));
	}
	auto cpu = onnxGPUResource->Heap->GetCPUDescriptorHandleForHeapStart();
	auto gpu = onnxGPUResource->Heap->GetGPUDescriptorHandleForHeapStart();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto nthCPU = [&](UINT i) { auto h = cpu; h.ptr += i * inc; return h; };
	auto nthGPU = [&](UINT i) { auto h = gpu; h.ptr += i * inc; return h; };

	// (0) SceneColor → SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = sceneColor->GetDesc().Format; // R16G16B16A16_FLOAT
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->SceneSRV_CPU = nthCPU(0);
		onnxGPUResource->SceneSRV_GPU = nthGPU(0);
		dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->SceneSRV_CPU);
	}

	// (1) InputNCHW → UAV  (DX_ONNX.InputBuffer)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		const auto& inShape = DX_ONNX.GetInputShapeContent();
		uint64_t elems = 1;
		for (auto d : inShape)
		{
			elems *= (d > 0 ? (uint64_t)d : 1);
		}
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = (UINT)elems;
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		onnxGPUResource->InputContentUAV_CPU = nthCPU(1);
		onnxGPUResource->InputContentUAV_GPU = nthGPU(1);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferContent().Get(), nullptr, &u, onnxGPUResource->InputContentUAV_CPU);
	}

	// (2) OutputCHW → SRV (DX_ONNX.OutputBuffer)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		const auto& osh = DX_ONNX.GetOutputShape();
		uint64_t elems = 1; for (auto d : osh) elems *= (d > 0 ? (uint64_t)d : 1);
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = (UINT)elems;
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->ModelOutSRV_CPU = nthCPU(2);
		onnxGPUResource->ModelOutSRV_GPU = nthGPU(2);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	// (3) OnnxTex → UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		onnxGPUResource->OnnxTexUAV_CPU = nthCPU(3);
		onnxGPUResource->OnnxTexUAV_GPU = nthGPU(3);
		dev->CreateUnorderedAccessView(onnxGPUResource->OnnxTex.Get(), nullptr, &u, onnxGPUResource->OnnxTexUAV_CPU);
	}

	// (4) OnnxTex → SRV (blit)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		onnxGPUResource->OnnxTexSRV_CPU = nthCPU(4);
		onnxGPUResource->OnnxTexSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(onnxGPUResource->OnnxTex.Get(), &s, onnxGPUResource->OnnxTexSRV_CPU);
	}

	// (5) CB (업로드)
	{
		const UINT CBSize = (UINT)((sizeof(UINT) * 4 + 255) & ~255); // W,H,C,pad
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(CBSize);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&onnxGPUResource->CB));
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	// 화면 사이즈 저장(디스패치에 사용)
	onnxResource->Width = W; onnxResource->Height = H;
}
