#include "DirectXManager.h"

#include "OnnxManager.h"
#include "ImageManager.h"
#include "Support/Window.h"

#include "Support/Shader.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <d3dcompiler.h>

#include "D3D/DXContext.h"
#include <DirectXMath.h>
#include <wrl.h>
#include <vector>
#include <assert.h>
#include <iostream>

static inline void GetBackbufferSize(uint32_t& w, uint32_t& h) {
	ID3D12Resource* back = DX_WINDOW.GetBackbuffer(); // 이미 레포에 있는 함수
	const auto d = back->GetDesc();
	w = static_cast<uint32_t>(d.Width);
	h = d.Height;
}

D3D12_HEAP_PROPERTIES DirectXManager::GetHeapUploadProperties()
{
	D3D12_HEAP_PROPERTIES hpUpload{};
	hpUpload.Type = D3D12_HEAP_TYPE_UPLOAD;
	hpUpload.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	hpUpload.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	hpUpload.CreationNodeMask = 0;
	hpUpload.VisibleNodeMask = 0;

	return hpUpload;
}

D3D12_HEAP_PROPERTIES DirectXManager::GetDefaultUploadProperties()
{
	D3D12_HEAP_PROPERTIES hpDefault{};
	hpDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
	hpDefault.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	hpDefault.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	hpDefault.CreationNodeMask = 0;
	hpDefault.VisibleNodeMask = 0;

	return hpDefault;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC DirectXManager::GetPipelineState(ComPointer<ID3D12RootSignature>& rootSignature, D3D12_INPUT_ELEMENT_DESC* vertexLayout, uint32_t vertexLayoutCount, Shader& vertexShader, Shader& pixelShader)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gfxPsod{};
	gfxPsod.pRootSignature = rootSignature;
	gfxPsod.InputLayout.NumElements = vertexLayoutCount;
	gfxPsod.InputLayout.pInputElementDescs = vertexLayout;
	gfxPsod.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	gfxPsod.VS.BytecodeLength = vertexShader.GetSize();
	gfxPsod.VS.pShaderBytecode = vertexShader.GetBuffer();
	gfxPsod.PS.BytecodeLength = pixelShader.GetSize();
	gfxPsod.PS.pShaderBytecode = pixelShader.GetBuffer();
	gfxPsod.DS.BytecodeLength = 0;
	gfxPsod.DS.pShaderBytecode = nullptr;
	gfxPsod.HS.BytecodeLength = 0;
	gfxPsod.HS.pShaderBytecode = nullptr;
	gfxPsod.GS.BytecodeLength = 0;
	gfxPsod.GS.pShaderBytecode = nullptr;
	gfxPsod.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gfxPsod.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	gfxPsod.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	gfxPsod.RasterizerState.FrontCounterClockwise = FALSE;
	gfxPsod.RasterizerState.DepthBias = 0;
	gfxPsod.RasterizerState.DepthBiasClamp = 0.0f;
	gfxPsod.RasterizerState.SlopeScaledDepthBias = 0.f;
	gfxPsod.RasterizerState.DepthClipEnable = FALSE;
	gfxPsod.RasterizerState.MultisampleEnable = FALSE;
	gfxPsod.RasterizerState.AntialiasedLineEnable = FALSE;
	gfxPsod.RasterizerState.ForcedSampleCount = 0;

	gfxPsod.StreamOutput.NumEntries = 0;
	gfxPsod.StreamOutput.NumStrides = 0;
	gfxPsod.StreamOutput.pBufferStrides = nullptr;
	gfxPsod.StreamOutput.pSODeclaration = nullptr;
	gfxPsod.StreamOutput.RasterizedStream = 0;
	gfxPsod.NumRenderTargets = 1;
	gfxPsod.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	//gfxPsod.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gfxPsod.DSVFormat = DXGI_FORMAT_UNKNOWN;
	gfxPsod.BlendState.AlphaToCoverageEnable = FALSE;
	gfxPsod.BlendState.IndependentBlendEnable = FALSE;
	gfxPsod.BlendState.RenderTarget[0].BlendEnable = TRUE;
	gfxPsod.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	gfxPsod.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
	gfxPsod.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	gfxPsod.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	gfxPsod.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	gfxPsod.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	gfxPsod.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
	gfxPsod.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
	gfxPsod.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	gfxPsod.DepthStencilState.DepthEnable = FALSE;
	gfxPsod.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	gfxPsod.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	gfxPsod.DepthStencilState.StencilEnable = FALSE;
	gfxPsod.DepthStencilState.StencilReadMask = 0;
	gfxPsod.DepthStencilState.StencilWriteMask = 0;
	gfxPsod.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	gfxPsod.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	gfxPsod.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	gfxPsod.SampleMask = 0xFFFFFFFF;
	gfxPsod.SampleDesc.Count = 1;
	gfxPsod.SampleDesc.Quality = 0;
	gfxPsod.NodeMask = 0;
	gfxPsod.CachedPSO.CachedBlobSizeInBytes = 0;
	gfxPsod.CachedPSO.pCachedBlob = nullptr;
	gfxPsod.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	return gfxPsod;
}

D3D12_RESOURCE_DESC DirectXManager::GetVertexResourceDesc()
{
	D3D12_RESOURCE_DESC rdv{};
	rdv.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rdv.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	rdv.Width = 1024;
	rdv.Height = 1;
	rdv.DepthOrArraySize = 1;
	rdv.MipLevels = 1;
	rdv.Format = DXGI_FORMAT_UNKNOWN;
	rdv.SampleDesc.Count = 1;
	rdv.SampleDesc.Quality = 0;
	rdv.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rdv.Flags = D3D12_RESOURCE_FLAG_NONE;

	return rdv;
}

D3D12_RESOURCE_DESC DirectXManager::GetUploadResourceDesc(uint32_t textureSize)
{
	D3D12_RESOURCE_DESC rdu{};

	rdu.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rdu.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	rdu.Width = textureSize + 1024;
	rdu.Height = 1;
	rdu.DepthOrArraySize = 1;
	rdu.MipLevels = 1;
	rdu.Format = DXGI_FORMAT_UNKNOWN;
	rdu.SampleDesc.Count = 1;
	rdu.SampleDesc.Quality = 0;
	rdu.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rdu.Flags = D3D12_RESOURCE_FLAG_NONE;

	return rdu;
}

