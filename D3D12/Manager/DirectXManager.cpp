#include "DirectXManager.h"

#include "OnnxManager.h"
#include "ImageManager.h"
#include "InputManager.h"

#include "Support/Window.h"
#include "Support/Onnx/OnnxService.h"
#include "Support/Shader.h"

#include "D3D/DXContext.h"
#include <wrl.h>
#include <vector>
#include <assert.h>
#include <iostream>
#include <numbers> 

#define MOVE_SPEED 10



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


static inline DirectX::XMFLOAT3 DirFromYawPitchLH(float yaw, float pitch)
{
	float cy = cosf(yaw), sy = sinf(yaw);
	float cp = cosf(pitch), sp = sinf(pitch);
	XMFLOAT3 d{ cp * sy, sp, cp * cy };
	
	XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&d));
	XMStoreFloat3(&d, v);
	return d;
}

static inline DirectX::XMMATRIX MakeVP_Dir(const Camera& cam, float aspect)
{
	XMVECTOR eye = XMLoadFloat3(&cam.pos);
	XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&cam.dir));
	XMVECTOR up = XMVector3Normalize(XMLoadFloat3(&cam.up));

	XMMATRIX V = XMMatrixLookToLH(eye, dir, up); 
	XMMATRIX P = XMMatrixPerspectiveFovLH(cam.fovY, aspect, cam.nearZ, cam.farZ);
	return XMMatrixMultiply(V, P);
}

static void MoveCamera_FPS(Camera& c, float forward, float right, float upMove, float dt, float speed)
{
	// dir/up로 right 벡터 도출
	XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&c.dir));
	XMVECTOR up = XMVectorSet(0, 1, 0, 0); // 월드업 고정(롤 없음)
	XMVECTOR rightV = XMVector3Normalize(XMVector3Cross(up, dir)); // RH? LH? 방향만 맞으면 OK

	XMVECTOR pos = XMLoadFloat3(&c.pos);
	pos = XMVectorAdd(pos, XMVectorScale(dir, forward * speed * dt));
	pos = XMVectorAdd(pos, XMVectorScale(rightV, right * speed * dt));
	pos = XMVectorAdd(pos, XMVectorScale(up, upMove * speed * dt));
	XMStoreFloat3(&c.pos, pos);
}

static void RotateCameraYawPitch(Camera& c, float dYaw, float dPitch)
{
	c.yaw += dYaw;

	float min = (XM_PIDIV2 * 0.99f < c.pitch + dPitch ? XM_PIDIV2 * 0.99f : c.pitch + dPitch);
	c.pitch = -XM_PIDIV2 * 0.99f > min ? -XM_PIDIV2 * 0.99f : min;
	c.dir = DirFromYawPitchLH(c.yaw, c.pitch);
}


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
	SetVertexLayout();

	UINT w, h;
	DX_WINDOW.GetBackbufferSize(w, h);
	CreateOffscreen(w, h);

	auto device = DX_CONTEXT.GetDevice();

	InitShader();
	InitBlitPipeline();
	CreateSimpleBlitPipeline(); // 새 전용 블릿 PSO 추가
	InitCubePipeline();
	//InitGroundGeometry();
	//InitCubeGeometry();
	CreateGreenPipeline();
	CreateFullscreenQuadVB(w, h);

	SetVerticies();
	InitUploadRenderingObject();
	InitGeometry();

	// ★ ONNX IO 준비 + 디스크립터 구성
	CreateOnnxResources(w, h);
	CreateDebugFillOnnxTexPSO();
	CreateDebugViewPreCHWPSO();


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

	mCam.pos = { 0, 2.0f, -6.0f };

	DX_WINDOW.GetDelegate().AddDelegate(this, &DirectXManager::OnChangedMouseLock);

	return true;
}

