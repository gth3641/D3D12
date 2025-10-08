#include "DirectXManager.h"

#include "OnnxManager.h"
#include "ImageManager.h"
#include "Support/Window.h"
#include "Support/Onnx/OnnxService.h"
#include "Support/Shader.h"

#include "D3D/DXContext.h"
#include <wrl.h>
#include <vector>
#include <assert.h>
#include <iostream>

static const char* g_VS = R"(
cbuffer MVP : register(b0) { float4x4 uMVP; };

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut main(VSIn i){
    VSOut o;
    o.pos = mul(float4(i.pos,1), uMVP);
    o.uv  = i.uv;
    return o;
}
)";

static const char* g_PS = R"(
Texture2D    gTex : register(t0);
SamplerState gSmp : register(s0);

struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(PSIn i) : SV_Target
{
    return gTex.Sample(gSmp, i.uv);
}
)";

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
	if (DX_ONNX.IsInitialized() == true)
	{
		m_Onnx = std::make_unique<OnnxPassResources>();
		if (m_Onnx == nullptr)
		{
			return false;
		}

		m_OnnxGPU = std::make_unique<OnnxGPUResources>();
		if (m_OnnxGPU == nullptr)
		{
			return false;
		}

	}
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
	InitCubePipeline();
	InitCubeGeometry();
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

	//UINT w = DX_WINDOW.GetWidth(), h = DX_WINDOW.GetHeight();
	InitDepth(w, h);
	mAspect = (float)w / (float)h;

	return true;
}

void DirectXManager::Update()
{
	//DX_CONTEXT.SignalAndWait();
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

	GetRenderingObject1().UploadCPUResource(false, true);

	mAngle += 1.0f * (1.0f / 60.0f); // 1 rad/sec 정도
}

void DirectXManager::RenderCube(ID3D12GraphicsCommandList7* cmd)
{
	auto vp = DX_WINDOW.CreateViewport();
	auto sc = DX_WINDOW.CreateScissorRect();
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	cmd->ClearDepthStencilView(mDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 오프스크린에 그리기(최종 blit로 화면에 나올 수 있게)
	cmd->OMSetRenderTargets(1, &mRtvScene, FALSE, &mDSV);

	cmd->SetPipelineState(mCubePSO);
	cmd->SetGraphicsRootSignature(mCubeRootSig);

	// === SRV 힙 + t0 바인딩 ===
	ID3D12DescriptorHeap* srvHeap = DX_IMAGE.GetSrvheap();
	cmd->SetDescriptorHeaps(1, &srvHeap);
	// RS param[1] == t0 테이블
	cmd->SetGraphicsRootDescriptorTable(1, DX_IMAGE.GetGPUDescriptorHandle(m_StyleObject.GetTestIndex())); // index=2

	// IA
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &mVBV);
	cmd->IASetIndexBuffer(&mIBV);

	// MVP
	XMMATRIX world = XMMatrixRotationY(mAngle) * XMMatrixRotationX(mAngle * 0.5f);
	XMVECTOR eye = XMVectorSet(0, 0, -3, 0), at = XMVectorSet(0, 0, 0, 0), up = XMVectorSet(0, 1, 0, 0);
	XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, mAspect, 0.1f, 100.0f);
	XMMATRIX mvp = XMMatrixTranspose(world * view * proj);
	cmd->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);

	cmd->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

}

void DirectXManager::RenderImage(ID3D12GraphicsCommandList7* cmd)
{
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

}