D3D12_RESOURCE_DESC DirectXManager::GetTextureResourceDesc(const ImageData& textureData)
{
	D3D12_RESOURCE_DESC rdt{};
	rdt.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rdt.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	rdt.Width = textureData.width;
	rdt.Height = textureData.height;
	rdt.DepthOrArraySize = 1;
	rdt.MipLevels = 1;
	rdt.Format = textureData.giPixelFormat;
	rdt.SampleDesc.Count = 1;
	rdt.SampleDesc.Quality = 0;
	rdt.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rdt.Flags = D3D12_RESOURCE_FLAG_NONE;
	return rdt;
}

D3D12_TEXTURE_COPY_LOCATION DirectXManager::GetTextureSource(ComPointer<ID3D12Resource2>& uploadBuffer, ImageData& textureData, uint32_t textureStride)
{
	D3D12_TEXTURE_COPY_LOCATION txtcSrc;
	txtcSrc.pResource = uploadBuffer;
	txtcSrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	txtcSrc.PlacedFootprint.Offset = 0;
	txtcSrc.PlacedFootprint.Footprint.Width = textureData.width;
	txtcSrc.PlacedFootprint.Footprint.Height = textureData.height;
	txtcSrc.PlacedFootprint.Footprint.Depth = 1;
	txtcSrc.PlacedFootprint.Footprint.RowPitch = textureStride;
	txtcSrc.PlacedFootprint.Footprint.Format = textureData.giPixelFormat;

	return txtcSrc;
}

D3D12_TEXTURE_COPY_LOCATION DirectXManager::GetTextureDestination(ComPointer<ID3D12Resource2>& texture)
{
	D3D12_TEXTURE_COPY_LOCATION txtcDst;
	txtcDst.pResource = texture;
	txtcDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	txtcDst.SubresourceIndex = 0;

	return txtcDst;
}

D3D12_BOX DirectXManager::GetTextureSizeAsBox(const ImageData& textureData)
{
	D3D12_BOX textureSizeAsBox;
	textureSizeAsBox.left = textureSizeAsBox.top = textureSizeAsBox.front = 0;
	textureSizeAsBox.right = textureData.width;
	textureSizeAsBox.bottom = textureData.height;
	textureSizeAsBox.back = 1;

	return textureSizeAsBox;
}

D3D12_VERTEX_BUFFER_VIEW DirectXManager::GetVertexBufferView(ComPointer<ID3D12Resource2>& vertexBuffer, uint32_t vertexCount, uint32_t vertexSize)
{
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vbv.SizeInBytes = vertexSize * vertexCount;
	vbv.StrideInBytes = vertexSize;

	return vbv;
}


bool DirectXManager::Init()
{
	SetVerticies();
	SetVertexLayout();
	CreateDescriptorHipForTexture();
	InitUploadRenderingObject();

	UINT w, h;
	DX_WINDOW.GetBackbufferSize(w, h);
	CreateOffscreen(w, h);

	auto device = DX_CONTEXT.GetDevice();
	mSrvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mRtvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	InitShader();
	InitBlitPipeline();

	// ★ ONNX 초기화 (네 모델 경로 맞춰서)
	DX_ONNX.Init(L"./Resources/Onnx/udnie-9.onnx", DX_CONTEXT.GetDevice(), DX_CONTEXT.GetCommandQueue());

	// ★ ONNX IO 준비 + 디스크립터 구성
	CreateOnnxResources(w, h);

	// 리소스 상태 초기화
	mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	mResolvedState = D3D12_RESOURCE_STATE_COMMON;

	vbv1 = DirectXManager::GetVertexBufferView(
		DX_MANAGER.GetRenderingObject1().GetVertexBuffer(),
		DX_MANAGER.GetRenderingObject1().GetVertexCount(), sizeof(Vertex));
	vbv2 = DirectXManager::GetVertexBufferView(
		DX_MANAGER.GetRenderingObject2().GetVertexBuffer(),
		DX_MANAGER.GetRenderingObject2().GetVertexCount(), sizeof(Vertex));

	return true;
}


void DirectXManager::Shutdown()
{
	DestroyOffscreen();
	mOnnx.reset();

	m_RootSignature.Release();
	m_PipelineStateObj.Release();
	//m_Srvheap.Release();
}

void DirectXManager::UploadGPUResource(ID3D12GraphicsCommandList7* cmdList)
{
	if (cmdList == nullptr)
	{
		return;
	}

	RenderingObject1.UploadGPUResource(cmdList);
	RenderingObject2.UploadGPUResource(cmdList);
}


void DirectXManager::CreateOnnxResources(UINT W, UINT H)
{
	// ★ ONNX의 in/out 버퍼 생성(모델 shape 반영). 동적 입력이면 W,H 로 맞춰짐.
	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H);

	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	// ★ 컴퓨트 파이프라인 생성(Pre/Post 공용 RS)
	if (!CreateOnnxComputePipeline()) return;

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
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mOnnxGPU.OnnxTex));
	}

	// === 디스크립터 힙: Scene SRV, Input UAV, Output SRV, OnnxTex UAV, OnnxTex SRV ===
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 5; // 0..4
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&mOnnxGPU.Heap));
	}
	auto cpu = mOnnxGPU.Heap->GetCPUDescriptorHandleForHeapStart();
	auto gpu = mOnnxGPU.Heap->GetGPUDescriptorHandleForHeapStart();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto nthCPU = [&](UINT i) { auto h = cpu; h.ptr += i * inc; return h; };
	auto nthGPU = [&](UINT i) { auto h = gpu; h.ptr += i * inc; return h; };

	// (0) SceneColor → SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = mSceneColor->GetDesc().Format; // R16G16B16A16_FLOAT
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		mOnnxGPU.SceneSRV_CPU = nthCPU(0);
		mOnnxGPU.SceneSRV_GPU = nthGPU(0);
		dev->CreateShaderResourceView(mSceneColor.Get(), &s, mOnnxGPU.SceneSRV_CPU);
	}

	// (1) InputNCHW → UAV  (DX_ONNX.InputBuffer)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		u.Format = DXGI_FORMAT_UNKNOWN;
		const auto& inShape = DX_ONNX.GetInputShape();
		uint64_t elems = 1; for (auto d : inShape) elems *= (d > 0 ? (uint64_t)d : 1);
		u.Buffer.FirstElement = 0;
		u.Buffer.NumElements = (UINT)elems;
		u.Buffer.StructureByteStride = sizeof(float);
		u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		mOnnxGPU.InputUAV_CPU = nthCPU(1);
		mOnnxGPU.InputUAV_GPU = nthGPU(1);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBuffer().Get(), nullptr, &u, mOnnxGPU.InputUAV_CPU);
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

		mOnnxGPU.ModelOutSRV_CPU = nthCPU(2);
		mOnnxGPU.ModelOutSRV_GPU = nthGPU(2);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, mOnnxGPU.ModelOutSRV_CPU);
	}

	// (3) OnnxTex → UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		mOnnxGPU.OnnxTexUAV_CPU = nthCPU(3);
		mOnnxGPU.OnnxTexUAV_GPU = nthGPU(3);
		dev->CreateUnorderedAccessView(mOnnxGPU.OnnxTex.Get(), nullptr, &u, mOnnxGPU.OnnxTexUAV_CPU);
	}

	// (4) OnnxTex → SRV (blit)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		mOnnxGPU.OnnxTexSRV_CPU = nthCPU(4);
		mOnnxGPU.OnnxTexSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(mOnnxGPU.OnnxTex.Get(), &s, mOnnxGPU.OnnxTexSRV_CPU);
	}

	// (5) CB (업로드)
	{
		const UINT CBSize = (UINT)((sizeof(UINT) * 4 + 255) & ~255); // W,H,C,pad
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(CBSize);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mOnnxGPU.CB));
	}

	// 화면 사이즈 저장(디스패치에 사용)
	onnx_.Width = W; onnx_.Height = H;
}

