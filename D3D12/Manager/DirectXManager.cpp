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

static DXGI_FORMAT NonSRGB(DXGI_FORMAT f) {
	switch (f) {
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
	default: return f;
	}
}

static void FillBufferWithFloat(
	ID3D12GraphicsCommandList7* cmd,
	ID3D12Resource* dst,      // UAV 대상 버퍼 (DEFAULT, UAV 플래그)
	float value,
	std::vector<ComPointer<ID3D12Resource>>& keepAlive)
{
	if (!cmd || !dst) return;
	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	auto d = dst->GetDesc();
	const UINT64 bytes = d.Width;
	if (!bytes) return;

	// 1) 업로드 버퍼 생성
	ComPointer<ID3D12Resource> upload;
	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
	auto rd = CD3DX12_RESOURCE_DESC::Buffer(bytes);
	if (FAILED(dev->CreateCommittedResource(
		&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&upload)))) return;

	// 2) 업로드 버퍼 메모리에 value로 채우기
	{
		void* p = nullptr;
		D3D12_RANGE r{ 0,0 };
		if (SUCCEEDED(upload->Map(0, &r, &p)) && p) {
			const size_t count = bytes / sizeof(float);
			std::fill_n(reinterpret_cast<float*>(p), count, value);
			// 남는 바이트(4바이트 미만)는 0으로 냅둠
			upload->Unmap(0, nullptr);
		}
	}

	// 3) UAV 버퍼는 상태를 COPY_DEST로 바꾸고 복사 → UAV barrier로 작성 보장
	auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(
		dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
	cmd->ResourceBarrier(1, &toCopy);

	cmd->CopyBufferRegion(dst, 0, upload.Get(), 0, bytes);
	keepAlive.emplace_back(std::move(upload));
	auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
		dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmd->ResourceBarrier(1, &toUav);

	auto uav = CD3DX12_RESOURCE_BARRIER::UAV(dst);
	cmd->ResourceBarrier(1, &uav);
}

static bool SafeCopyBufferToReadback(
	ID3D12Resource* srcBuf,
	D3D12_RESOURCE_STATES assumedState,          // 현재 우리가 알고 있는 상태 (보통 UAV)
	D3D12_RESOURCE_STATES restoreState,          // 끝나고 돌려놓을 상태 (보통 UAV)
	std::vector<uint8_t>& out)
{
	if (!srcBuf) return false;

	auto dev = DX_CONTEXT.GetDevice();
	auto d = srcBuf->GetDesc();
	if (d.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) return false;

	// 1) Readback buffer
	ComPointer<ID3D12Resource> rb;
	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_READBACK);
	auto rd = CD3DX12_RESOURCE_DESC::Buffer(d.Width);
	if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb))))
		return false;

	// 2) Record on a NEW command list (important!)
	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();

	// Ensure UAV writes visible before copy
	auto srcBufBarr = CD3DX12_RESOURCE_BARRIER::UAV(srcBuf);
	cmd->ResourceBarrier(1, &srcBufBarr);

	auto barr = CD3DX12_RESOURCE_BARRIER::Transition(srcBuf, assumedState, D3D12_RESOURCE_STATE_COPY_SOURCE);
	if (assumedState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
		cmd->ResourceBarrier(1, &barr);
	}

	cmd->CopyResource(rb.Get(), srcBuf);

	auto barr2 = CD3DX12_RESOURCE_BARRIER::Transition(srcBuf, D3D12_RESOURCE_STATE_COPY_SOURCE, restoreState);
	if (restoreState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
		cmd->ResourceBarrier(1, &barr2);
	}

	DX_CONTEXT.ExecuteCommandList();
	DX_CONTEXT.SignalAndWait();

	// 3) Read back
	out.resize((size_t)d.Width);
	void* p = nullptr;
	D3D12_RANGE r{ 0, (SIZE_T)d.Width };
	if (SUCCEEDED(rb->Map(0, &r, &p)) && p) {
		memcpy(out.data(), p, (size_t)d.Width);
		rb->Unmap(0, nullptr);
		return true;
	}
	return false;
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
	CreateOnnxResources(w, h);

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
	m_StyleObject.UploadGPUResource(cmdList);
}


