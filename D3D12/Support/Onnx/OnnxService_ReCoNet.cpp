#include "OnnxService_ReCoNet.h"
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



void OnnxService_ReCoNet::RecordPreprocess_ReCoNet(
	ID3D12GraphicsCommandList7* cmd, 
	ID3D12DescriptorHeap* heap, 
	OnnxPassResources* onnxResource,
	OnnxGPUResources* onnxGPUResource,
	ID3D12Resource2* sceneColor,
	Image& styleImage
)
{
	if (!heap || !onnxResource || !sceneColor || !onnxGPUResource) return;

	ID3D12DescriptorHeap* heaps[] = { heap };
	cmd->SetDescriptorHeaps(1, heaps);

	// ��ó�� RS/PSO
	cmd->SetComputeRootSignature(onnxResource->PreRS.Get());
	cmd->SetPipelineState(onnxResource->PrePSO.Get());

	// �Է� shape [1,3,H,W]
	const auto& ish = DX_ONNX.GetInputShapeContent();
	if (ish.size() != 4) return;
	const UINT inC = (UINT)ish[1];
	const UINT inH = (UINT)ish[2];
	const UINT inW = (UINT)ish[3];

	// �÷���: bit0=FlipV, bit4=BGR swap
	UINT flags = 0;
	const auto fmt = sceneColor->GetDesc().Format;
	const bool isBGRA = (fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
	if (isBGRA) flags |= PRE_BGR_SWAP;

	// �ʿ�� ������(��ǥ ü�迡 ����) �� �⺻�� off
	// flags |= 0x1;

	struct CB { UINT W, H, C, Flags; } cb{ inW, inH, inC, flags };
	{
		// 256B ���� CB �� �����̽��� ���
		uint8_t* base = nullptr;
		onnxGPUResource->CB->Map(0, nullptr, (void**)&base);
		std::memcpy(base + 0, &cb, sizeof(cb));
		onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress() + 0);
	}

	// t0: Scene SRV (�� SRGB ���� SRV�� ���� �� �ڵ� ����ȭ)
	WriteSceneSRVToSlot0(sceneColor, onnxGPUResource);
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->SceneSRV_GPU);

	// u0: InputContent UAV (NCHW float32)
	// ����: UAV ����
	{
		auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
		cmd->ResourceBarrier(1, &uav);
	}
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->InputContentUAV_GPU);

	// Dispatch
	if (inW && inH) {
		const UINT TG = 8;
		cmd->Dispatch((inW + TG - 1) / TG, (inH + TG - 1) / TG, 1);
		// UAV complete
		auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBufferContent().Get());
		cmd->ResourceBarrier(1, &uav);
	}
}