void DirectXManager::RecordPreprocess(ID3D12GraphicsCommandList7* cmd)
{
	// SceneColor: RenderOffscreen 끝에서 NON_PIXEL_SHADER_RESOURCE 상태여야 함

   // 힙/RS/PSO
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(onnx_.PrePSO.Get());

	// 모델 입력 W,H,C 계산 (NCHW 또는 NHWC 가정)
	UINT inW = 1, inH = 1, inC = 3;
	{
		const auto& ish = DX_ONNX.GetInputShape();
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

	// CB 업데이트 (W=네트워크 입력 너비, H=높이, C=채널)
	struct { UINT W, H, C, _pad; } CB{ inW, inH, inC, 0 };
	void* p = nullptr;
	mOnnxGPU.CB->Map(0, nullptr, &p);
	std::memcpy(p, &CB, sizeof(CB));
	mOnnxGPU.CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress());

	// 루트 바인딩: t0=Scene SRV, u0=InputNCHW UAV
	cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.SceneSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.InputUAV_GPU);

	// 디스패치: 모델 입력 해상도 기준
	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((inW + TGX - 1) / TGX, (inH + TGY - 1) / TGY, 1);

	// UAV 쓰기 가시화
	auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBuffer().Get());
	cmd->ResourceBarrier(1, &uav);
}

void DirectXManager::RecordPostprocess(ID3D12GraphicsCommandList7* cmd)
{
	// ★★★ ONNX 출력 버퍼: UAV(ORT가 방금 씀) → SRV 읽기용 상태로 바꾸기
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetOutputBuffer().Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
	}

	// OnnxTex: COMMON → UAV
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mOnnxGPU.OnnxTex.Get(), D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
	}

	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(onnx_.PostPSO.Get());

	// 출력 텐서 크기 from ONNX
	UINT outW = 1, outH = 1, outC = 3;
	{
		const auto& osh = DX_ONNX.GetOutputShape();
		if (osh.size() == 4) {
			// [N,C,H,W] or [N,H,W,C]
			if (osh[1] <= 4) { outC = (UINT)osh[1]; outH = (UINT)osh[2]; outW = (UINT)osh[3]; }
			else { outH = (UINT)osh[1]; outW = (UINT)osh[2]; outC = (UINT)osh[3]; }
		}
		else if (osh.size() == 3) {
			if (osh[0] <= 4) { outC = (UINT)osh[0]; outH = (UINT)osh[1]; outW = (UINT)osh[2]; }
			else { outH = (UINT)osh[0]; outW = (UINT)osh[1]; outC = (UINT)osh[2]; }
		}
		if (!outC) outC = 3;
	}

	struct { UINT W, H, C, _pad; } CB{ outW, outH, outC, 0 };
	void* p = nullptr;
	mOnnxGPU.CB->Map(0, nullptr, &p);
	std::memcpy(p, &CB, sizeof(CB));
	mOnnxGPU.CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress());

	// t0=ONNX Output SRV, u0=OnnxTex UAV
	cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.OnnxTexUAV_GPU);

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((onnx_.Width + TGX - 1) / TGX, (onnx_.Height + TGY - 1) / TGY, 1);

	// OnnxTex: UAV → PIXEL_SHADER_RESOURCE
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mOnnxGPU.OnnxTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
	}
}

void DirectXManager::RunOnnxGPU()
{
	// 필요시: 전처리에서 UAV로 쓴 InputNCHW가 COMMON이든 그대로든,
	// ONNX(DML)에서 읽을 수 있게(대부분 COMMON/GENERIC_READ면 OK)
	// 여기선 DML이 스스로 필요한 배리어를 잡는다고 가정.

	// (1) 모델 입출력 바인딩
	//   - 입력: mOnnxGPU.InputNCHW (float, 3*W*H)
	//   - 출력: mOnnxGPU.OutputNCHW (float, C*W*H)
//		const UINT C = 3; // 모델 출력 채널 수
	//DX_ONNX.BindIO(
	//	mOnnxGPU.InputNCHW.Get(), 3 * onnx_.Width * onnx_.Height,
	//	mOnnxGPU.OutputNCHW.Get(), C * onnx_.Width * onnx_.Height);

	// (2) 실행
	DX_ONNX.Run();  // 내부에서 CommandQueue/Sync 처리
}

void DirectXManager::ResizeOnnxResources(UINT W, UINT H)
{
	if (W == onnx_.Width && H == onnx_.Height) return;

	// (1) 기존 Onnx GPU 리소스 해제
	mOnnxGPU.InputNCHW.Release();
	mOnnxGPU.OnnxTex.Release();
	mOnnxGPU.CB.Release();
	mOnnxGPU.Heap.Release();
	mOnnxGPU.SceneSRV_CPU = {};
	mOnnxGPU.SceneSRV_GPU = {};
	mOnnxGPU.InputUAV_CPU = {};
	mOnnxGPU.InputUAV_GPU = {};
	mOnnxGPU.InputSRV_CPU = {};
	mOnnxGPU.InputSRV_GPU = {};
	mOnnxGPU.OnnxTexUAV_CPU = {};
	mOnnxGPU.OnnxTexUAV_GPU = {};
	mOnnxGPU.OnnxTexSRV_CPU = {};
	mOnnxGPU.OnnxTexSRV_GPU = {};

	// (2) 새 크기로 재생성 (SceneColor가 이미 새로 만들어진 뒤여야 함)
	CreateOnnxResources(W, H);

	// (3) DirectML 경로도 IO 갱신한다면
	DX_ONNX.ResizeIO(DX_CONTEXT.GetDevice(), W, H);
}