void DirectXManager::Shutdown()
{
	DestroyOffscreen();

	m_RootSignature.Release();
	m_PipelineStateObj.Release();

	if (m_OnnxGPU != nullptr)
	{
		m_OnnxGPU->Reset();
		m_OnnxGPU.release();
		m_OnnxGPU = nullptr;
	}

	if (m_Onnx != nullptr)
	{
		m_Onnx.release();
		m_Onnx = nullptr;
	}

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
	if (DX_ONNX.IsInitialized() == false)
	{
		return;
	}

	switch (DX_ONNX.GetOnnxType())
	{
	case OnnxType::Udnie:
	{
		OnnxService::CreateOnnxResources_Udnie(
			W, H,
			*m_StyleObject.GetImage().get(),
			m_Onnx.get(),
			m_OnnxGPU.get(),
			mHeapCPU.Get(),
			mSceneColor.Get(),
			mOnnxTexState,
			mOnnxInputState
			);
		break;
	}
	case OnnxType::AdaIN:
	{
		OnnxService::CreateOnnxResources_AdaIN(
			W, H,
			*m_StyleObject.GetImage().get(),
			m_Onnx.get(),
			m_OnnxGPU.get(),
			mHeapCPU.Get(),
			mSceneColor.Get(),
			mOnnxTexState,
			mOnnxInputState);
	}
	break;

	case OnnxType::FastNeuralStyle:
	{
		OnnxService::CreateOnnxResources_FastNeuralStyle(
			W, H, 
			*m_StyleObject.GetImage().get(), 
			m_Onnx.get(), 
			m_OnnxGPU.get(), 
			mHeapCPU.Get(), 
			mSceneColor.Get(),
			mOnnxTexState,
			mOnnxInputState);
	}
	break;
	default:
		break;
	}

	
}

void DirectXManager::RecordPreprocess(ID3D12GraphicsCommandList7* cmd)
{
	switch (DX_ONNX.GetOnnxType())
	{
		case OnnxType::Udnie:
		{
			OnnxService::RecordPreprocess_Udnie(
				cmd, 
				m_OnnxGPU->Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(),
				mOnnxInputState
				);
		}
		break;

		case OnnxType::AdaIN:
		{
			OnnxService::RecordPreprocess_AdaIN(
				cmd, 
				m_OnnxGPU->Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(), 
				mSceneColor.Get(),
				*m_StyleObject.GetImage().get());
		}
		break;

		case OnnxType::FastNeuralStyle:
		{
			OnnxService::RecordPreprocess_FastNeuralStyle(
				cmd, 
				m_OnnxGPU->Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(), 
				mSceneColor.Get(),
				*m_StyleObject.GetImage().get());
		}
		break;
	}
}

void DirectXManager::RecordPostprocess(ID3D12GraphicsCommandList7* cmd)
{
	switch (DX_ONNX.GetOnnxType())
	{
		case OnnxType::Udnie:
		{
			OnnxService::RecordPostprocess_Udnie(cmd, m_OnnxGPU->Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), mOnnxTexState);
		}
		break;
		case OnnxType::AdaIN:
		{
			OnnxService::RecordPostprocess_AdaIN(cmd, m_OnnxGPU->Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), mOnnxTexState);
		}
		break;

		case OnnxType::FastNeuralStyle:
		{
			OnnxService::RecordPostprocess_FastNeuralStyle(cmd, m_OnnxGPU->Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), mOnnxTexState);
		}
		break;
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

	cmd->DrawInstanced(3, 1, 0, 0);
}