void DirectXManager::CreateOnnxResources(UINT W, UINT H)
{
	// ★ 스타일 텍스처 실제 크기 얻기
	ID3D12Resource* styleTex = m_StyleObject.GetImage()->GetTexture();
	auto sDesc = styleTex->GetDesc();
	UINT styleW = (UINT)sDesc.Width;
	UINT styleH = sDesc.Height;

	// 1) ONNX IO 준비 (★ 4 인자)
	DX_ONNX.PrepareIO(DX_CONTEXT.GetDevice(), W, H, styleW, styleH);

	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	// 2) 컴퓨트 파이프라인(전/후처리)
	if (!CreateOnnxComputePipeline()) return;

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
			D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mOnnxGPU.OnnxTex));

		mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
	}

	// 4) 디스크립터 힙
	{
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 12; // ★ 약간 여유
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&mOnnxGPU.Heap));

		D3D12_DESCRIPTOR_HEAP_DESC dCPU = d;
		dCPU.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dev->CreateDescriptorHeap(&dCPU, IID_PPV_ARGS(&mHeapCPU));
	}
	auto gpuStart = mOnnxGPU.Heap->GetGPUDescriptorHandleForHeapStart();
	auto cpuGPU = mOnnxGPU.Heap->GetCPUDescriptorHandleForHeapStart();
	auto cpuOnly = mHeapCPU->GetCPUDescriptorHandleForHeapStart();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto nthGPU = [&](UINT i) { auto h = gpuStart; h.ptr += i * inc; return h; };
	auto nthCPU_GPU = [&](UINT i) { auto h = cpuGPU;  h.ptr += i * inc; return h; };
	auto nthCPUONLY = [&](UINT i) { auto h = cpuOnly; h.ptr += i * inc; return h; };

	// (0) Scene SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = mSceneColor->GetDesc().Format;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		mOnnxGPU.SceneSRV_CPU = nthCPU_GPU(0);
		mOnnxGPU.SceneSRV_GPU = nthGPU(0);
		dev->CreateShaderResourceView(mSceneColor.Get(), &s, mOnnxGPU.SceneSRV_CPU);
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

		mOnnxGPU.InputContentUAV_CPU = nthCPU_GPU(1);
		mOnnxGPU.InputContentUAV_GPU = nthGPU(1);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferContent().Get(), nullptr, &u, mOnnxGPU.InputContentUAV_CPU);
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

		mOnnxGPU.InputStyleUAV_CPU = nthCPU_GPU(2);
		mOnnxGPU.InputStyleUAV_GPU = nthGPU(2);
		dev->CreateUnorderedAccessView(DX_ONNX.GetInputBufferStyle().Get(), nullptr, &u, mOnnxGPU.InputStyleUAV_CPU);
	}
	// (3) OnnxTex UAV
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
		u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		mOnnxGPU.OnnxTexUAV_CPU = nthCPU_GPU(3);
		mOnnxGPU.OnnxTexUAV_GPU = nthGPU(3);
		dev->CreateUnorderedAccessView(mOnnxGPU.OnnxTex.Get(), nullptr, &u, mOnnxGPU.OnnxTexUAV_CPU);
	}
	// (4) OnnxTex SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC s{};
		s.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		s.Texture2D.MipLevels = 1;
		mOnnxGPU.OnnxTexSRV_CPU = nthCPU_GPU(4);
		mOnnxGPU.OnnxTexSRV_GPU = nthGPU(4);
		dev->CreateShaderResourceView(mOnnxGPU.OnnxTex.Get(), &s, mOnnxGPU.OnnxTexSRV_CPU);
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

		mOnnxGPU.ModelOutSRV_CPU = nthCPU_GPU(5);
		mOnnxGPU.ModelOutSRV_GPU = nthGPU(5);
		dev->CreateShaderResourceView(DX_ONNX.GetOutputBuffer().Get(), &s, mOnnxGPU.ModelOutSRV_CPU);
	}
	// (6) StyleTex SRV
	{
		mOnnxGPU.StyleSRV_CPU = nthCPU_GPU(6);
		mOnnxGPU.StyleSRV_GPU = nthGPU(6);
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

		mOnnxGPU.InputContentSRV_CPU = nthCPU_GPU(7);
		mOnnxGPU.InputContentSRV_GPU = nthGPU(7);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferContent().Get(), &s, mOnnxGPU.InputContentSRV_CPU);
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

		mOnnxGPU.InputStyleSRV_CPU = nthCPU_GPU(8);
		mOnnxGPU.InputStyleSRV_GPU = nthGPU(8);
		dev->CreateShaderResourceView(DX_ONNX.GetInputBufferStyle().Get(), &s, mOnnxGPU.InputStyleSRV_CPU);
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
		mOnnxGPU.InputStyleUAV_GPU_ForClear = nthGPU(slot);
		mOnnxGPU.InputStyleUAV_CPU_ForClear = nthCPUONLY(slot);

		// GPU-visible heap
		dev->CreateUnorderedAccessView(
			DX_ONNX.GetInputBufferStyle().Get(), nullptr, &uRaw, nthCPU_GPU(slot));
		// CPU-only heap
		dev->CreateUnorderedAccessView(
			DX_ONNX.GetInputBufferStyle().Get(), nullptr, &uRaw, mOnnxGPU.InputStyleUAV_CPU_ForClear);
	}

	// 5) CB
	{
		const UINT Slice = (UINT)((sizeof(UINT) * 8 + 255) & ~255);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(Slice * 2);
		dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mOnnxGPU.CB));
		mOnnxInputState = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	onnx_.Width = W; onnx_.Height = H;
}

