#include "OnnxService_BlindVideo.h"
#include "Util/OnnxDefine.h"

#include "Manager/OnnxManager.h"
#include "Manager/DirectXManager.h"

#include "Support/Image.h"
#include "Support/Shader.h"

enum class NormProfile { Raw01, ImageNet, CaffeBGR255, TanhIn };
static NormProfile kProfile = NormProfile::Raw01; 
static bool kUseBGR = false; 


// ====== 새로 추가 ======
static void CreateStructuredFloatBufferAndViews(
	ID3D12Device* dev,
	UINT tensorW, UINT tensorH, UINT channels,                 
	ComPointer<ID3D12Resource>& outBuffer,                     
	D3D12_CPU_DESCRIPTOR_HANDLE uavCPU, D3D12_GPU_DESCRIPTOR_HANDLE uavGPU,
	D3D12_CPU_DESCRIPTOR_HANDLE srvCPU, D3D12_GPU_DESCRIPTOR_HANDLE srvGPU)
{
	const UINT64 numFloats = (UINT64)tensorW * tensorH * channels;
	const UINT64 bytes = numFloats * sizeof(float);

	// 1) 리소스 (DEFAULT, UAV 가능)
	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	auto rd = CD3DX12_RESOURCE_DESC::Buffer(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	ComPointer<ID3D12Resource> buf;
	(dev->CreateCommittedResource(
		&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
		IID_PPV_ARGS(&buf)));

	// 2) UAV(Structured<float>)
	D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
	u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	u.Format = DXGI_FORMAT_UNKNOWN;
	u.Buffer.FirstElement = 0;
	u.Buffer.NumElements = (UINT)numFloats;            
	u.Buffer.StructureByteStride = sizeof(float);      
	u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;       
	dev->CreateUnorderedAccessView(buf.Get(), nullptr, &u, uavCPU);

	// 3) SRV(Structured<float>)  디버그/후처리에서 읽을 때 사용
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	s.Format = DXGI_FORMAT_UNKNOWN;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.Buffer.FirstElement = 0;
	s.Buffer.NumElements = (UINT)numFloats;             
	s.Buffer.StructureByteStride = sizeof(float);       
	s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;        
	dev->CreateShaderResourceView(buf.Get(), &s, srvCPU);

	outBuffer = std::move(buf);
}

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


void OnnxService_BlindVideo::RecordPreprocess_BlindVideo(
	ID3D12GraphicsCommandList7* cmd,
	ID3D12DescriptorHeap* heap,
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	ID3D12Resource2* sceneColor,          // It
	ID3D12Resource2* processedColor       // Pt
)
{
	if (!heap || !onnxResource || !sceneColor || !onnxGPUResource) return;

	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PrePSO.Get());

	const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,6,H,W]
	const UINT inC = (UINT)ish[1];
	const UINT inH = (UINT)ish[2];
	const UINT inW = (UINT)ish[3];

	UINT flags = 0;
	if (kUseBGR) 
		flags |= PRE_BGR_SWAP;

	switch (kProfile) {
	case NormProfile::Raw01:
		// nothing
		break;
	case NormProfile::ImageNet:
		flags |= PRE_IMAGENET_MEANSTD;
		break;
	case NormProfile::CaffeBGR255:
		flags |= PRE_CAFFE_BGR_MEAN;   // 내부에서 *255 후 mean-BGR 수행
		break;
	case NormProfile::TanhIn:
		flags |= PRE_TANH_INPUT;
		break;
	}

	auto fmt = sceneColor->GetDesc().Format;
	if (fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) flags |= 0x10; // BGR
	//if (fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) flags |= 0x1; // sRGB

	const bool ptValid = (processedColor && processedColor != sceneColor);
	if (ptValid) 
		flags |= PRE_PT_VALID; // PtValid

	PreCBData cb
	{ 
		inW, inH, inC, flags, 
		0, 0, 0, 0 
	};

	void* p = nullptr;
	onnxGPUResource->CB->Map(0, nullptr, &p);
	std::memcpy(p, &cb, sizeof(cb));
	onnxGPUResource->CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());

	// t0=It, t1=Pt
	WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
	WritePtSRVToSlot1(ptValid ? processedColor : sceneColor, onnxGPUResource);
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);        // SRV range base
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU); // u0

	// 입력 UAV 상태 보장
	{
		auto* inBuf = DX_ONNX.GetInputBufferContent().Get();
		auto uav = CD3DX12_RESOURCE_BARRIER::UAV(inBuf);
		cmd->ResourceBarrier(1, &uav);
	}

	const UINT TG = 8;
	cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);

	// UAV write 가시성
	{
		auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
		cmd->ResourceBarrier(1, &uav);
	}
}

void OnnxService_BlindVideo::RecordPostprocess_BlindVideo(
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
	if (kUseBGR) 
		flags |= PRE_BGR_SWAP;

	switch (kProfile) {
	case NormProfile::Raw01:
		// nothing
		break;
	case NormProfile::ImageNet:
		flags |= PRE_IMAGENET_MEANSTD;
		break;
	case NormProfile::CaffeBGR255:
		flags |= PRE_CAFFE_BGR_MEAN;   // 내부에서 *255 후 mean-BGR 수행
		break;
	case NormProfile::TanhIn:
		flags |= PRE_TANH_INPUT;
		break;
	}

	PostCBData cb{ srcW, srcH, srcC, flags,
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

void OnnxService_BlindVideo::CreateOnnxResources_BlindVideo(UINT W, UINT H,
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

	// (1) Pt SRV (t1)  초기엔 sceneColor로 채워두고, 매 프레임 갱신(WritePtSRVToSlot1)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = sceneColor->GetDesc().Format; // placeholder
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;

		onnxGPUResource->PtSRV_CPU = nthCPU(1);
		onnxGPUResource->PtSRV_GPU = nthGPU(1);
		dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->PtSRV_CPU);
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
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		//u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

		onnxGPUResource->OnnxTexUAV_CPU = nthCPU(3);
		onnxGPUResource->OnnxTexUAV_GPU = nthGPU(3);
		dev->CreateUnorderedAccessView(onnxGPUResource->OnnxTex.Get(), nullptr, &u,
			onnxGPUResource->OnnxTexUAV_CPU);
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

	// (5) Dummy SRV (후처리에서 SRV range=2 충족용)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = 1; // 더미 1개
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->DummySRV_CPU = nthCPU(5);
		onnxGPUResource->DummySRV_GPU = nthGPU(5);
		dev->CreateShaderResourceView(nullptr, &s, onnxGPUResource->DummySRV_CPU);
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