void DirectXManager::DrawConstantGreen_Standalone()
{
	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();

	// 1) 백버퍼 전이 PRESENT -> RENDER_TARGET
	ID3D12Resource* back = DX_WINDOW.GetBackbuffer();
	auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
		back,
		D3D12_RESOURCE_STATE_PRESENT,           
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

bool DirectXManager::InitCubePipeline()
{
	// RS: b0(상수 16개), t0(SRV1), s0(정적 샘플러)
	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
	CD3DX12_ROOT_PARAMETER1 params[2];
	params[0].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);       // b0
	params[1].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL); // t0

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samp.ShaderRegister = 0; // s0

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(_countof(params), params, 1, &samp,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPointer<ID3DBlob> rsBlob, rsErr;
	D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &rsErr);
	DX_CONTEXT.GetDevice()->CreateRootSignature(
		0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&mCubeRootSig));

	// 셰이더(위에서 교체한 g_VS/g_PS 사용)
	ComPointer<ID3DBlob> vs, ps, err;
	D3DCompile(g_VS, strlen(g_VS), nullptr, nullptr, nullptr, "main", "vs_5_1", 0, 0, &vs, &err);
	D3DCompile(g_PS, strlen(g_PS), nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0, &ps, &err);

	// 입력 레이아웃: POSITION + TEXCOORD
	D3D12_INPUT_ELEMENT_DESC il[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, sizeof(float) * 3,      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = mCubeRootSig;
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 와인딩 신경 안쓰게 안전하게
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.SampleMask = UINT_MAX;
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // 최종 백버퍼(blit 전용이면 상관없음)
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc = { 1,0 };
	pso.InputLayout = { il, _countof(il) };

	return SUCCEEDED(DX_CONTEXT.GetDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mCubePSO)));

}

bool DirectXManager::InitCubeGeometry()
{
	struct CubeVtx { XMFLOAT3 pos; XMFLOAT2 uv; };
	const float c = 0.5f;

	// 각 면별 4버텍스(정석 매핑)
	CubeVtx v[] = {
		// +Z (front)
		{{-c,-c, c},{0,1}}, {{-c, c, c},{0,0}}, {{ c, c, c},{1,0}}, {{ c,-c, c},{1,1}},
		// -Z (back)
		{{ c,-c,-c},{0,1}}, {{ c, c,-c},{0,0}}, {{-c, c,-c},{1,0}}, {{-c,-c,-c},{1,1}},
		// +X (right)
		{{ c,-c, c},{0,1}}, {{ c, c, c},{0,0}}, {{ c, c,-c},{1,0}}, {{ c,-c,-c},{1,1}},
		// -X (left)
		{{-c,-c,-c},{0,1}}, {{-c, c,-c},{0,0}}, {{-c, c, c},{1,0}}, {{-c,-c, c},{1,1}},
		// +Y (top)
		{{-c, c, c},{0,1}}, {{-c, c,-c},{0,0}}, {{ c, c,-c},{1,0}}, {{ c, c, c},{1,1}},
		// -Y (bottom)
		{{-c,-c,-c},{0,1}}, {{-c,-c, c},{0,0}}, {{ c,-c, c},{1,0}}, {{ c,-c,-c},{1,1}},
	};
	uint16_t idx[] = {
		0,1,2, 0,2,3,
		4,5,6, 4,6,7,
		8,9,10, 8,10,11,
		12,13,14, 12,14,15,
		16,17,18, 16,18,19,
		20,21,22, 20,22,23,
	};
	mIndexCount = (UINT)_countof(idx);

	// 업로드 → DEFAULT 복사 (기존 코드 재사용)
	auto dev = DX_CONTEXT.GetDevice();

	size_t vbSize = sizeof(v);
	CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT), hpUp(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC   rdVB = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

	ComPointer<ID3D12Resource> uploadVB;
	dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdVB,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mVB));
	dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &rdVB,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadVB));

	size_t ibSize = sizeof(idx);
	CD3DX12_RESOURCE_DESC rdIB = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
	ComPointer<ID3D12Resource> uploadIB;
	dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdIB,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mIB));
	dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &rdIB,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadIB));

	void* p = nullptr;
	uploadVB->Map(0, nullptr, &p); memcpy(p, v, vbSize); uploadVB->Unmap(0, nullptr);
	uploadIB->Map(0, nullptr, &p); memcpy(p, idx, ibSize); uploadIB->Unmap(0, nullptr);

	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
	cmd->CopyResource(mVB, uploadVB);
	cmd->CopyResource(mIB, uploadIB);

	auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(mVB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(mIB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
	cmd->ResourceBarrier(1, &b1);
	cmd->ResourceBarrier(1, &b2);

	DX_CONTEXT.ExecuteCommandList();

	mVBV.BufferLocation = mVB->GetGPUVirtualAddress();
	mVBV.StrideInBytes = sizeof(CubeVtx);
	mVBV.SizeInBytes = (UINT)vbSize;

	mIBV.BufferLocation = mIB->GetGPUVirtualAddress();
	mIBV.Format = DXGI_FORMAT_R16_UINT;
	mIBV.SizeInBytes = (UINT)ibSize;

	return true;
}

bool DirectXManager::InitDepth(UINT w, UINT h)
{
	auto dev = DX_CONTEXT.GetDevice();

	if (!mDsvHeap) {
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		d.NumDescriptors = 1;
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&mDsvHeap)))) return false;
		mDSV = mDsvHeap->GetCPUDescriptorHandleForHeapStart();
	}

	// 리소스 생성
	D3D12_CLEAR_VALUE optClear{};
	optClear.Format = DXGI_FORMAT_D32_FLOAT;
	optClear.DepthStencil = { 1.0f, 0 };

	CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, w, h, 1, 1);
	rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	DestroyDepth();
	if (FAILED(dev->CreateCommittedResource(
		&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &optClear, IID_PPV_ARGS(&mDepth)))) return false;

	// DSV 만들기
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dev->CreateDepthStencilView(mDepth, &dsv, mDSV);

	return true;
}