void DirectXManager::RecordPreprocess(ID3D12GraphicsCommandList7* cmd)
{
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(onnx_.PrePSO.Get());

	const UINT Slice = (UINT)((sizeof(UINT) * 8 + 255) & ~255);

	// ----- (A) CONTENT -----
	{
		UINT inWc, inHc, inCc;
		const auto& ish = DX_ONNX.GetInputShapeContent(); // [1,3,H,W]
		inCc = (UINT)ish[1]; inHc = (UINT)ish[2]; inWc = (UINT)ish[3];

		UINT flagsC = 0;
		auto fmtC = mSceneColor->GetDesc().Format;
		if (fmtC == DXGI_FORMAT_B8G8R8A8_UNORM || fmtC == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			flagsC |= 0x10; // BGR swap

		struct CB { UINT W, H, C, Flags; } cb{ inWc, inHc, inCc, flagsC };
		uint8_t* base = nullptr;
		mOnnxGPU.CB->Map(0, nullptr, (void**)&base);
		memcpy(base + 0, &cb, sizeof(cb));
		mOnnxGPU.CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress() + 0);

		// t0/u0
		WriteSceneSRVToSlot0();
		cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.SceneSRV_GPU);
		cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.InputContentUAV_GPU);

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
		ID3D12Resource* styleTex = m_StyleObject.GetImage()->GetTexture();
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
		auto fmt = m_StyleObject.GetImage()->GetTextureData().giPixelFormat;
		if (fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			flags |= 0x10;

		struct CB { UINT W, H, C, Flags; } cb{ inWs, inHs, inCs, flags };
		uint8_t* base = nullptr;
		mOnnxGPU.CB->Map(0, nullptr, (void**)&base);
		memcpy(base + Slice, &cb, sizeof(cb));
		mOnnxGPU.CB->Unmap(0, nullptr);
		cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress() + Slice);

		// StyleTex: PSR -> NPSR
		static D3D12_RESOURCE_STATES sStyleTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		if (sStyleTexState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
			auto b = CD3DX12_RESOURCE_BARRIER::Transition(
				styleTex, sStyleTexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cmd->ResourceBarrier(1, &b);
			sStyleTexState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}

		// In RecordPreprocess (STYLE section)
		WriteStyleSRVToSlot6(styleTex, m_StyleObject.GetImage()->GetTextureData().giPixelFormat);
		cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.StyleSRV_GPU);   // t0
		cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.InputStyleUAV_GPU); // u0

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

