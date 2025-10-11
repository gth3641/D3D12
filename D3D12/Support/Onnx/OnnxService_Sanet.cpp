#include "OnnxService_Sanet.h"
#include "Util/OnnxDefine.h"
#include "Manager/OnnxManager.h"
#include "Manager/DirectXManager.h"
#include "Support/Image.h"

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
	if (!heap || !onnxResource || !sceneColor || !onnxGPUResource) return;

	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PrePSO.Get());

	// 공통 플래그 조립
	UINT flags = 0;
	if (kUseBGR) flags |= PRE_BGR_SWAP;
	switch (kProfile) {
	case NormProfile::Raw01:     break;
	case NormProfile::ImageNet:  flags |= PRE_IMAGENET_MEANSTD; break;
	case NormProfile::CaffeBGR255: flags |= PRE_CAFFE_BGR_MEAN; break;
	case NormProfile::TanhIn:    flags |= PRE_TANH_INPUT; break;
	}

	auto fmt = sceneColor->GetDesc().Format;
	if (fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) flags |= PRE_BGR_SWAP; // BGR

	const bool twoInputs = true; //DX_ONNX.HasTwoInputs();

	// ===== Case A) SANet 2-input: Content/Style 각각 3채널 CHW로 두 번 디스패치 =====
	if (twoInputs)
	{
		// Pass#1: Content → InputContent
		{
			const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,3,Hc,Wc]
			const UINT inW = (UINT)ish[3], inH = (UINT)ish[2], inC = (UINT)ish[1];

			struct CB { UINT TensorW, TensorH, C, Flags; UINT _pad0, _pad1, _pad2, _pad3; } cb{ inW, inH, inC, flags, 0,0,0,0 };
			void* p = nullptr;
			onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
			cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

			// t0 = Content
			WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
			cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);

			// u0 = InputContent (한 번에 하나만 바인딩)
			cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU);

			// UAV barrier (선택)
			{
				auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
				cmd->ResourceBarrier(1, &uav);
			}
			const UINT TG = 8;
			cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);
			{
				auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
				cmd->ResourceBarrier(1, &uav);
			}
		}

		// Pass#2: Style → InputStyle
		{
			ID3D12Resource2* style = styleImage.GetTexture().Get();
			const auto& ish = DX_ONNX.GetInputShapeStyle(); // [1,3,Hs,Ws]
			const UINT inW = (UINT)ish[3], inH = (UINT)ish[2], inC = (UINT)ish[1];

			struct CB { UINT TensorW, TensorH, C, Flags; UINT _pad0, _pad1, _pad2, _pad3; } cb{ inW, inH, inC, flags, 0,0,0,0 };
			void* p = nullptr;
			onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
			cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

			// t0 = Style
			WriteSceneSRVToSlot0(style, onnxGPUResource); // 슬롯 재사용
			cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);

			// u0 = InputStyle
			cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputStyleUAV_GPU);

			{
				auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferStyle());
				cmd->ResourceBarrier(1, &uav);
			}
			const UINT TG = 8;
			cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);
			{
				auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferStyle());
				cmd->ResourceBarrier(1, &uav);
			}
		}
		return;
	}

	// ===== Case B) 병합(C=6) 모델: 기존 경로 유지 (t0=Content, t1=Style) =====
	{
		const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,6,H,W]
		const UINT inW = (UINT)ish[3], inH = (UINT)ish[2], inC = (UINT)ish[1];

		// 컨텐츠/스타일 SRV 채우기
		WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
		WritePtSRVToSlot1(styleImage.GetTexture().Get(), onnxGPUResource);

		// PtValid 플래그를 유지하려면 여기서 세팅 (기존 코드와 동일)
		// flags |= PRE_PT_VALID; // 필요시

		struct CB { UINT TensorW, TensorH, C, Flags; UINT _pad0, _pad1, _pad2, _pad3; } cb{ inW, inH, inC, flags, 0,0,0,0 };
		void* p = nullptr;
		onnxGPUResource->CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

		cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);        // SRV range (t0=content, t1=style)
		cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU); // u0

		auto* inBuf = DX_ONNX.GetInputBufferContent().Get();
		{ auto uav = CD3DX12_RESOURCE_BARRIER::UAV(inBuf); cmd->ResourceBarrier(1, &uav); }

		const UINT TG = 8;
		cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);

		{ auto uav = CD3DX12_RESOURCE_BARRIER::UAV(inBuf); cmd->ResourceBarrier(1, &uav); }
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

	// 출력 shape로 정확히 NumElements 설정
	const auto& osh = DX_ONNX.GetOutputShape(); // [1,3,H,W]
	if (osh.size() != 4) return;
	const UINT srcC = (UINT)osh[1];
	const UINT srcH = (UINT)osh[2];
	const UINT srcW = (UINT)osh[3];

	// OutputBuffer: UAV->SRV(NPSR)
	static D3D12_RESOURCE_STATES sOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (sOutputState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(),
			sOutputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		sOutputState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}

	// OnnxTex: -> UAV
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			onnxGPUResource->OnnxTex.Get(),
			mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// ModelOut SRV 갱신 (정확한 NumElements)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = srcW * srcH * srcC;   // 3*H*W
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		DX_CONTEXT.GetDevice()->CreateShaderResourceView(
			DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	UINT flags = 0;
	//if (kUseBGR) 
	//	flags |= PRE_BGR_SWAP;

	//switch (kProfile) {
	//case NormProfile::Raw01:
	//	// nothing
	//	break;
	//case NormProfile::ImageNet:
	//	flags |= PRE_IMAGENET_MEANSTD;
	//	break;
	//case NormProfile::CaffeBGR255:
	//	flags |= PRE_CAFFE_BGR_MEAN;   // 내부에서 *255 후 mean-BGR 수행
	//	break;
	//case NormProfile::TanhIn:
	//	flags |= PRE_TANH_INPUT;
	//	break;
	//}

	// CB (Src/Dst dims + /255)
	struct CBData {
		UINT SrcW, SrcH, SrcC, Flags;
		UINT DstW, DstH, _r1, _r2;
		float Gain, Bias, _f0, _f1;
	} cb{ srcW, srcH, srcC, flags,
		  onnxResource->Width, onnxResource->Height, 0,0,
		  1.0f, 0.0f, 0,0 };
	{
		void* p = nullptr;
		onnxGPUResource->CB->Map(0, nullptr, &p);
		std::memcpy(p, &cb, sizeof(cb));
		onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());
	}

	// 바인딩 & 디스패치
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->ModelOutSRV_GPU); // t0
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->OnnxTexUAV_GPU);  // u0

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((onnxResource->Width + TGX - 1) / TGX,
		(onnxResource->Height + TGY - 1) / TGY, 1);

	// OutputBuffer: 다시 UAV
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		sOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// OnnxTex: UAV → PSR (화면 블릿용)
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			onnxGPUResource->OnnxTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
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

	// 1) ONNX IO 준비 (입력 CHW는 러너에서 [1,6,H,W] 보장)
	DX_ONNX.PrepareIO(dev, W, H);

	// 2) 컴퓨트 파이프라인
	if (!DX_MANAGER.CreateOnnxComputePipeline()) return;

	// 3) 최종 출력 텍스처 (OnnxTex: RGBA8, UAV)
	{
		D3D12_RESOURCE_DESC td{};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = W; td.Height = H;
		td.DepthOrArraySize = 1; td.MipLevels = 1;
		//td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		td.SampleDesc = { 1, 0 };
		td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
			D3D12_RESOURCE_STATE_COMMON, nullptr,
			IID_PPV_ARGS(&onnxGPUResource->OnnxTex));
		mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
	}

	// 4) 디스크립터 힙 (shader-visible)
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

	// (1) Style SRV 
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = sceneColor->GetDesc().Format; // placeholder
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;

		
		onnxGPUResource->StyleSRV_CPU = nthCPU(1);
		onnxGPUResource->StyleSRV_GPU = nthGPU(1);
		// 초기에는 sceneColor로 채워두고, 외부에서 실제 style 텍스처로 갱신
		dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->StyleSRV_CPU);
	}

	// (2) InputContent UAV (u0, 전처리)
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
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferContent().Get(), nullptr, &u,
			onnxGPUResource->InputContentUAV_CPU);
	}

	// (3) OnnxTex UAV (u0, 후처리)
	{
		//if (DX_ONNX.HasTwoInputs())  // 무조건 스타일/컨텐츠 입력
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
			u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			u.Format = DXGI_FORMAT_UNKNOWN;
			auto bytes = DX_ONNX.GetInputBufferStyle()->GetDesc().Width;
			u.Buffer.FirstElement = 0;
			u.Buffer.NumElements = (UINT)(bytes / sizeof(float));
			u.Buffer.StructureByteStride = sizeof(float);
			u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

			onnxGPUResource->InputStyleUAV_CPU = nthCPU(3);
			onnxGPUResource->InputStyleUAV_GPU = nthGPU(3);
			dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferStyle(), nullptr, &u,
				onnxGPUResource->InputStyleUAV_CPU);
		}
	}

	// (4) ModelOut SRV = OutputBuffer (Buffer SRV, 후처리 t0)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN; // structured
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;

		auto* outBuf = DX_ONNX.GetOutputBuffer().Get();
		const UINT numElems = (UINT)(outBuf ? (outBuf->GetDesc().Width / sizeof(float)) : 0);
		s.Buffer.NumElements = numElems;
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->ModelOutSRV_CPU = nthCPU(4);
		onnxGPUResource->ModelOutSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(outBuf, &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	// (5) 
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  // ★ OnnxTex 포맷과 맞춤
		onnxGPUResource->OnnxTexUAV_CPU = nthCPU(5);
		onnxGPUResource->OnnxTexUAV_GPU = nthGPU(5);
		dev->CreateUnorderedAccessView(onnxGPUResource->OnnxTex.Get(), nullptr, &u,
			onnxGPUResource->OnnxTexUAV_CPU);
	}

	// (6) OnnxTex SRV (blit)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		//s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Texture2D.MipLevels = 1;

		onnxGPUResource->OnnxTexSRV_CPU = nthCPU(6);
		onnxGPUResource->OnnxTexSRV_GPU = nthGPU(6);
		dev->CreateShaderResourceView(onnxGPUResource->OnnxTex.Get(), &s, onnxGPUResource->OnnxTexSRV_CPU);
	}

	// (7) CB (전/후처리 공용)
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