void DirectXManager::RecordOnnxPass(ID3D12GraphicsCommandList* cmd)
{
	const UINT W = onnx_.Width, H = onnx_.Height;

	// === 힙 바인딩: 컴퓨트 전용 ===
	ID3D12DescriptorHeap* heaps[] = { mCSHeap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);

	// (A) 전처리: Scene SRV -> Input UAV
	cmd->SetComputeRootSignature(onnx_PreRS.Get());
	cmd->SetPipelineState(onnx_PrePSO.Get());

	// t0=Scene SRV, u0=Input UAV, consts=(SrcW,SrcH, DstW, DstH, Channels=3, ...)
	cmd->SetComputeRootDescriptorTable(0, mCS_GPU[kSlot_SceneSRV]);
	cmd->SetComputeRootDescriptorTable(1, mCS_GPU[kSlot_InputUAV]);
	struct PreConsts { UINT SrcW, SrcH, DstW, DstH, Channels, _pad[3]; } pre{ mWidth, mHeight, W, H, 3 };
	cmd->SetComputeRoot32BitConstants(2, sizeof(PreConsts) / 4, &pre, 0);

	// SceneColor는 이미 NON_PIXEL_SHADER_RESOURCE 상태여야 함(RenderOffscreen에서 전환)
	cmd->Dispatch((W + 7) / 8, (H + 7) / 8, 1);

	// UAV 쓰기 완료 보장
	CD3DX12_RESOURCE_BARRIER uav = CD3DX12_RESOURCE_BARRIER::UAV(mOnnxInputBuf.Get());
	cmd->ResourceBarrier(1, &uav);

	// (B) DirectML/ONNX 실행 (GPU IO 바인딩 전제) // ★ 중요
	// DX_ONNX.Run();  // 내부에서 mOnnxInputBuf 읽어 mOnnxOutputBuf에 씀
	// 만약 바인딩 API가 필요하다면: DX_ONNX.BindIO(mOnnxInputBuf.Get(), 3*W*H, mOnnxOutputBuf.Get(), 4*W*H); DX_ONNX.Run();

	// 모델이 OutputBuf에 쓴 것 보장
	CD3DX12_RESOURCE_BARRIER rb = CD3DX12_RESOURCE_BARRIER::UAV(mOnnxOutputBuf.Get());
	cmd->ResourceBarrier(1, &rb);

	// (C) 후처리: Output SRV(CHW) -> mResolved UAV(RGBA8)
	cmd->SetPipelineState(onnx_PostPSO.Get());
	cmd->SetComputeRootDescriptorTable(0, mCS_GPU[kSlot_OutputSRV]);   // t0
	cmd->SetComputeRootDescriptorTable(1, mCS_GPU[kSlot_ResolvedUAV]); // u0

	// Post 상수: SrcW/SrcH/Channels/RangeMode, DstW/DstH ...
	struct PostConsts { UINT SrcW, SrcH, Channels, RangeMode, DstW, DstH, _pad[2]; }
	post{ W, H, 3, /*0:0..1*/0, mWidth, mHeight };
	cmd->SetComputeRoot32BitConstants(2, sizeof(PostConsts) / 4, &post, 0);

	// mResolved는 현재 UAV 상태여야 함
	cmd->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8, 1);

	// (D) 그래픽스 블릿 대비: mResolved UAV -> PIXEL_SHADER_RESOURCE  // ★ 변경
	if (mResolvedState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mResolved.Get(), mResolvedState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mResolvedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
}

bool DirectXManager::CreateOnnxComputePipeline()
{
	ID3D12Device* device = DX_CONTEXT.GetDevice();

	// 루트시그
	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE1 rangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER1 params[3];
	params[0].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_ALL); // t0
	params[1].InitAsDescriptorTable(1, &rangeUAV, D3D12_SHADER_VISIBILITY_ALL); // u0
	params[2].InitAsConstantBufferView(0);                                      // b0

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.ShaderRegister = 0;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPointer<ID3DBlob> sigBlob, errBlob;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errBlob);
	if (FAILED(hr)) return false;

	hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&onnx_.PreRS));
	if (FAILED(hr)) return false;

	// 컴퓨트 셰이더 컴파일 (entry 이름 일치 확인: "main" 또는 "CSMain")
	ComPointer<ID3DBlob> csPre, csPost;

	hr = D3DCompileFromFile(L"./Shaders/cs_preprocess.hlsl", nullptr, nullptr,
		"main", "cs_5_0", 0, 0, &csPre, &errBlob);
	if (FAILED(hr) || !csPre) return false;

	hr = D3DCompileFromFile(L"./Shaders/cs_postprocess.hlsl", nullptr, nullptr,
		"main", "cs_5_0", 0, 0, &csPost, &errBlob);
	if (FAILED(hr) || !csPost) return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC preDesc{};
	preDesc.pRootSignature = onnx_.PreRS.Get();
	preDesc.CS = { csPre->GetBufferPointer(), csPre->GetBufferSize() };
	hr = device->CreateComputePipelineState(&preDesc, IID_PPV_ARGS(&onnx_.PrePSO));
	if (FAILED(hr)) return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC postDesc{};
	postDesc.pRootSignature = onnx_.PreRS.Get(); // 같은 RS를 재사용
	postDesc.CS = { csPost->GetBufferPointer(), csPost->GetBufferSize() };
	hr = device->CreateComputePipelineState(&postDesc, IID_PPV_ARGS(&onnx_.PostPSO));
	if (FAILED(hr)) return false;

	return true;
}

void DirectXManager::BeginFrame()
{
	//if (DX_WINDOW.ShouldResize()) {
	//	DestroyOffscreen();
	//	UINT w, h;
	//	DX_WINDOW.GetBackbufferSize(w, h);
	//	CreateOffscreen(w, h);
	//}
}

void DirectXManager::Resize()
{
	DestroyOffscreen();
	UINT w, h;
	DX_WINDOW.GetBackbufferSize(w, h);
	CreateOffscreen(w, h);
	CreateOnnxResources(w, h);
}