void DirectXManager::RecordPostprocess(ID3D12GraphicsCommandList7* cmd)
{
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(onnx_.PostPSO.Get());

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
			mOnnxGPU.OnnxTex.Get(),
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
			DX_ONNX.GetOutputBuffer().Get(), &s, mOnnxGPU.ModelOutSRV_CPU);
	}

	//{
	//	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	//	s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	//	s.Format = DXGI_FORMAT_UNKNOWN;
	//	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	//	s.Buffer.FirstElement = 0;
	//	s.Buffer.NumElements = srcW * srcH * srcC; // 컨텐트 shape
	//	s.Buffer.StructureByteStride = sizeof(float);
	//	s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	//	DX_CONTEXT.GetDevice()->CreateShaderResourceView(
	//		DX_ONNX.GetInputBufferContent().Get(), &s, mOnnxGPU.ModelOutSRV_CPU);
	//}


	// 4) CB 업데이트 (SrcW/H/C, DstW/H)
	const UINT dstW = onnx_.Width;
	const UINT dstH = onnx_.Height;

	struct CBData {
		UINT SrcW, SrcH, SrcC, _r0;
		UINT DstW, DstH, _r1, _r2;
		float Gain, Bias, _f0, _f1;
	};
	CBData cb{ srcW, srcH, srcC, 0, dstW, dstH, 0, 0, 1.0f, 0.0f, 0, 0 }; // ★ 먼저 크게

	void* p = nullptr;
	mOnnxGPU.CB->Map(0, nullptr, &p); std::memcpy(p, &cb, sizeof(cb)); mOnnxGPU.CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress());

	// 5) 바인딩: t0=ModelOut SRV, u0=OnnxTex UAV
	cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.ModelOutSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.OnnxTexUAV_GPU);

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
			mOnnxGPU.OnnxTex.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
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

	// RS: t0(SRV)만
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
	if (FAILED(D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &err))) return false;
	if (FAILED(dev->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&m_BlitRS2)))) return false;

	ComPointer<ID3DBlob> vs, ps;
	if (FAILED(D3DCompileFromFile(L"./Shaders/vs_blit.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &err))) return false;
	if (FAILED(D3DCompileFromFile(L"./Shaders/ps_blit.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, &err))) return false;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_BlitRS2.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

	// ★ 입력 레이아웃 없음 (SV_VertexID)
	pso.InputLayout = { nullptr, 0 };

	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthEnable = FALSE;
	pso.SampleDesc = { 1,0 };
	pso.NumRenderTargets = 1;
	pso.SampleMask = UINT_MAX;
	pso.RTVFormats[0] = DX_WINDOW.GetBackbuffer() ? DX_WINDOW.GetBackbuffer()->GetDesc().Format
		: DXGI_FORMAT_R8G8B8A8_UNORM;

	return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_BlitPSO2)));
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

void DirectXManager::WriteSceneSRVToSlot0()
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.Format = mSceneColor->GetDesc().Format;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;
	dev->CreateShaderResourceView(mSceneColor.Get(), &s, mOnnxGPU.SceneSRV_CPU);
}

void DirectXManager::WriteStyleSRVToSlot6(ID3D12Resource* styleTex, DXGI_FORMAT fmt)
{
	if (!styleTex) return;
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.Format = fmt; // ★ NonSRGB(fmt) 말고 원래 fmt 그대로
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	s.Texture2D.MipLevels = 1;
	DX_CONTEXT.GetDevice()->CreateShaderResourceView(styleTex, &s, mOnnxGPU.StyleSRV_CPU);
}

void DirectXManager::Debug_ShowPreprocessedToScreen(ID3D12GraphicsCommandList7* cmd, bool showContent)
{
	// OnnxTex -> UAV
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mOnnxGPU.OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(m_DebugShowInputPSO.Get());
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);

	// t0 = (content/style) **SRV**, u0 = OnnxTex UAV
	if (showContent)
		cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.InputContentSRV_GPU); // ★ SRV!
	else
		cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.InputStyleSRV_GPU);   // ★ SRV!
	cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.OnnxTexUAV_GPU);

	// CB (W,H,C)
	const auto& ish = showContent ? DX_ONNX.GetInputShapeContent() : DX_ONNX.GetInputShapeStyle();
	struct CB { UINT W, H, C, _; } cb{ (UINT)ish[3], (UINT)ish[2], (UINT)ish[1], 0 };
	void* p = nullptr; mOnnxGPU.CB->Map(0, nullptr, &p); memcpy(p, &cb, sizeof(cb)); mOnnxGPU.CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress());

	const UINT TG = 8;
	cmd->Dispatch((cb.W + TG - 1) / TG, (cb.H + TG - 1) / TG, 1);

	// OnnxTex -> PSR, 블릿
	auto toPS = CD3DX12_RESOURCE_BARRIER::Transition(
		mOnnxGPU.OnnxTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toPS);
	mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	BlitToBackbuffer(cmd);
}

