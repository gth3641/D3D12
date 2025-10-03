#include "DirectXManager.h"

#include "OnnxManager.h"
#include "ImageManager.h"
#include "Support/Window.h"

#include "Support/Shader.h"

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
	InitUploadRenderingObject();

	UINT w, h;
	DX_WINDOW.GetBackbufferSize(w, h);
	CreateOffscreen(w, h);

	auto device = DX_CONTEXT.GetDevice();

	InitShader();
	InitBlitPipeline();
	CreateSimpleBlitPipeline(); // 새 전용 블릿 PSO 추가
	CreateGreenPipeline();
	CreateFullscreenQuadVB(w, h);

	// ★ ONNX IO 준비 + 디스크립터 구성
	//CreateOnnxResources(w, h);

	// 리소스 상태 초기화
	mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	vbv1 = DirectXManager::GetVertexBufferView(
		DX_MANAGER.GetRenderingObject1().GetVertexBuffer(),
		DX_MANAGER.GetRenderingObject1().GetVertexCount(), sizeof(Vertex));
	vbv2 = DirectXManager::GetVertexBufferView(
		DX_MANAGER.GetRenderingObject2().GetVertexBuffer(),
		DX_MANAGER.GetRenderingObject2().GetVertexCount(), sizeof(Vertex));

	return true;
}

void DirectXManager::Update()
{
	DX_CONTEXT.SignalAndWait();
	static int frame = 0;
	std::vector<Triangle>& triangles = GetRenderingObject1().GetTriangle();
	frame++;
	for (Triangle& triangle : triangles)
	{
		for (int i = 0; i < 3; i++)
		{
			triangle.m_Verticies[i].x += 1.f;
			
			if (frame >= 1500)
			{
				triangle.m_Verticies[i].x -= 1500.f;
			}
		}
	}
	if (frame >= 1500)
	{
		frame -= 1500;
	}

	GetRenderingObject1().UploadCPUResource();
}

void DirectXManager::Shutdown()
{
	DestroyOffscreen();

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
	// ONNX의 in/out 버퍼 생성(모델 shape 반영). 동적 입력이면 W,H 로 맞춰짐.
	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H);

	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	// 컴퓨트 파이프라인 생성(Pre/Post 공용 RS)
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

		mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
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
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	// 화면 사이즈 저장(디스패치에 사용)
	onnx_.Width = W; onnx_.Height = H;
}

void DirectXManager::RecordPreprocess(ID3D12GraphicsCommandList7* cmd)
{
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

	UINT dstW = onnx_.Width;                 // 창 폭(= OnnxTex 폭)
	UINT dstH = onnx_.Height;                // 창 높이(= OnnxTex 높이)

	// CB 업데이트 (W=네트워크 입력 너비, H=높이, C=채널)
	struct CB 
	{ 
		UINT SrcW, SrcH, SrcC, DstW, DstH, _pad[3]; 
	} cb{ inW, inH, inC, dstW, dstH };
	void* p = nullptr;
	mOnnxGPU.CB->Map(0, nullptr, &p); 
	memcpy(p, &cb, sizeof(cb)); 
	mOnnxGPU.CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress());

	// 루트 바인딩: t0=Scene SRV, u0=InputNCHW UAV
	cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.SceneSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.InputUAV_GPU);

	// 디스패치: 모델 입력 해상도 기준
	const UINT TGX = 8, TGY = 8;
	// 0) InputBuffer: (현재 상태) -> UAV (전처리에서 쓰기)
	if (mOnnxInputState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetInputBuffer().Get(),
			mOnnxInputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &toUav);
		mOnnxInputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// (기존 코드 그대로: 힙/RS/PSO 설정, CB 쓰기, 디스패치)
	cmd->Dispatch((inW + TGX - 1) / TGX, (inH + TGY - 1) / TGY, 1);

	// 1) UAV 쓰기 가시화
	auto uav = CD3DX12_RESOURCE_BARRIER::UAV(DX_ONNX.GetInputBuffer().Get());
	cmd->ResourceBarrier(1, &uav);

	// 2) InputBuffer: UAV -> GENERIC_READ (★ ORT가 바로 읽게)
	{
		auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
			DX_ONNX.GetInputBuffer().Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		cmd->ResourceBarrier(1, &toRead);
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}
}