void DirectXManager::Update(float deltaTime)
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

	//MoveAxis consumeAxis = DX_INPUT.Consume();



	float moveAxis_X = 0.f;
	moveAxis_X += DX_INPUT.isStateKeyDown('D') == true ? 1.f : 0.f;
	moveAxis_X -= DX_INPUT.isStateKeyDown('A') == true ? 1.f : 0.f;

	float moveAxis_Y = 0.f;
	moveAxis_Y += DX_INPUT.isStateKeyDown('W') == true ? 1.f : 0.f;
	moveAxis_Y -= DX_INPUT.isStateKeyDown('S') == true ? 1.f : 0.f;

	if (DX_WINDOW.IsMouseLock() == true)
	{
		const POINT& deltaPos = DX_WINDOW.GetMouseMove();

		RotateCameraYawPitch(mCam, deltaPos.x * deltaTime * (0.01f), deltaPos.y * deltaTime * (-0.01f));
	}

	MoveCamera_FPS(mCam, moveAxis_Y, moveAxis_X, 0.f, deltaTime, MOVE_SPEED);
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
	DX_WINDOW.GetDelegate().RemoveDelegate(this);

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
	m_PlaneObject.UploadGPUResource(cmdList);
	m_CubeObject.UploadGPUResource(cmdList);
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

	case OnnxType::WCT2:
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

	case OnnxType::MsgNet:
	case OnnxType::ReCoNet:
	{
		OnnxService::CreateOnnxResources_ReCoNet(
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

	case OnnxType::BlindVideo:
	{
		OnnxService::CreateOnnxResources_BlindVideo(
			W, H,
			*m_StyleObject.GetImage().get(),
			m_Onnx.get(),
			m_OnnxGPU.get(),
			mHeapCPU.Get(),
			mSceneColor.Get(),
			mOnnxTexState,
			mOnnxInputState);

		// ★ Pt 텍스처(이전 프레임 결과)도 같은 크기로 보장
		RecreatePrevStylizedIfNeeded(W, H);
		mHasPrevStylized = false; // 새로 만들었으니 첫 프레임은 It로 대체

	}
	break;

	case OnnxType::Sanet:
	{
		OnnxService::CreateOnnxResources_Sanet(
			W, H,
			*m_StyleObject.GetImage().get(),
			m_Onnx.get(),
			m_OnnxGPU.get(),
			mHeapCPU.Get(),
			mSceneColor.Get(),
			mOnnxTexState,
			mOnnxInputState);

		// ★ Pt 텍스처(이전 프레임 결과)도 같은 크기로 보장
		RecreatePrevStylizedIfNeeded(W, H);
		mHasPrevStylized = false; // 새로 만들었으니 첫 프레임은 It로 대체

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

		case OnnxType::WCT2:
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

		case OnnxType::MsgNet:
		case OnnxType::ReCoNet:
		{
			OnnxService::RecordPreprocess_ReCoNet(
				cmd,
				m_OnnxGPU->Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(),
				mSceneColor.Get(),
				*m_StyleObject.GetImage().get());

		}
		break;

		case OnnxType::BlindVideo:
		{
			ID3D12Resource2* pt = (mHasPrevStylized && mPrevStylized) ? mPrevStylized.Get() : mSceneColor.Get();

			OnnxService::RecordPreprocess_BlindVideo(
				cmd,
				m_OnnxGPU->Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(),
				mSceneColor.Get(),   // It (현재 프레임)
				pt                   // Pt (이전 프레임 결과, 없으면 It)
			);
		}
		break;

		case OnnxType::Sanet:
		{
			OnnxService::RecordPreprocess_Sanet(
				cmd,
				m_OnnxGPU->Heap.Get(),
				*m_StyleObject.GetImage().get(),
				m_Onnx.get(),
				m_OnnxGPU.get(),
				mSceneColor.Get()
			);
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

		case OnnxType::WCT2:
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

		case OnnxType::MsgNet:
		case OnnxType::ReCoNet:
		{
			OnnxService::RecordPostprocess_ReCoNet(cmd, m_OnnxGPU->Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), mOnnxTexState);
		}
		break;

		case OnnxType::BlindVideo:
		{
			OnnxService::RecordPostprocess_BlindVideo(
				cmd, m_OnnxGPU->Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), mOnnxTexState);

			ID3D12Resource2* src = m_OnnxGPU->OnnxTex.Get();
			if (src && mPrevStylized)
			{
				// ★ compute→copy 가시성 보장
				{
					auto uav = CD3DX12_RESOURCE_BARRIER::UAV(src);
					cmd->ResourceBarrier(1, &uav);
				}

				if (mOnnxTexState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
					auto b = CD3DX12_RESOURCE_BARRIER::Transition(
						src, mOnnxTexState, D3D12_RESOURCE_STATE_COPY_SOURCE);
					cmd->ResourceBarrier(1, &b);
					mOnnxTexState = D3D12_RESOURCE_STATE_COPY_SOURCE;
				}

				if (mPrevStylizedState != D3D12_RESOURCE_STATE_COPY_DEST) {
					auto b = CD3DX12_RESOURCE_BARRIER::Transition(
						mPrevStylized.Get(), mPrevStylizedState, D3D12_RESOURCE_STATE_COPY_DEST);
					cmd->ResourceBarrier(1, &b);
					mPrevStylizedState = D3D12_RESOURCE_STATE_COPY_DEST;
				}

				cmd->CopyResource(mPrevStylized.Get(), src);

				{
					auto b = CD3DX12_RESOURCE_BARRIER::Transition(
						mPrevStylized.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					cmd->ResourceBarrier(1, &b);
					mPrevStylizedState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				}

				// 화면 블릿을 위해 다시 PSR로
				{
					auto b = CD3DX12_RESOURCE_BARRIER::Transition(
						src, D3D12_RESOURCE_STATE_COPY_SOURCE,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					cmd->ResourceBarrier(1, &b);
					mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				}

				mHasPrevStylized = true;
			}
		}
		break;

		case OnnxType::Sanet:
		{
			OnnxService::RecordPostprocess_Sanet(cmd, m_OnnxGPU->Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), mOnnxTexState);
		}
		break;
	}
}

void DirectXManager::OnChangedMouseLock()
{
}


void DirectXManager::CreateFullscreenQuadVB(UINT w, UINT h)
{
	struct Vtx2 { float x, y, u, v; };
	Vtx2 quad[6] = {
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
	mFSQuadVBV.StrideInBytes = sizeof(Vtx2);
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
	for (int i = 0; i < k; ++i) 
	{ 
		char t[64]; 
		sprintf_s(t, " %.5f", f[i]); 
		first += t; 
	}
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

void DirectXManager::Debug_FillOnnxTex(ID3D12GraphicsCommandList7* cmd)
{
	if (!m_Onnx || !m_OnnxGPU) return;

	// OnnxTex → UAV 전이
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			m_OnnxGPU->OnnxTex.Get(),
			mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// 디스크립터/RS/PSO 바인딩
	ID3D12DescriptorHeap* heaps[] = { m_OnnxGPU->Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(m_Onnx->PreRS.Get());         // 기존 RS 재사용
	cmd->SetPipelineState(m_DebugFillOnnxTexPSO.Get());        // 이전에 만든 PSO

	// RS 슬롯: t-range(0)는 더미/아무거나, u-range(1)는 OnnxTex UAV
	cmd->SetComputeRootDescriptorTable(0, m_OnnxGPU->DummySRV_GPU);
	cmd->SetComputeRootDescriptorTable(1, m_OnnxGPU->OnnxTexUAV_GPU);
	cmd->SetComputeRootConstantBufferView(2, m_OnnxGPU->CB->GetGPUVirtualAddress()); // 사용 안 해도 바인딩

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((m_Onnx->Width + TGX - 1) / TGX,
		(m_Onnx->Height + TGY - 1) / TGY, 1);

	// UAV 가시성 + PSR로 전이(블릿용)
	auto uav = CD3DX12_RESOURCE_BARRIER::UAV(m_OnnxGPU->OnnxTex.Get());
	cmd->ResourceBarrier(1, &uav);

	auto toPS = CD3DX12_RESOURCE_BARRIER::Transition(
		m_OnnxGPU->OnnxTex.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toPS);
	mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void DirectXManager::Debug_ViewPreprocessCHW(ID3D12GraphicsCommandList7* cmd)
{
	if (!m_Onnx || !m_OnnxGPU) return;
	ID3D12Resource* inBuf = DX_ONNX.GetInputBufferContent().Get();
	if (!inBuf) return;

	// (A) 실제 채널 수 계산 ( = bytes / (W*H*sizeof(float)) )
	const UINT W = m_Onnx->Width, H = m_Onnx->Height;
	const UINT64 bytes = inBuf->GetDesc().Width;
	UINT realC = 0;
	if (W > 0 && H > 0) realC = (UINT)(bytes / (UINT64(sizeof(float)) * W * H));
	realC = std::min<UINT>(realC, 6u);       // 안전장치

	// (B) SRV 갱신 (정확한 NumElements 사용)
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	s.Format = DXGI_FORMAT_UNKNOWN;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.Buffer.FirstElement = 0;
	s.Buffer.NumElements = realC * W * H;    // ★ 실제 채널 수 반영
	s.Buffer.StructureByteStride = sizeof(float);
	s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	DX_CONTEXT.GetDevice()->CreateShaderResourceView(
		inBuf, &s, m_OnnxGPU->ModelOutSRV_CPU);

	// (C) InputBuffer: UAV→SRV (읽기)
	auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
		inBuf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toSRV);

	// (D) OnnxTex 클리어(선택) + UAV로 전이
	//     남은 영역에 이전 프레임이 남지 않게!
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			m_OnnxGPU->OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	// 간단 클리어(옵션): 0으로
	// -> 필요하면 별도 ClearUAV(Structured) 도입. 여기선 스킵 가능.

	// (E) CB 세팅 (타일 자동)
	struct CBData {
		UINT SrcW, SrcH, SrcC, Flags;
		UINT DstW, DstH, TilesX, TilesY;   // ★ 변경
		float Gain, Bias, _f0, _f1;
	} cb{};
	cb.SrcW = W; cb.SrcH = H; cb.SrcC = realC;
	cb.DstW = W; cb.DstH = H;
	cb.TilesX = (realC <= 3) ? 3 : 3;
	cb.TilesY = (realC <= 3) ? 1 : 2;      // ★ 3채널이면 3x1, 6채널이면 3x2
	cb.Gain = 1.0f; cb.Bias = 0.0f;        // [-1,1]이면 0.5/0.5로

	void* p = nullptr;
	m_OnnxGPU->CB->Map(0, nullptr, &p);
	std::memcpy(p, &cb, sizeof(cb));
	m_OnnxGPU->CB->Unmap(0, nullptr);

	// (F) 바인딩 & 디스패치
	ID3D12DescriptorHeap* heaps[] = { m_OnnxGPU->Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(m_Onnx->PreRS.Get());
	cmd->SetPipelineState(m_DebugViewPreCHWPSO.Get());
	cmd->SetComputeRootDescriptorTable(0, m_OnnxGPU->ModelOutSRV_GPU);  // t0..(t1 dummy)
	cmd->SetComputeRootDescriptorTable(1, m_OnnxGPU->OnnxTexUAV_GPU);   // u0
	cmd->SetComputeRootConstantBufferView(2, m_OnnxGPU->CB->GetGPUVirtualAddress());

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((W + TGX - 1) / TGX, (H + TGY - 1) / TGY, 1);

	// (G) UAV 가시성 + OnnxTex는 PSR로 (블릿)
	auto uav = CD3DX12_RESOURCE_BARRIER::UAV(m_OnnxGPU->OnnxTex.Get());
	cmd->ResourceBarrier(1, &uav);
	auto toPS = CD3DX12_RESOURCE_BARRIER::Transition(
		m_OnnxGPU->OnnxTex.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toPS);
	mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	// (H) ★★ 중요: InputBuffer 상태 원복 (다음 프레임 전처리용)
	auto backUAV = CD3DX12_RESOURCE_BARRIER::Transition(
		inBuf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmd->ResourceBarrier(1, &backUAV);
}

bool DirectXManager::CreateDebugFillOnnxTexPSO()
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	if (!dev) return false;

	// BlindVideo용 PreRS( SRV table, UAV table, CBV ) 이미 있음
	if (!m_Onnx || !m_Onnx->PreRS) return false;

	ComPointer<ID3DBlob> cs, err;
	HRESULT hr = D3DCompileFromFile(
		L"./Shaders/cs_debug_fill_onnxtex.hlsl",
		nullptr, nullptr, "main", "cs_5_0",
		0, 0, &cs, &err);

	if (FAILED(hr) || !cs)
	{
		if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = m_Onnx->PreRS.Get(); // UAV(u0) 있는 RS 재사용
	desc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

	hr = dev->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_DebugFillOnnxTexPSO));
	if (FAILED(hr)) return false;

	return true;
}

bool DirectXManager::CreateDebugViewPreCHWPSO()
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	if (!m_Onnx || !m_Onnx->PreRS) return false; // PreRS 재사용 (t-range, u-range, CBV)

	ComPointer<ID3DBlob> cs, err;
	HRESULT hr = D3DCompileFromFile(
		L"./Shaders/cs_debug_view_pre_chw.hlsl",
		nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs, &err);
	if (FAILED(hr) || !cs) {
		if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
		return false;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
	d.pRootSignature = m_Onnx->PreRS.Get();
	d.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

	return SUCCEEDED(dev->CreateComputePipelineState(&d, IID_PPV_ARGS(&m_DebugViewPreCHWPSO)));
}

void DirectXManager::RecreatePrevStylizedIfNeeded(UINT W, UINT H)
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	bool need = true;
	if (mPrevStylized) {
		const auto d = mPrevStylized->GetDesc();
		// 크기나 포맷이 다르면 재생성
		need = (d.Width != W || d.Height != H || d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT);
	}
	if (!need) return;

	// 기존 리소스 정리
	mPrevStylized.Release();
	mPrevStylizedState = D3D12_RESOURCE_STATE_COMMON;
	mHasPrevStylized = false;

	// 새 리소스 생성 (전처리에서 SRV로 읽을 거라 NPSR로 시작)
	D3D12_RESOURCE_DESC td{};
	td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = W;
	td.Height = H;
	td.DepthOrArraySize = 1;
	td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	td.SampleDesc = { 1, 0 };
	td.Flags = D3D12_RESOURCE_FLAG_NONE; // SRV만 필요

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	dev->CreateCommittedResource(
		&hp, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, // 전처리에서 읽기
		nullptr, IID_PPV_ARGS(&mPrevStylized));

	mPrevStylizedState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

void DirectXManager::Debug_PostprocessOnly(ID3D12GraphicsCommandList7* cmd)
{
	if (!m_Onnx || !m_OnnxGPU) return;

	// 디버그 CHW(=모델 출력 모사) 준비: srcW/H는 편의상 출력 해상도와 동일
	const UINT srcW = m_Onnx->Width;
	const UINT srcH = m_Onnx->Height;
	EnsureDebugModelCHW(srcW, srcH); // ModelOutSRV(슬롯4)도 여기서 갱신됨

	// OnnxTex → UAV 전이 (후처리 u0로 씀)
	if (mOnnxTexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			m_OnnxGPU->OnnxTex.Get(),
			mOnnxTexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmd->ResourceBarrier(1, &b);
		mOnnxTexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// CB 채우기 (후처리 HLSL과 동일한 레이아웃)
	struct CBData {
		UINT SrcW, SrcH, SrcC, Flags;
		UINT DstW, DstH, _r1, _r2;
		float Gain, Bias, _f0, _f1;
	} cb{ srcW, srcH, 3, 0, m_Onnx->Width, m_Onnx->Height, 0,0, 1.0f, 0.0f, 0,0 };

	void* p = nullptr;
	m_OnnxGPU->CB->Map(0, nullptr, &p);
	std::memcpy(p, &cb, sizeof(cb));
	m_OnnxGPU->CB->Unmap(0, nullptr);

	// 바인딩 & 디스패치 (후처리 PSO 사용)
	ID3D12DescriptorHeap* heaps[] = { m_OnnxGPU->Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetComputeRootSignature(m_Onnx->PreRS.Get());
	cmd->SetPipelineState(m_Onnx->PostPSO.Get());

	// SRV range base = ModelOutSRV (슬롯4). 바로 뒤 슬롯5에 DummySRV가 붙어 있음.
	cmd->SetComputeRootDescriptorTable(0, m_OnnxGPU->ModelOutSRV_GPU); // t0..(t1 dummy)
	cmd->SetComputeRootDescriptorTable(1, m_OnnxGPU->OnnxTexUAV_GPU);  // u0
	cmd->SetComputeRootConstantBufferView(2, m_OnnxGPU->CB->GetGPUVirtualAddress());

	const UINT TGX = 8, TGY = 8;
	cmd->Dispatch((m_Onnx->Width + TGX - 1) / TGX,
		(m_Onnx->Height + TGY - 1) / TGY, 1);

	// UAV 가시성 + PSR 전이 (블릿용)
	auto uav = CD3DX12_RESOURCE_BARRIER::UAV(m_OnnxGPU->OnnxTex.Get());
	cmd->ResourceBarrier(1, &uav);

	auto toPS = CD3DX12_RESOURCE_BARRIER::Transition(
		m_OnnxGPU->OnnxTex.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toPS);
	mOnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void DirectXManager::EnsureDebugModelCHW(UINT w, UINT h)
{
	if (mDebugModel && mDebugSrcW == w && mDebugSrcH == h) return;

	ID3D12Device* dev = DX_CONTEXT.GetDevice();
	const size_t plane = size_t(w) * size_t(h);
	const size_t num = plane * 3;          // C=3
	const size_t bytes = num * sizeof(float);

	// 1) DEFAULT 버퍼(Shader SRV로 읽힐 실제 소스)
	CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT);
	auto rd = CD3DX12_RESOURCE_DESC::Buffer(bytes);
	(dev->CreateCommittedResource(
		&hpDef, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(&mDebugModel)));

	// 2) UPLOAD 버퍼(채워서 복사)
	ComPointer<ID3D12Resource> upload;
	CD3DX12_HEAP_PROPERTIES hpUp(D3D12_HEAP_TYPE_UPLOAD);
	(dev->CreateCommittedResource(
		&hpUp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&upload)));

	// 3) CHW 데이터 채우기 (R=u, G=v, B=(u+v)/2)
	{
		void* p = nullptr;
		D3D12_RANGE r{ 0,0 };
		upload->Map(0, &r, &p);
		float* f = reinterpret_cast<float*>(p);

		for (UINT y = 0; y < h; ++y) {
			for (UINT x = 0; x < w; ++x) {
				const float u = (w > 1) ? float(x) / float(w - 1) : 0.0f;
				const float v = (h > 1) ? float(y) / float(h - 1) : 0.0f;
				const size_t idx = size_t(y) * size_t(w) + size_t(x);
				f[0 * plane + idx] = u;               // R
				f[1 * plane + idx] = v;               // G
				f[2 * plane + idx] = 0.5f * (u + v);  // B
			}
		}
		upload->Unmap(0, nullptr);
	}

	// 4) 복사 + 상태 전이(NPSR로)
	{
		ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
		cmd->CopyBufferRegion(mDebugModel.Get(), 0, upload.Get(), 0, bytes);
		auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
			mDebugModel.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &toSRV);
		DX_CONTEXT.ExecuteCommandList();
	}

	mDebugSrcW = w; mDebugSrcH = h;

	// 5) ModelOut SRV(슬롯4) 갱신 (후처리 CS의 t0)
	D3D12_SHADER_RESOURCE_VIEW_DESC s{};
	s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	s.Format = DXGI_FORMAT_UNKNOWN;
	s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	s.Buffer.FirstElement = 0;
	s.Buffer.NumElements = UINT(num);
	s.Buffer.StructureByteStride = sizeof(float);
	s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	dev->CreateShaderResourceView(
		mDebugModel.Get(), &s, m_OnnxGPU->ModelOutSRV_CPU);
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

	// RootSig: SRV range = BlindVideo면 2개(t0=It, t1=Pt), 그 외 1개
	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		2,
		0 // t0 베이스
	);

	// UAV range: u0 하나(전처리 u0=Input, 후처리 u0=OnnxTex)
	CD3DX12_DESCRIPTOR_RANGE1 rangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER1 params[3];
	params[0].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_ALL); // t0..t1
	params[1].InitAsDescriptorTable(1, &rangeUAV, D3D12_SHADER_VISIBILITY_ALL); // u0
	params[2].InitAsConstantBufferView(0);                                      // b0

	D3D12_STATIC_SAMPLER_DESC samp{};
	samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.ShaderRegister = 0; // s0

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(_countof(params), params, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPointer<ID3DBlob> sigBlob, errBlob;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &sigBlob, &errBlob);
	if (FAILED(hr)) return false;

	hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
		IID_PPV_ARGS(&m_Onnx->PreRS));
	if (FAILED(hr)) return false;

	// CS compile
	ComPointer<ID3DBlob> csPre, csPost;
	hr = D3DCompileFromFile(L"./Shaders/cs_preprocess.hlsl", nullptr, nullptr, "main", "cs_5_0", 0, 0, &csPre, &errBlob);
	if (FAILED(hr) || !csPre)  return false;
	hr = D3DCompileFromFile(L"./Shaders/cs_postprocess.hlsl", nullptr, nullptr, "main", "cs_5_0", 0, 0, &csPost, &errBlob);
	if (FAILED(hr) || !csPost) return false;

	// PSO(pre)
	D3D12_COMPUTE_PIPELINE_STATE_DESC preDesc{};
	preDesc.pRootSignature = m_Onnx->PreRS.Get();
	preDesc.CS = { csPre->GetBufferPointer(), csPre->GetBufferSize() };
	hr = device->CreateComputePipelineState(&preDesc, IID_PPV_ARGS(&m_Onnx->PrePSO));
	if (FAILED(hr))
	{
		return false;
	}

	// PSO(post)  같은 RS 재사용
	D3D12_COMPUTE_PIPELINE_STATE_DESC postDesc{};
	postDesc.pRootSignature = m_Onnx->PreRS.Get();
	postDesc.CS = { csPost->GetBufferPointer(), csPost->GetBufferSize() };
	if (FAILED(device->CreateComputePipelineState(&postDesc, IID_PPV_ARGS(&m_Onnx->PostPSO)))) return false;

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

	RecreatePrevStylizedIfNeeded(w, h);
	mHasPrevStylized = false;

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

	auto vp = DX_WINDOW.CreateViewport();
	auto sc = DX_WINDOW.CreateScissorRect();
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->ClearDepthStencilView(mDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	UploadGPUResource(cmd);

	RenderImage(cmd);

	m_PlaneObject.Rendering(cmd, mCam, mAspect, mRtvScene, mDSV, 0, 0.f);
	m_CubeObject.Rendering(cmd, mCam, mAspect, mRtvScene, mDSV, 0, mAngle);


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
	if (mOnnxTexState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			m_OnnxGPU->OnnxTex.Get(), mOnnxTexState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

	// 파이프라인/RS
	cmd->SetPipelineState(m_BlitPSO2.Get());
	cmd->SetGraphicsRootSignature(m_BlitRS2.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// SRV 힙 바인딩 (t0 = OnnxTex)
	ID3D12DescriptorHeap* heaps[] = { m_OnnxGPU->Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootDescriptorTable(0, m_OnnxGPU->OnnxTexSRV_GPU);

	// SV_VertexID 
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

	if (m_PlaneObject.Init("./Resources/Block.png", 3, mCubePSO, mCubeRootSig) == false)
	{
		return;
	}

	if (m_CubeObject.Init("./Resources/Dog.png", 4, mCubePSO, mCubeRootSig) == false)
	{
		return;
	}

}

void DirectXManager::InitGeometry()
{
	{
		// 바닥 평면: XZ에 -50..+50, Y=0 (100m x 100m)
		const float H = 50.0f;
		// 타일링: 10m 당 1타일 -> 전체 10x10 타일
		const float tile = 10.0f;

		Vtx v[4] = {
			{{-H, 0.0f, -H}, {0.0f,         0.0f        }},
			{{-H, 0.0f,  H}, {0.0f,         100.0f / tile }},
			{{ H, 0.0f,  H}, {100.0f / tile,  100.0f / tile }},
			{{ H, 0.0f, -H}, {100.0f / tile,  0.0f        }},
		};
		uint16_t i[6] = { 0,1,2, 0,2,3 };
		UINT index = 6;

		const UINT vbSize = sizeof(v);
		const UINT ibSize = sizeof(i);

		m_PlaneObject.InitGeometry(v, vbSize, i, ibSize, index);
	}

	{
		const float c = 0.5f;

		// 각 면별 4버텍스
		Vtx v[] = {
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

		const UINT vbSize = sizeof(v);
		const UINT ibSize = sizeof(idx);

		m_CubeObject.InitGeometry(v, vbSize, idx, ibSize, (UINT)_countof(idx));
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