void DirectXManager::Debug_CopyStyleToScreen(ID3D12GraphicsCommandList7* cmd)
{
	// OnnxTex -> UAV 전이
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mOnnxGPU.OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	cmd->SetComputeRootSignature(onnx_.PreRS.Get());
	cmd->SetPipelineState(m_CopyTexToTex2DPSO.Get()); // 이건 CreateOnnxComputePipeline에서 생성
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);

	// t0=Style SRV, u0=OnnxTex UAV
	WriteStyleSRVToSlot6(m_StyleObject.GetImage()->GetTexture(),
		m_StyleObject.GetImage()->GetTextureData().giPixelFormat);
	cmd->SetComputeRootDescriptorTable(0, mOnnxGPU.StyleSRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, mOnnxGPU.OnnxTexUAV_GPU);

	// CB = (OnnxTex 크기)
	struct CB { UINT W, H, _r0, _r1; } cb{ onnx_.Width, onnx_.Height, 0,0 };
	void* p = nullptr; mOnnxGPU.CB->Map(0, nullptr, &p); memcpy(p, &cb, sizeof(cb)); mOnnxGPU.CB->Unmap(0, nullptr);
	cmd->SetComputeRootConstantBufferView(2, mOnnxGPU.CB->GetGPUVirtualAddress());

	cmd->Dispatch((cb.W + 7) / 8, (cb.H + 7) / 8, 1);

	// OnnxTex->PSR, 블릿
	auto toPS = CD3DX12_RESOURCE_BARRIER::Transition(
		mOnnxGPU.OnnxTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toPS);
	mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	BlitToBackbuffer(cmd);
}

void DirectXManager::Debug_DumpOrtOutput(ID3D12GraphicsCommandList7* cmd)
{
	DX_CONTEXT.ExecuteCommandList();
	DX_CONTEXT.SignalAndWait();

	// ONNX Run() 직후에는 출력 버퍼가 보통 UAV로 쓰였다고 가정
	std::vector<uint8_t> bytes;
	if (!SafeCopyBufferToReadback(DX_ONNX.GetOutputBuffer().Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		bytes)) return;

	const float* f = reinterpret_cast<const float*>(bytes.data());
	const size_t n = bytes.size() / sizeof(float);
	if (!n) return;

	float mn = +1e9f, mx = -1e9f;
	for (size_t i = 0; i < n; ++i) { mn = (mn < f[i] ? mn : f[i]); mx = (mx > f[i] ? mx : f[i]); }

	char buf[512];
	int k = (int)std::min<size_t>(16, n);
	std::string first;
	for (int i = 0; i < k; ++i) { char t[32]; sprintf_s(t, " %.5f", f[i]); first += t; }
	sprintf_s(buf, "[ORT OUT SAFE] n=%llu  min=%.6f  max=%.6f  first16:%s\n",
		(unsigned long long)n, mn, mx, first.c_str());
	OutputDebugStringA(buf);
}

void DirectXManager::Debug_DumpBuffer(ID3D12Resource* src, const char* tag)
{
	if (!src) return;
	std::vector<uint8_t> bytes;
	if (!SafeCopyBufferToReadback(src,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		bytes)) return;

	const float* f = reinterpret_cast<const float*>(bytes.data());
	const size_t n = bytes.size() / sizeof(float);
	if (!n) return;

	float mn = +1e9f, mx = -1e9f;
	for (size_t i = 0; i < n; ++i) { mn = (mn < f[i] ? mn : f[i]); mx = (mx > f[i] ? mx : f[i]); }

	char buf[512];
	char buf2[512];
	int k = (int)std::min<size_t>(16, n);
	std::string first;
	for (int i = 0; i < k; ++i) { char t[32]; sprintf_s(t, " %.5f", f[i]); first += t; }
	sprintf_s(buf, "[%s] ", tag);
	sprintf_s(buf2, "n=%llu  min=%.6f  max=%.6f  first16:%s\n", (unsigned long long)n, mn, mx, first.c_str());
	OutputDebugStringA(buf);
	OutputDebugStringA(buf2);
}