void DirectXManager::RecordPostprocess(ID3D12GraphicsCommandList7* cmd)
{
	// 0) … Output UAV->NPSR, OnnxTex (tracked)->UAV 전환 (지금 코드 유지)

	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(onnx_.PostPSO.Get());

	// Src = 모델 출력(224x224x3), Dst = 화면 크기
	const UINT srcW = 224, srcH = 224, srcC = 3;
	const UINT dstW = onnx_.Width;      // CreateOnnxResources(W,H)에서 받은 화면 크기
	const UINT dstH = onnx_.Height;

	struct CBData {
		UINT SrcW, SrcH, SrcC, _r0;
		UINT DstW, DstH, _r1, _r2;
	} CB{ srcW, srcH, srcC, 0, dstW, dstH, 0, 0 };

	void* p = nullptr;
	mOnnxGPU.CB->Map(0, nullptr, &p);
	std::memcpy(p, &CB, sizeof(CB));
	mOnnxGPU.CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress());

	// t0 = Output SRV(CHW), u0 = OnnxTex UAV
	cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.OnnxTexUAV_GPU);

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
			mOnnxGPU.OnnxTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
}

void DirectXManager::RunOnnxGPU()
{
	DX_ONNX.Run(); 
}

void DirectXManager::CreateFullscreenQuadVB(UINT w, UINT h)
{
	struct Vtx { float x, y, u, v; };
	Vtx quad[6] = {
		{ 0.f,    0.f,    0.f, 0.f },
		{ 0.f,    (float)h, 0.f, 1.f },
		{ (float)w, (float)h, 1.f, 1.f },
		{ 0.f,    0.f,    0.f, 0.f },
		{ (float)w, (float)h, 1.f, 1.f },
		{ (float)w, 0.f,    1.f, 0.f },
	};

	const UINT vbSize = sizeof(quad);
	auto dev = DX_CONTEXT.GetDevice();

	// Default(VB)
	CD3DX12_HEAP_PROPERTIES hpDefault(D3D12_HEAP_TYPE_DEFAULT);
	auto desc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
	(dev->CreateCommittedResource(
		&hpDefault, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mFSQuadVB)));

	// Upload
	ComPointer<ID3D12Resource> upload;
	CD3DX12_HEAP_PROPERTIES hpUpload(D3D12_HEAP_TYPE_UPLOAD);
	(dev->CreateCommittedResource(
		&hpUpload, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));

	// 업로드 버퍼에 데이터 써넣기
	{
		void* p = nullptr;
		D3D12_RANGE r{ 0,0 };
		(upload->Map(0, &r, &p));
		std::memcpy(p, quad, vbSize);
		upload->Unmap(0, nullptr);
	}

	// GPU 복사 + 상태 전이
	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
	cmd->CopyBufferRegion(mFSQuadVB.Get(), 0, upload.Get(), 0, vbSize);

	auto toVB = CD3DX12_RESOURCE_BARRIER::Transition(
		mFSQuadVB.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
	);
	cmd->ResourceBarrier(1, &toVB);

	DX_CONTEXT.ExecuteCommandList();  // ★ 업로드 리소스는 여기까지 생존해야 안전

	// VBV
	mFSQuadVBV.BufferLocation = mFSQuadVB->GetGPUVirtualAddress();
	mFSQuadVBV.SizeInBytes = vbSize;
	mFSQuadVBV.StrideInBytes = sizeof(Vtx);
}

bool DirectXManager::CreateSimpleBlitPipeline()
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_ROOT_PARAMETER1   param[1];
	param[0].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.ShaderRegister = 0;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(_countof(param), param, 1, &samp,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPointer<ID3DBlob> sigBlob, err;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &err);
	if (FAILED(hr)) return false;

	hr = dev->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&m_BlitRS2));
	if (FAILED(hr)) return false;

	ComPointer<ID3DBlob> vs, ps;
	hr = D3DCompileFromFile(L"./Shaders/vs_blit.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &err);
	if (FAILED(hr)) return false;
	hr = D3DCompileFromFile(L"./Shaders/ps_blit.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, &err);
	if (FAILED(hr)) return false;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_BlitRS2.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.InputLayout = { nullptr, 0 };                 // 풀스크린 삼각형 (SV_VertexID)
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // (옵션) 혹시 모를 컬링 회피
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthEnable = FALSE;
	pso.SampleDesc = { 1, 0 };
	pso.NumRenderTargets = 1;
	pso.SampleMask = UINT_MAX;                       

	if (auto back = DX_WINDOW.GetBackbuffer())
		pso.RTVFormats[0] = back->GetDesc().Format;
	else
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	hr = dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_BlitPSO2));
	return SUCCEEDED(hr);
}