void DirectXManager::RenderOffscreen(ID3D12GraphicsCommandList7* cmd)
{
	// SceneColor: ? -> RTV
	if (mSceneColorState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mSceneColor.Get(), mSceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1, &b);
		mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	const FLOAT clear[4] = { 0,0,0,1 };
	cmd->OMSetRenderTargets(1, &mRtvScene, FALSE, nullptr);
	cmd->ClearRenderTargetView(mRtvScene, clear, 0, nullptr);

	// === PSO/RS/IA ===
	cmd->SetPipelineState(DX_MANAGER.GetPipelineStateObj());
	cmd->SetGraphicsRootSignature(DX_MANAGER.GetRootSignature());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VIEWPORT vp = DX_WINDOW.CreateViewport();
	RECT scRect = DX_WINDOW.CreateScissorRect();
	cmd->RSSetScissorRects(1, &scRect);
	cmd->RSSetViewports(1, &vp);

	static float bf_ff = 0.f;
	float bf[] = { bf_ff, bf_ff, bf_ff, bf_ff };
	cmd->OMSetBlendFactor(bf);

	static float color[] = { 0.f, 0.f, 0.f };
	for (int i = 0; i < 3; ++i) {
		color[i] += 5.f;
		if (color[i] > 255.f) color[i] = 0.f;
	}

	struct ScreenCB { float ViewSize[2]; };
	ScreenCB scb{ (float)DX_WINDOW.GetWidth(), (float)DX_WINDOW.GetHeight() };

	cmd->SetGraphicsRoot32BitConstants(0, 3, color, 0);
	cmd->SetGraphicsRoot32BitConstants(1, 2, &scb, 0);

	ID3D12DescriptorHeap* srvHeap = DX_IMAGE.GetSrvheap();
	cmd->SetDescriptorHeaps(1, &srvHeap);

	cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject1().GetTestIndex()));
	cmd->IASetVertexBuffers(0, 1, &vbv1);
	cmd->DrawInstanced(DX_MANAGER.GetRenderingObject1().GetVertexCount(), 1, 0, 0);

	cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject2().GetTestIndex()));
	cmd->IASetVertexBuffers(0, 1, &vbv2);
	cmd->DrawInstanced(DX_MANAGER.GetRenderingObject2().GetVertexCount(), 1, 0, 0);

	// === 다음 패스(컴퓨트 전처리)에서 SRV로 샘플 가능 상태(NPSR)로 끝내기 ===
	//auto b = CD3DX12_RESOURCE_BARRIER::Transition(
	//	mSceneColor.Get(), mSceneColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	//cmd->ResourceBarrier(1, &b);
	//mSceneColorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	// RTV -> NON_PIXEL_SHADER_RESOURCE (전처리에서 SRV로 읽도록)
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mSceneColor.Get(),
			mSceneColorState,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mSceneColorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}

}

bool DirectXManager::RunOnnx(const std::string& onnxPath)
{
	if (!mOnnx) mOnnx = std::make_unique<OnnxRunner>();
	if (mOnnxPath != onnxPath) {
		if (!mOnnx->Initialize(onnxPath, 3)) return false;
		mOnnxPath = onnxPath;
	}

	// (B) 읽을 범위를 정확히 지정
	auto desc = mSceneColor->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0, totalSize = 0;
	DX_CONTEXT.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &numRows, &rowSizeInBytes, &totalSize);
	D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalSize) }; // 읽을 바이트 범위 지정

	void* p = nullptr;
	if (FAILED(mReadback->Map(0, &readRange, &p)) || !p) return false;

	const uint8_t* src = reinterpret_cast<const uint8_t*>(p);
	std::vector<float> nchw; 
	nchw.resize((size_t)1 * 3 * mHeight * mWidth);

	auto h2f = [](uint16_t h)->float {
		uint32_t s = (h & 0x8000) << 16; 
		uint32_t e = (h & 0x7C00) >> 10; 
		uint32_t m = (h & 0x03FF); 
		uint32_t f;
		if (e == 0) 
		{ 
			if (m == 0) f = s; 
			else 
			{ 
				e = 1; 
				while ((m & 0x0400) == 0) 
				{ 
					m <<= 1; 
					--e; 
				} 
				m &= 0x03FF; 
				f = s | ((e + (127 - 15)) << 23) | (m << 13); 
			} 
		}
		else if (e == 31) 
		{ 
			f = s | 0x7F800000 | (m << 13); 
		}
		else 
		{ 
			f = s | ((e + (127 - 15)) << 23) | (m << 13); 
		}
		float out; 
		std::memcpy(&out, &f, 4);
		return out;
	};

	for (uint32_t y = 0; y < mHeight; ++y) 
	{
		const size_t srcPitch = fp.Footprint.RowPitch; // 정렬된 RowPitch 사용
		const uint16_t* row = reinterpret_cast<const uint16_t*>(src + y * srcPitch);
		for (uint32_t x = 0; x < mWidth; ++x) {

			uint16_t r16 = row[x * 4 + 0]; 
			uint16_t g16 = row[x * 4 + 1]; 
			uint16_t b16 = row[x * 4 + 2];

			float rf = h2f(r16);
			float gf = h2f(g16);
			float bf = h2f(b16);

			size_t base = (size_t)y * mWidth + x;
			size_t plane = (size_t)mWidth * mHeight;

			nchw[base + 0 * plane] = rf; 
			nchw[base + 1 * plane] = gf; 
			nchw[base + 2 * plane] = bf;
		}
	}
	mReadback->Unmap(0, nullptr);

	// ONNX 실행
	std::vector<float> out; std::vector<int64_t> oshape;
	if (!mOnnx->Run(nchw.data(), 1, 3, (int)mHeight, (int)mWidth, out, oshape)) return false;

	// 출력 shape 해석
	int oN = 1, oC = 1, oH = 1, oW = 1; bool isNCHW = true;
	if (oshape.size() == 4) {
		auto d0 = oshape[0], d1 = oshape[1], d2 = oshape[2], d3 = oshape[3];
		if (d1 <= 4 && (d1 == 1 || d1 == 3 || d1 == 4)) { oN = (int)d0; oC = (int)d1; oH = (int)d2; oW = (int)d3; isNCHW = true; }
		else if (d3 <= 4 && (d3 == 1 || d3 == 3 || d3 == 4)) { oN = (int)d0; oH = (int)d1; oW = (int)d2; oC = (int)d3; isNCHW = false; }
		else { oN = (int)d0; oC = (int)d1; oH = (int)d2; oW = (int)d3; isNCHW = true; }
	}
	else if (oshape.size() == 3) {
		auto d0 = oshape[0], d1 = oshape[1], d2 = oshape[2];
		if (d0 <= 4 && (d0 == 1 || d0 == 3 || d0 == 4)) { oC = (int)d0; oH = (int)d1; oW = (int)d2; isNCHW = true; }
		else if (d2 <= 4 && (d2 == 1 || d2 == 3 || d2 == 4)) { oH = (int)d0; oW = (int)d1; oC = (int)d2; isNCHW = false; }
		else { oC = (int)d0; oH = (int)d1; oW = (int)d2; isNCHW = true; }
	}
	else {
		return false;
	}
	size_t expected = (size_t)oN * oC * oH * oW; if (oN != 1 || expected != out.size()) return false;

	// CHW 정규화
	std::vector<float> chw((size_t)oC * oH * oW);
	if (isNCHW) {
		std::memcpy(chw.data(), out.data(), expected * sizeof(float));
	}
	else {
		for (int y = 0; y < oH; ++y) for (int x = 0; x < oW; ++x) {
			size_t base = (size_t)y * oW + x;
			for (int c = 0; c < oC; ++c) chw[(size_t)c * oH * oW + base] = out[base * oC + c];
		}
	}

	// 프레임 크기로 리사이즈
	const int dstH = (int)mHeight, dstW = (int)mWidth;
	const float* srcCHW = chw.data();
	std::vector<float> chwResized;
	if (oH != dstH || oW != dstW) {
		chwResized.resize((size_t)oC * dstH * dstW);
		OnnxRunner::BilinearResizeCHW(chw.data(), oC, oH, oW, chwResized.data(), oC, dstH, dstW);
		srcCHW = chwResized.data();
	}

	// RGBA8 패킹 → 멤버 보관
	size_t plane = (size_t)dstH * dstW;
	std::vector<uint8_t> rgba(plane * 4);

	// 1) 첫 3채널 일부 샘플로 min/max 측정
	float vmin = +1e30f, vmax = -1e30f;
	const int C = oC;
	const int sampleStep = (int)std::max<size_t>(1, plane / 4096);
	for (int c = 0; c < (3 < C ? 3 : C); ++c) {
		const float* P = srcCHW + (size_t)c * plane;
		for (size_t i = 0; i < plane; i += (size_t)sampleStep) {
			float v = P[i];
			if (v == v) { // NaN 방지
				if (v < vmin) vmin = v;
				if (v > vmax) vmax = v;
			}
		}
	}

	// 2) 범위 힌트 결정
	enum class RangeHint { ZeroOne, NegOneOne, Zero255 };
	RangeHint hint;
	if (vmax > 2.0f)             hint = RangeHint::Zero255;   // 0..255로 보임
	else if (vmin < -0.1f)       hint = RangeHint::NegOneOne; // -1..1로 보임
	else                          hint = RangeHint::ZeroOne;   // 0..1로 보임

	auto norm01 = [hint](float v)->float {
		if (!(v == v)) v = 0.f;
		switch (hint) {
		case RangeHint::Zero255:   v = v * (1.0f / 255.0f); break;
		case RangeHint::NegOneOne: v = v * 0.5f + 0.5f;     break;
		case RangeHint::ZeroOne:   /* 그대로 */            break;
		}
		if (v < 0.f) v = 0.f; if (v > 1.f) v = 1.f;
		return v;
		};
	auto to8 = [](float v)->uint8_t { return (uint8_t)(v * 255.f + 0.5f); };

	if (C >= 3) {
		const float* R = srcCHW + 0 * plane;
		const float* G = srcCHW + 1 * plane;
		const float* B = srcCHW + 2 * plane;
		for (size_t i = 0; i < plane; ++i) {
			rgba[i * 4 + 0] = to8(norm01(R[i]));
			rgba[i * 4 + 1] = to8(norm01(G[i]));
			rgba[i * 4 + 2] = to8(norm01(B[i]));
			rgba[i * 4 + 3] = 255;
		}
	}
	else if (C == 1) {
		const float* Y = srcCHW;
		for (size_t i = 0; i < plane; ++i) {
			uint8_t g = to8(norm01(Y[i]));
			rgba[i * 4 + 0] = g; rgba[i * 4 + 1] = g; rgba[i * 4 + 2] = g; rgba[i * 4 + 3] = 255;
		}
	}
	else {
		const float* R = srcCHW + 0 * plane;
		const float* G = (C > 1) ? srcCHW + 1 * plane : srcCHW;
		const float* B = (C > 2) ? srcCHW + 2 * plane : srcCHW;
		for (size_t i = 0; i < plane; ++i) {
			rgba[i * 4 + 0] = to8(norm01(R[i]));
			rgba[i * 4 + 1] = to8(norm01(G[i]));
			rgba[i * 4 + 2] = to8(norm01(B[i]));
			rgba[i * 4 + 3] = 255;
		}
	}

	// 업로드용 멤버에 보관
	mOnnxRGBA.swap(rgba);
	return true;
}