void DirectXManager::ResizeOnnxResources(UINT W, UINT H)
{
	if (W == onnx_.Width && H == onnx_.Height) return;

	mOnnxGPU.OnnxTex.Release();
	mOnnxGPU.CB.Release();
	mOnnxGPU.Heap.Release();
	mOnnxGPU = {};

	// ★ 스타일 텍스처 실제 크기
	ID3D12Resource* styleTex = m_StyleObject.GetImage()->GetTexture();
	auto sDesc = styleTex->GetDesc();
	UINT styleW = (UINT)sDesc.Width;
	UINT styleH = sDesc.Height;

	// ★ 4 인자
	DX_ONNX.ResizeIO(DX_CONTEXT.GetDevice(), W, H, styleW, styleH);

	CreateOnnxResources(W, H);
	onnx_.Width = W; onnx_.Height = H;
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

	{
		ComPointer<ID3DBlob> csDbg, err;
		HRESULT hr = D3DCompileFromFile(L"./Shaders/cs_debug_show_input.hlsl", nullptr, nullptr,
			"main", "cs_5_0", 0, 0, &csDbg, &err);
		if (SUCCEEDED(hr)) {
			D3D12_COMPUTE_PIPELINE_STATE_DESC dbg{};
			dbg.pRootSignature = onnx_.PreRS.Get(); // t0/u0/b0 동일 RS 재사용
			dbg.CS = { csDbg->GetBufferPointer(), csDbg->GetBufferSize() };
			DX_CONTEXT.GetDevice()->CreateComputePipelineState(&dbg, IID_PPV_ARGS(&m_DebugShowInputPSO));
		}
	}

	{
		ComPointer<ID3DBlob> csDbg, err;
		HRESULT hr = D3DCompileFromFile(L"./Shaders/cs_copy_tex_to_tex2d.hlsl", nullptr, nullptr,
			"main", "cs_5_0", 0, 0, &csDbg, &err);
		if (SUCCEEDED(hr)) {
			D3D12_COMPUTE_PIPELINE_STATE_DESC dbg{};
			dbg.pRootSignature = onnx_.PreRS.Get(); // t0/u0/b0 동일 RS 재사용
			dbg.CS = { csDbg->GetBufferPointer(), csDbg->GetBufferSize() };
			DX_CONTEXT.GetDevice()->CreateComputePipelineState(&dbg, IID_PPV_ARGS(&m_CopyTexToTex2DPSO));
		}
	}

	{
		ComPointer<ID3DBlob> csDbg, err;
		HRESULT hr = D3DCompileFromFile(L"./Shaders/cs_fill_buffer.hlsl", nullptr, nullptr,
			"main", "cs_5_0", 0, 0, &csDbg, &err);
		if (SUCCEEDED(hr)) {
			D3D12_COMPUTE_PIPELINE_STATE_DESC dbg{};
			dbg.pRootSignature = onnx_.PreRS.Get(); // t0/u0/b0 동일 RS 재사용
			dbg.CS = { csDbg->GetBufferPointer(), csDbg->GetBufferSize() };
			DX_CONTEXT.GetDevice()->CreateComputePipelineState(&dbg, IID_PPV_ARGS(&m_FillPSO));
		}

	}

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
	// OnnxTex는 PS에서 읽음
	if (mOnnxTexState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mOnnxGPU.OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	// 뷰포트/시저
	UINT bw = 0, bh = 0; DX_WINDOW.GetBackbufferSize(bw, bh);
	D3D12_VIEWPORT vp{ 0,0,(float)bw,(float)bh,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)bw,(LONG)bh };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	// ★ RTV 바인딩(혹시 BeginFrame이 안 했다면 대비)
	auto rtv = DX_WINDOW.GetRtvHandle(DX_WINDOW.GetBackBufferIndex());
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	// 파이프라인/RS
	cmd->SetPipelineState(m_BlitPSO2.Get());
	cmd->SetGraphicsRootSignature(m_BlitRS2.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// SRV 힙 바인딩 (t0 = OnnxTex)
	ID3D12DescriptorHeap* heaps[] = { mOnnxGPU.Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootDescriptorTable(0, mOnnxGPU.OnnxTexSRV_GPU);

	// SV_VertexID 삼각형 1개
	cmd->IASetVertexBuffers(0, 0, nullptr);
	cmd->DrawInstanced(3, 1, 0, 0);
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

	if (m_StyleObject.Init("./Resources/Style_.png", 2) == false)
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