bool DirectXManager::CreateGreenPipeline()
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	// 루트시그: 파라미터 0개
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(0, nullptr, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPointer<ID3DBlob> sig, err;
	if (FAILED(D3D12SerializeVersionedRootSignature(&rsDesc, &sig, &err))) return false;
	if (FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_RS_Green)))) return false;

	// 셰이더 컴파일
	ComPointer<ID3DBlob> vs, ps;
	if (FAILED(D3DCompileFromFile(L"./Shaders/vs_green.hlsl", nullptr, nullptr,
		"main", "vs_5_0", 0, 0, &vs, nullptr))) return false;
	if (FAILED(D3DCompileFromFile(L"./Shaders/ps_green.hlsl", nullptr, nullptr,
		"main", "ps_5_0", 0, 0, &ps, nullptr))) return false;

	// PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_RS_Green.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.InputLayout = { nullptr, 0 }; // 입력 없음
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthEnable = FALSE;
	pso.SampleMask = UINT_MAX;
	pso.NumRenderTargets = 1;

	// ★ 반드시 ‘현재 스왑체인 백버퍼 포맷’과 동일해야 함
	if (auto back = DX_WINDOW.GetBackbuffer())
		pso.RTVFormats[0] = back->GetDesc().Format;
	else
		pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	pso.SampleDesc = { 1,0 };

	return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_PSO_Green)));
}

void DirectXManager::DrawConstantGreen(ID3D12GraphicsCommandList7* cmd)
{
	UINT bw = 0, bh = 0; DX_WINDOW.GetBackbufferSize(bw, bh);
	D3D12_VIEWPORT vp{ 0,0,(float)bw,(float)bh,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)bw,(LONG)bh };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	/*auto rtv = DX_WINDOW.GetRtvHandle(DX_WINDOW.GetBackBufferIndex());
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);*/

	cmd->SetPipelineState(m_PSO_Green.Get());
	cmd->SetGraphicsRootSignature(m_RS_Green.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 풀스크린 삼각형: 버텍스버퍼 없음
	cmd->DrawInstanced(3, 1, 0, 0);
}

void DirectXManager::DrawConstantGreen_Standalone()
{
	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();

	// 1) 백버퍼 전이 PRESENT -> RENDER_TARGET
	ID3D12Resource* back = DX_WINDOW.GetBackbuffer();
	auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
		back,
		D3D12_RESOURCE_STATE_PRESENT,            // ★ 혹시 COMMON으로 트래킹한다면 거기에 맞춰 수정
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmd->ResourceBarrier(1, &toRT);

	// 2) RTV 바인딩 + 클리어 (검증용)
	D3D12_CPU_DESCRIPTOR_HANDLE rtv =
		DX_WINDOW.GetRtvHandle(DX_WINDOW.GetBackBufferIndex());
	const FLOAT clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);

	// 3) 뷰포트/시저
	UINT bw = 0, bh = 0; DX_WINDOW.GetBackbufferSize(bw, bh);
	D3D12_VIEWPORT vp{ 0,0,(float)bw,(float)bh,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)bw,(LONG)bh };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	// 4) 그리기
	cmd->SetPipelineState(m_PSO_Green.Get());
	cmd->SetGraphicsRootSignature(m_RS_Green.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);

	// 5) 다시 PRESENT로 전이
	auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	cmd->ResourceBarrier(1, &toPresent);

	// 6) 제출 + 프레젠트
	DX_CONTEXT.ExecuteCommandList();
	DX_WINDOW.Present();

}