void DirectXManager::BlitToBackbuffer(ID3D12GraphicsCommandList7* cmd)
{
	UINT bw = 0, bh = 0; DX_WINDOW.GetBackbufferSize(bw, bh);
	D3D12_VIEWPORT vp{ 0,0,(float)bw,(float)bh,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)bw,(LONG)bh };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	auto rtv = DX_WINDOW.GetRtvHandle(DX_WINDOW.GetBackBufferIndex());
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	cmd->SetPipelineState(m_PsoBlitBackbuffer.Get());
	cmd->SetGraphicsRootSignature(m_RootSignature.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	struct ScreenCB { float ViewSize[2]; };
	ScreenCB scb{ (float)bw, (float)bh };
	static float color[3] = { 1,1,1 };
	cmd->SetGraphicsRoot32BitConstants(0, 3, color, 0);
	cmd->SetGraphicsRoot32BitConstants(1, 2, &scb, 0);

	// ★ 여기서 mOnnxGPU.Heap + OnnxTex SRV 바인딩
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootDescriptorTable(2, mOnnxGPU.OnnxTexSRV_GPU);

	cmd->IASetVertexBuffers(0, 1, &mFSQuadVBV);
	cmd->DrawInstanced(6, 1, 0, 0);
}

bool DirectXManager::RunOnnxCPUOnly(const std::string& onnxPath)
{
	if (!mOnnx) mOnnx = std::make_unique<OnnxRunner>();
	if (mOnnxPath != onnxPath) {
		if (!mOnnx->Initialize(onnxPath, 3)) return false;
		mOnnxPath = onnxPath;
	}

	// 이 함수 호출 전, 반드시 RenderOffscreen(cmd) → Execute → WaitGPU 가 끝나 있어야 함!
	// (그래야 mReadback에 최신 픽셀이 존재)
	D3D12_RANGE r{ 0, 0 };
	void* p = nullptr;
	if (FAILED(mReadback->Map(0, &r, &p)) || !p) return false;

	auto desc = mSceneColor->GetDesc();
	UINT64 rowPitch = 0, total = 0;
	DX_CONTEXT.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, &rowPitch, &total);

	const uint8_t* src = reinterpret_cast<const uint8_t*>(p);
	std::vector<float> nchw; nchw.resize((size_t)1 * 3 * mHeight * mWidth);

	auto h2f = [](uint16_t h)->float {
		// 간단 half->float 변환(정확 버전은 _cvtsh_ss)
		uint32_t s = (h & 0x8000) << 16;
		uint32_t e = (h & 0x7C00) >> 10;
		uint32_t m = (h & 0x03FF);
		uint32_t f;
		if (e == 0) {
			if (m == 0) f = s;
			else {
				e = 1; while ((m & 0x0400) == 0) { m <<= 1; --e; } m &= 0x03FF;
				f = s | ((e + (127 - 15)) << 23) | (m << 13);
			}
		}
		else if (e == 31) {
			f = s | 0x7F800000 | (m << 13);
		}
		else {
			f = s | ((e + (127 - 15)) << 23) | (m << 13);
		}
		float out; std::memcpy(&out, &f, 4); return out;
		};

	for (uint32_t y = 0; y < mHeight; ++y) {
		const uint16_t* row = reinterpret_cast<const uint16_t*>(src + y * rowPitch);
		for (uint32_t x = 0; x < mWidth; ++x) {
			uint16_t r16 = row[x * 4 + 0], g16 = row[x * 4 + 1], b16 = row[x * 4 + 2];
			float rf = h2f(r16), gf = h2f(g16), bf = h2f(b16);
			size_t base = (size_t)y * mWidth + x;
			size_t plane = (size_t)mWidth * mHeight;
			nchw[base + 0 * plane] = rf;
			nchw[base + 1 * plane] = gf;
			nchw[base + 2 * plane] = bf;
		}
	}
	mReadback->Unmap(0, nullptr);

	// ONNX 실행 (NCHW, 1x3xH x W)
	std::vector<float> out;
	std::vector<int64_t> oshape;
	if (!mOnnx->Run(nchw.data(), 1, 3, (int)mHeight, (int)mWidth, out, oshape))
		return false;

	// ---- 출력 shape 해석 (NCHW/NHWC/CHW/HWC) ----
	int oN = 1, oC = 1, oH = 1, oW = 1; bool isNCHW = true;
	if (oshape.size() == 4) {
		int64_t d0 = oshape[0], d1 = oshape[1], d2 = oshape[2], d3 = oshape[3];
		if (d1 <= 4 && (d1 == 1 || d1 == 3 || d1 == 4)) { oN = (int)d0; oC = (int)d1; oH = (int)d2; oW = (int)d3; isNCHW = true; }
		else if (d3 <= 4 && (d3 == 1 || d3 == 3 || d3 == 4)) { oN = (int)d0; oH = (int)d1; oW = (int)d2; oC = (int)d3; isNCHW = false; }
		else { oN = (int)d0; oC = (int)d1; oH = (int)d2; oW = (int)d3; isNCHW = true; }
	}
	else if (oshape.size() == 3) {
		int64_t d0 = oshape[0], d1 = oshape[1], d2 = oshape[2];
		if (d0 <= 4 && (d0 == 1 || d0 == 3 || d0 == 4)) { oC = (int)d0; oH = (int)d1; oW = (int)d2; isNCHW = true; }
		else if (d2 <= 4 && (d2 == 1 || d2 == 3 || d2 == 4)) { oH = (int)d0; oW = (int)d1; oC = (int)d2; isNCHW = false; }
		else { oC = (int)d0; oH = (int)d1; oW = (int)d2; isNCHW = true; }
	}
	else {
		return false; // 이미지 출력 아님
	}
	if (oN != 1 || (size_t)oN * oC * oH * oW != out.size()) return false;

	// CHW 정규화
	std::vector<float> chw((size_t)oC * oH * oW);
	if (isNCHW) {
		std::memcpy(chw.data(), out.data(), out.size() * sizeof(float));
	}
	else {
		for (int y = 0; y < oH; ++y) for (int x = 0; x < oW; ++x) {
			size_t base = (size_t)y * oW + x;
			for (int c = 0; c < oC; ++c) chw[(size_t)c * oH * oW + base] = out[base * oC + c];
		}
	}

	// 프레임 크기로 리사이즈
	const int dstH = (int)mHeight, dstW = (int)mWidth;
	const float* srcCHW = chw.data();
	std::vector<float> chwResized;
	if (oH != dstH || oW != dstW) {
		chwResized.resize((size_t)oC * dstH * dstW);
		OnnxRunner::BilinearResizeCHW(chw.data(), oC, oH, oW, chwResized.data(), oC, dstH, dstW);
		srcCHW = chwResized.data();
	}

	// RGBA8 패킹 → 멤버에 저장
	size_t plane = (size_t)dstH * dstW;
	mOnnxRGBA.resize(plane * 4);
	auto to8 = [](float v)->uint8_t { v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v); return (uint8_t)(v * 255.f + 0.5f); };

	if (oC >= 3) {
		const float* R = srcCHW + 0 * plane;
		const float* G = srcCHW + 1 * plane;
		const float* B = srcCHW + 2 * plane;
		for (size_t i = 0; i < plane; ++i) {
			mOnnxRGBA[i * 4 + 0] = to8(R[i]); mOnnxRGBA[i * 4 + 1] = to8(G[i]);
			mOnnxRGBA[i * 4 + 2] = to8(B[i]); mOnnxRGBA[i * 4 + 3] = 255;
		}
	}
	else if (oC == 1) {
		const float* Y = srcCHW;
		for (size_t i = 0; i < plane; ++i) {
			uint8_t g = to8(Y[i]);
			mOnnxRGBA[i * 4 + 0] = g; mOnnxRGBA[i * 4 + 1] = g; mOnnxRGBA[i * 4 + 2] = g; mOnnxRGBA[i * 4 + 3] = 255;
		}
	}
	else { // 그 외: 상위 3채널만 사용
		const float* R = srcCHW + 0 * plane;
		const float* G = srcCHW + 1 * plane;
		const float* B = (oC > 2) ? srcCHW + 2 * plane : srcCHW;
		for (size_t i = 0; i < plane; ++i) {
			mOnnxRGBA[i * 4 + 0] = to8(R[i]); mOnnxRGBA[i * 4 + 1] = to8(G[i]);
			mOnnxRGBA[i * 4 + 2] = to8(B[i]); mOnnxRGBA[i * 4 + 3] = 255;
		}
	}
	return true;
}