void DirectXManager::DestroyDepth()
{
	if (mDepth) { mDepth.Release(); }
}

void DirectXManager::ResizeOnnxResources(UINT W, UINT H)
{
	if (m_Onnx != nullptr && m_OnnxGPU != nullptr)
	{
		if (W == m_Onnx->Width && H == m_Onnx->Height) return;
		m_OnnxGPU->Reset();

		ID3D12Resource* styleTex = m_StyleObject.GetImage()->GetTexture();
		auto sDesc = styleTex->GetDesc();
		UINT styleW = (UINT)sDesc.Width;
		UINT styleH = sDesc.Height;

		DX_ONNX.ResizeIO(DX_CONTEXT.GetDevice(), W, H, styleW, styleH);

		CreateOnnxResources(W, H);
		m_Onnx->Width = W; m_Onnx->Height = H;
		return;
	}
	
	ID3D12Resource* styleTex = m_StyleObject.GetImage()->GetTexture();
	auto sDesc = styleTex->GetDesc();
	UINT styleW = (UINT)sDesc.Width;
	UINT styleH = sDesc.Height;

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
		IID_PPV_ARGS(&m_Onnx->PreRS));
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
	preDesc.pRootSignature = m_Onnx->PreRS.Get();
	preDesc.CS = { csPre->GetBufferPointer(), csPre->GetBufferSize() };
	hr = device->CreateComputePipelineState(&preDesc, IID_PPV_ARGS(&m_Onnx->PrePSO));
	if (FAILED(hr)) return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC postDesc{};
	postDesc.pRootSignature = m_Onnx->PreRS.Get(); // 같은 RS를 재사용
	postDesc.CS = { csPost->GetBufferPointer(), csPost->GetBufferSize() };
	hr = device->CreateComputePipelineState(&postDesc, IID_PPV_ARGS(&m_Onnx->PostPSO));
	if (FAILED(hr)) return false;

	//{
	//	ComPointer<ID3DBlob> csDbg, err;
	//	HRESULT hr = D3DCompileFromFile(L"./Shaders/cs_debug_show_input.hlsl", nullptr, nullptr,
	//		"main", "cs_5_0", 0, 0, &csDbg, &err);
	//	if (SUCCEEDED(hr)) {
	//		D3D12_COMPUTE_PIPELINE_STATE_DESC dbg{};
	//		dbg.pRootSignature = m_Onnx->PreRS.Get(); // t0/u0/b0 동일 RS 재사용
	//		dbg.CS = { csDbg->GetBufferPointer(), csDbg->GetBufferSize() };
	//		DX_CONTEXT.GetDevice()->CreateComputePipelineState(&dbg, IID_PPV_ARGS(&m_DebugShowInputPSO));
	//	}
	//}

	//{
	//	ComPointer<ID3DBlob> csDbg, err;
	//	HRESULT hr = D3DCompileFromFile(L"./Shaders/cs_copy_tex_to_tex2d.hlsl", nullptr, nullptr,
	//		"main", "cs_5_0", 0, 0, &csDbg, &err);
	//	if (SUCCEEDED(hr)) {
	//		D3D12_COMPUTE_PIPELINE_STATE_DESC dbg{};
	//		dbg.pRootSignature = m_Onnx->PreRS.Get(); // t0/u0/b0 동일 RS 재사용
	//		dbg.CS = { csDbg->GetBufferPointer(), csDbg->GetBufferSize() };
	//		DX_CONTEXT.GetDevice()->CreateComputePipelineState(&dbg, IID_PPV_ARGS(&m_CopyTexToTex2DPSO));
	//	}
	//}

	//{
	//	ComPointer<ID3DBlob> csDbg, err;
	//	HRESULT hr = D3DCompileFromFile(L"./Shaders/cs_fill_buffer.hlsl", nullptr, nullptr,
	//		"main", "cs_5_0", 0, 0, &csDbg, &err);
	//	if (SUCCEEDED(hr)) {
	//		D3D12_COMPUTE_PIPELINE_STATE_DESC dbg{};
	//		dbg.pRootSignature = m_Onnx->PreRS.Get(); // t0/u0/b0 동일 RS 재사용
	//		dbg.CS = { csDbg->GetBufferPointer(), csDbg->GetBufferSize() };
	//		DX_CONTEXT.GetDevice()->CreateComputePipelineState(&dbg, IID_PPV_ARGS(&m_FillPSO));
	//	}

	//}

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

	mAspect = (h == 0) ? mAspect : (float)w / (float)h;
	InitDepth(w, h);
}