void OnnxService_ReCoNet::RecordPostprocess_ReCoNet(
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

	// ��� shape [1,3,H,W]
	const auto& osh = DX_ONNX.GetOutputShape();
	if (osh.size() != 4) return;
	const UINT srcC = (UINT)osh[1];
	const UINT srcH = (UINT)osh[2];
	const UINT srcW = (UINT)osh[3];

	// Output(NCHW) �� SRV�� �б�
	static D3D12_RESOURCE_STATES sOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (sOutputState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(),
			sOutputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		sOutputState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}

	// OnnxTex �� UAV�� ����
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			onnxGPUResource->OnnxTex.Get(),
			mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// ModelOut SRV ���� (����ȭ ����: float)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = srcW * srcH * srcC;     // NCHW ��� ��ü
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		DX_CONTEXT.GetDevice()->CreateShaderResourceView(
			DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	// CB ������Ʈ
	const UINT dstW = onnxResource->Width;
	const UINT dstH = onnxResource->Height;
	struct CBData {
		UINT SrcW, SrcH, SrcC, Flags; // Flags ����(�̻��=0)
		UINT DstW, DstH, _r1, _r2;
		float Gain, Bias, _f0, _f1;   // �� ���� �ɼ�
	} cb{ srcW, srcH, srcC, 0, dstW, dstH, 0, 0, 1.0f, 0.0f, 0, 0 };

	{
		void* p = nullptr;
		onnxGPUResource->CB->Map(0, nullptr, &p);
		std::memcpy(p, &cb, sizeof(cb));
		onnxGPUResource->CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, onnxGPUResource->CB->GetGPUVirtualAddress());
	}

	// t0=ModelOut(NCHW SRV), u0=OnnxTex(UAV)
	cmd->SetComputeRootDescriptorTable(0, onnxGPUResource->ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, onnxGPUResource->OnnxTexUAV_GPU);

	// Dispatch
	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((dstW + TGX - 1) / TGX, (dstH + TGY - 1) / TGY, 1);

	// ���� ����
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

void OnnxService_ReCoNet::CreateOnnxResources_ReCoNet(UINT W, UINT H,
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
	if (!onnxResource || !onnxGPUResource || !sceneColor) return;

	// 0) �ػ󵵴� 8�� ����� ����(��Ʈ��ũ �ٿ�/������ ����)
	const UINT netW = (W / 8) * 8;
	const UINT netH = (H / 8) * 8;

	// 1) ONNX IO �غ� (content��)
	DX_ONNX.PrepareIO(dev, netW, netH);

	// 2) ��ǻƮ ����������(��/��ó��)
	if (!DX_MANAGER.CreateOnnxComputePipeline()) return;

	// 3) OnnxTex (���� RGBA8)
	{
		D3D12_RESOURCE_DESC td{};
		td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = W; td.Height = H; // ǥ�� ����� ���� ����Ʈ ũ��
		td.DepthOrArraySize = 1; td.MipLevels = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc = { 1, 0 };
		td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
		(dev->CreateCommittedResource(
			&hp, D3D12_HEAP_FLAG_NONE, &td,
			D3D12_RESOURCE_STATE_COMMON, nullptr,
			IID_PPV_ARGS(&onnxGPUResource->OnnxTex)));

		mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
	}

	// 4) ��ũ���� ��: **���� ��**�� ��� (GPU-visible; CPU �ڵ��� ���� ���� CPU-start�� ��)
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 8; // Scene SRV, InputC UAV, OnnxTex UAV/SRV, ModelOut SRV, InputC SRV(debug) ��
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		(dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&onnxGPUResource->Heap)));
	}

	auto gpuStart = onnxGPUResource->Heap->GetGPUDescriptorHandleForHeapStart();
	auto cpuStart = onnxGPUResource->Heap->GetCPUDescriptorHandleForHeapStart();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto nthGPU = [&](UINT i) { auto h = gpuStart; h.ptr += i * inc; return h; };
	auto nthCPU = [&](UINT i) { auto h = cpuStart; h.ptr += i * inc; return h; };

	// (0) Scene SRV  �����ϸ� **SRGB ����**���� ����� ����ȭ �ڵ�ȭ
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		const auto scDesc = sceneColor->GetDesc();
		s.Format = scDesc.Format; // �̹� *_SRGB�� �״��
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;

		onnxGPUResource->SceneSRV_CPU = nthCPU(0);
		onnxGPUResource->SceneSRV_GPU = nthGPU(0);
		dev->CreateShaderResourceView(sceneColor, &s, onnxGPUResource->SceneSRV_CPU);
	}

	// (1) InputContent UAV (structured float)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		const auto bytes = DX_ONNX.GetInputBufferContent()->GetDesc().Width; // byte size
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		onnxGPUResource->InputContentUAV_CPU = nthCPU(1);
		onnxGPUResource->InputContentUAV_GPU = nthGPU(1);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferContent().Get(), nullptr, &u, onnxGPUResource->InputContentUAV_CPU);
	}

	// (2) OnnxTex UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

		onnxGPUResource->OnnxTexUAV_CPU = nthCPU(2);
		onnxGPUResource->OnnxTexUAV_GPU = nthGPU(2);
		dev->CreateUnorderedAccessView(onnxGPUResource->OnnxTex.Get(), nullptr, &u, onnxGPUResource->OnnxTexUAV_CPU);
	}

	// (3) OnnxTex SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Texture2D.MipLevels = 1;

		onnxGPUResource->OnnxTexSRV_CPU = nthCPU(3);
		onnxGPUResource->OnnxTexSRV_GPU = nthGPU(3);
		dev->CreateShaderResourceView(onnxGPUResource->OnnxTex.Get(), &s, onnxGPUResource->OnnxTexSRV_CPU);
	}

	// (4) ModelOut SRV NumElements�� Post���� �� ������ ����
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		s.Buffer.NumElements = 1; // placeholder
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->ModelOutSRV_CPU = nthCPU(4);
		onnxGPUResource->ModelOutSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, onnxGPUResource->ModelOutSRV_CPU);
	}

	// (5) InputContent SRV (������)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		s.Format = DXGI_FORMAT_UNKNOWN;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.Buffer.FirstElement = 0;
		const auto bytes = DX_ONNX.GetInputBufferContent()->GetDesc().Width;
		s.Buffer.NumElements = (UINT)(bytes / sizeof(float));
		s.Buffer.StructureByteStride = sizeof(float);
		s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		onnxGPUResource->InputContentSRV_CPU = nthCPU(5);
		onnxGPUResource->InputContentSRV_GPU = nthGPU(5);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferContent().Get(), &s, onnxGPUResource->InputContentSRV_CPU);
	}

	// (6) CB (��/��ó�� ���� 256B ����)
	{
		const UINT kCBAligned = ((sizeof(UINT) * 8 + 255) & ~255);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(kCBAligned);
		(dev->CreateCommittedResource(
			&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&onnxGPUResource->CB)));
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	onnxResource->Width = W;      // ǥ�� ũ��(����Ʈ)
	onnxResource->Height = H;
}