void DirectXManager::ResizeOnnxResources(UINT W, UINT H)
{
	if (W == onnx_.Width && H == onnx_.Height) return;

	// (1) 기존 Onnx GPU 리소스 해제
	mOnnxGPU.OnnxTex.Release();
	mOnnxGPU.CB.Release();
	mOnnxGPU.Heap.Release();
	mOnnxGPU.SceneSRV_CPU = {};
	mOnnxGPU.SceneSRV_GPU = {};
	mOnnxGPU.InputUAV_CPU = {};
	mOnnxGPU.InputUAV_GPU = {};
	mOnnxGPU.OnnxTexUAV_CPU = {};
	mOnnxGPU.OnnxTexUAV_GPU = {};
	mOnnxGPU.OnnxTexSRV_CPU = {};
	mOnnxGPU.OnnxTexSRV_GPU = {};

	// (2) 새 크기로 재생성 (SceneColor가 이미 새로 만들어진 뒤여야 함)
	CreateOnnxResources(W, H);

	// (3) DirectML 경로도 IO 갱신한다면
	DX_ONNX.ResizeIO(DX_CONTEXT.GetDevice(), W, H);
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


void DirectXManager::Resize()
{
	DestroyOffscreen();
	UINT w, h;
	DX_WINDOW.GetBackbufferSize(w, h);
	CreateOffscreen(w, h);
	CreateFullscreenQuadVB(w, h);

	ResizeOnnxResources(w, h);
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

	UploadGPUResource(cmd);

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
	struct ScreenCB { float ViewSize[2]; };
	ScreenCB scb{ (float)DX_WINDOW.GetWidth(), (float)DX_WINDOW.GetHeight() };

	cmd->SetGraphicsRoot32BitConstants(0, 3, color, 0);
	cmd->SetGraphicsRoot32BitConstants(1, 2, &scb, 0);

	ID3D12DescriptorHeap* srvHeap = DX_IMAGE.GetSrvheap();
	cmd->SetDescriptorHeaps(1, &srvHeap);

	cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject2().GetTestIndex()));
	cmd->IASetVertexBuffers(0, 1, &vbv2);
	cmd->DrawInstanced(DX_MANAGER.GetRenderingObject2().GetVertexCount(), 1, 0, 0);

	cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(DX_MANAGER.GetRenderingObject1().GetTestIndex()));
	cmd->IASetVertexBuffers(0, 1, &vbv1);
	cmd->DrawInstanced(DX_MANAGER.GetRenderingObject1().GetVertexCount(), 1, 0, 0);

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



void DirectXManager::BlitToBackbuffer(ID3D12GraphicsCommandList7* cmd)
{
	// OnnxTex 상태: PS에서 읽게 보장
	if (mOnnxTexState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mOnnxGPU.OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	UINT bw = 0, bh = 0; DX_WINDOW.GetBackbufferSize(bw, bh);
	D3D12_VIEWPORT vp{ 0,0,(float)bw,(float)bh,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)bw,(LONG)bh };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	auto rtv = DX_WINDOW.GetRtvHandle(DX_WINDOW.GetBackBufferIndex());
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	cmd->SetPipelineState(m_BlitPSO2.Get());
	cmd->SetGraphicsRootSignature(m_BlitRS2.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 이 PSO는 SRV 힙(t0)만 필요
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	// RS 파라미터 0 = t0
	cmd->SetGraphicsRootDescriptorTable(0, mOnnxGPU.OnnxTexSRV_GPU);

	cmd->IASetVertexBuffers(0, 1, &mFSQuadVBV);
	cmd->DrawInstanced(6, 1, 0, 0);

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
		{ 0.f,  0.f, 0.0f, 0.f },
		{ 0.f, 1000, 0.f, 1.f },
		{ 1000, 1000, 1.f, 1.f }
	};

	Vertex vertex4[] =
	{
		{ 0.f, 0.f, 0.f, 0.f },
		{ 1000, 0.f, 1.f, 0.f },
		{ 1000, 1000, 1.f, 1.f }
	};

	RenderingObject2.AddTriangle(vertex3, 3);
	RenderingObject2.AddTriangle(vertex4, 3);
}

void DirectXManager::SetVertexLayout()
{
	m_VertexLayout[0] = { "Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	m_VertexLayout[1] = { "Texcoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 2, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
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
	m_Width = w; m_Height = h;
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

	// 상태
	mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	return true;
}

void DirectXManager::DestroyOffscreen()
{
	mSceneColor.Release();
	//mResolved.Release();
	mOffscreenRtvHeap.Release();
	m_BlitSrvHeap.Release();

	mRtvScene = {};
	mResolvedSrvCPU = {};
	mResolvedSrvGPU = {};

	mSceneColorState = D3D12_RESOURCE_STATE_COMMON;
}