void DirectXManager::UploadOnnxResult(ID3D12GraphicsCommandList7* cmd)
{
	if (mOnnxRGBA.empty()) return;

	// mResolved: (COMMON 또는 PIXEL_SHADER_RESOURCE) -> COPY_DEST
	if (mResolvedState != D3D12_RESOURCE_STATE_COPY_DEST) {
		auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
			mResolved.Get(), mResolvedState, D3D12_RESOURCE_STATE_COPY_DEST);
		cmd->ResourceBarrier(1, &toCopy);
		mResolvedState = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	D3D12_SUBRESOURCE_DATA sd{};
	sd.pData = mOnnxRGBA.data();
	sd.RowPitch = (LONG_PTR)mWidth * 4;
	sd.SlicePitch = sd.RowPitch * mHeight;

	UpdateSubresources(cmd, mResolved.Get(), mUpload.Get(), 0, 0, 1, &sd);

	// COPY_DEST -> PIXEL_SHADER_RESOURCE
	auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		mResolved.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toSrv);
	mResolvedState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void DirectXManager::InitBlitPipeline()
{
	Shader vertexShader("VertexShader.cso");
	Shader pixelShader("PixelShader.cso");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = GetPipelineState(
		m_RootSignature, GetVertexLayout(), GetVertexLayoutCount(), vertexShader, pixelShader);

	// 백버퍼 포맷으로 교체
	if (auto back = DX_WINDOW.GetBackbuffer())
		pso.RTVFormats[0] = back->GetDesc().Format;

	DX_CONTEXT.GetDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_PsoBlitBackbuffer));


}