void DirectXManager::RenderOffscreen(ID3D12GraphicsCommandList7* cmd)
{
	// SceneColor: ? -> RTV
	if (mSceneColorState != D3D12_RESOURCE_STATE_RENDER_TARGET) 
	{
		CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
			mSceneColor.Get(), mSceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1, &b);
		mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	const FLOAT clear[4] = { 0,0,0,1 };
	cmd->OMSetRenderTargets(1, &mRtvScene, FALSE, nullptr);
	cmd->ClearRenderTargetView(mRtvScene, clear, 0, nullptr);

	UploadGPUResource(cmd);

	RenderImage(cmd);
	RenderCube(cmd);

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
			m_OnnxGPU->OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}

	// 뷰포트/시저
	UINT bw = 0, bh = 0; DX_WINDOW.GetBackbufferSize(bw, bh);
	D3D12_VIEWPORT vp{ 0,0,(float)bw,(float)bh,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)bw,(LONG)bh };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	// RTV 바인딩
	auto rtv = DX_WINDOW.GetRtvHandle(DX_WINDOW.GetBackBufferIndex());
	cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	// 파이프라인/RS
	cmd->SetPipelineState(m_BlitPSO2.Get());
	cmd->SetGraphicsRootSignature(m_BlitRS2.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// SRV 힙 바인딩 (t0 = OnnxTex)
	ID3D12DescriptorHeap* heaps[] = { m_OnnxGPU->Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootDescriptorTable(0, m_OnnxGPU->OnnxTexSRV_GPU);

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

	mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	return true;
}

void DirectXManager::DestroyOffscreen()
{
	mSceneColor.Release();
	mOffscreenRtvHeap.Release();
	m_BlitSrvHeap.Release();

	mRtvScene = {};
	mResolvedSrvCPU = {};
	mResolvedSrvGPU = {};

	mSceneColorState = D3D12_RESOURCE_STATE_COMMON;
}