void DirectXManager::SetVerticies()
{
	Vertex vertex1[] =
	{
		{ 0.f,  0.f, 0.0f, 0.f },
		{ 0.f, 500.f, 0.f, 1.f },
		{ 500.f, 500.f, 1.f, 1.f }
	};

	Vertex vertex2[] =
	{
		{ 0.f, 0.f, 0.f, 0.f },
		{ 500.f, 0.f, 1.f, 0.f },
		{ 500.f, 500.f, 1.f, 1.f }
	};

	RenderingObject1.AddTriangle(vertex1, 3);
	RenderingObject1.AddTriangle(vertex2, 3);

	Vertex vertex3[] =
	{
		{ 500.f,  0.f, 1.f, 1.f },
		{ 500.f, 500.f, 1.f, 0.f },
		{ 1000.f, 500.f, 0.f, 0.f }
	};

	Vertex vertex4[] =
	{
		{ 500.f, 0.f, 1.f, 1.f },
		{ 1000.f, 0.f, 0.f, 1.f },
		{ 1000.f, 500.f, 0.f, 0.f }
	};

	RenderingObject2.AddTriangle(vertex3, 3);
	RenderingObject2.AddTriangle(vertex4, 3);
}

void DirectXManager::SetVertexLayout()
{
	m_VertexLayout[0] = { "Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	m_VertexLayout[1] = { "Texcoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 2, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
}

void DirectXManager::CreateDescriptorHipForTexture()
{
	/*D3D12_DESCRIPTOR_HEAP_DESC dhd{};
	dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dhd.NumDescriptors = 4096;
	dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dhd.NodeMask = 0;

	DX_CONTEXT.GetDevice()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&m_Srvheap));*/
}

void DirectXManager::InitUploadRenderingObject()
{
	if (RenderingObject1.Init("./Resources/TEX_Noise.png", 0) == false)
	{
		return;
	}

	if (RenderingObject2.Init("./Resources/Image.png", 1) == false)
	{
		return;
	}
}

void DirectXManager::InitShader()
{
	Shader rootSignatureShader("RootSignature.cso");
	Shader vertexShader("VertexShader.cso");
	Shader pixelShader("PixelShader.cso");

	DX_CONTEXT.GetDevice()->CreateRootSignature(0, rootSignatureShader.GetBuffer(), rootSignatureShader.GetSize(), IID_PPV_ARGS(&m_RootSignature));

	InitPipelineSate(vertexShader, pixelShader);
}

void DirectXManager::InitPipelineSate(Shader& vertexShader, Shader& pixelShader)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gfxPsod = GetPipelineState(m_RootSignature, GetVertexLayout(), GetVertexLayoutCount(), vertexShader, pixelShader);

	if (mSceneColor) {
		gfxPsod.RTVFormats[0] = mSceneColor->GetDesc().Format; // R16G16B16A16_FLOAT
	}

	DX_CONTEXT.GetDevice()->CreateGraphicsPipelineState(&gfxPsod, IID_PPV_ARGS(&m_PipelineStateObj));
}


bool DirectXManager::CreateOffscreen(uint32_t w, uint32_t h)
{
	mWidth = w; mHeight = h;
	auto device = DX_CONTEXT.GetDevice();

	// RTV heap
	if (!mOffscreenRtvHeap) {
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		d.NumDescriptors = 1;
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&mOffscreenRtvHeap));
	}

	// === SceneColor: RTV ===
	D3D12_RESOURCE_DESC td{};
	td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = w; td.Height = h;
	td.DepthOrArraySize = 1; td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	td.SampleDesc = { 1,0 };
	td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clear{}; clear.Format = td.Format; clear.Color[3] = 1.0f;
	CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);

	device->CreateCommittedResource(
		&heapDefault, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&mSceneColor));

	mRtvScene = mOffscreenRtvHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateRenderTargetView(mSceneColor.Get(), nullptr, mRtvScene);

	// === (CPU 경로용) Resolved: UAV 가능 + SRV ===
	DXGI_FORMAT backFmt = DX_WINDOW.GetBackbuffer()->GetDesc().Format;
	td.Format = backFmt;
	td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	device->CreateCommittedResource(
		&heapDefault, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mResolved));

	// Blit용 SRV (mResolved는 CPU 경로에서만 사용)
	{
		D3D12_DESCRIPTOR_HEAP_DESC sh{};
		sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		sh.NumDescriptors = 1;
		sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_BlitSrvHeap));

		mResolvedSrvCPU = m_BlitSrvHeap->GetCPUDescriptorHandleForHeapStart();
		mResolvedSrvGPU = m_BlitSrvHeap->GetGPUDescriptorHandleForHeapStart();

		D3D12_SHADER_RESOURCE_VIEW_DESC sdesc{};
		sdesc.Format = backFmt;
		sdesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sdesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sdesc.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(mResolved.Get(), &sdesc, mResolvedSrvCPU);
	}

	// 상태
	mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	mResolvedState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

	return true;
}

void DirectXManager::DestroyOffscreen()
{
	mSceneColor.Release();
	mResolved.Release();
	mOffscreenRtvHeap.Release();
	m_BlitSrvHeap.Release();

	mRtvScene = {};
	mResolvedSrvCPU = {};
	mResolvedSrvGPU = {};

	mSceneColorState = D3D12_RESOURCE_STATE_COMMON;
	mResolvedState = D3D12_RESOURCE_STATE_COMMON;
}

