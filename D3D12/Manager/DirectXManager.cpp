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

#define CAM_MOVE_SPEED 0.05f

extern int FrameNum;

extern const char* OBJ_RES_PTH;
extern const char* OBJ_RES_NAME;
extern const int OBJ_RES_NUM;
extern const int MAX_FRAME;

Shader vertexShader("VertexShader.cso");
Shader pixelShader("PixelShader.cso");

struct ShadowCamera {
	DirectX::XMFLOAT3 dir{ 0.5f, -1.0f, 0.3f };
	float yaw = 0.0f, pitch = -0.9f;
	float orthoExtent = 60.0f;
	float zNear = 0.1f, zFar = 150.0f;
};
ShadowCamera mLight;



// g_VS_Shadow
static const char* g_VS_Shadow = R"(
cbuffer CB : register(b0){
  float4x4 uMVP;        // transposed
  float4x4 uLightVP;    // transposed
  float4x4 uWorld;      // transposed
  float4   uLightDir;   // .xyz
};

struct VSIn  { float3 pos:POSITION; float2 uv:TEXCOORD0; float3 nrm:NORMAL; };
struct VSOut {
  float4 pos : SV_Position;
  float2 uv  : TEXCOORD0;
  float4 sh  : TEXCOORD1;  // shadow coords
  float3 N   : TEXCOORD2;  // world normal
};

VSOut main(VSIn i){
  VSOut o;

  float4 wp = mul(float4(i.pos,1), uWorld);

  o.pos = mul(wp, uMVP);
  o.uv  = i.uv;

  float4 lp = mul(wp, uLightVP);
  float3 ndc = lp.xyz / lp.w;
  o.sh = float4(ndc*0.5f + 0.5f, 1.0f);

  float3 n = mul(i.nrm, (float3x3)uWorld);
  o.N = normalize(n);

  return o;
}
)";


// g_PS_ShadowPCF3x3
static const char* g_PS_ShadowPCF3x3 = R"(
Texture2D          gAlbedo : register(t0);
Texture2D<float>   gShadow : register(t1);
SamplerState             gSmp : register(s0);
SamplerComparisonState   gCmp : register(s1);

cbuffer CB : register(b0){
  float4x4 uMVP;
  float4x4 uLightVP;
  float4x4 uWorld;
  float4   uLightDir;   // .xyz
}

struct PSIn {
  float4 pos:SV_Position;
  float2 uv :TEXCOORD0;
  float4 sh :TEXCOORD1;
  float3 N  :TEXCOORD2;
};

float PCF3x3(float2 uv, float refZ, float2 texel)
{
    float a = 0.0f;
    [unroll] for(int y=-1;y<=1;++y){
        [unroll] for(int x=-1;x<=1;++x){
            a += gShadow.SampleCmpLevelZero(gCmp, uv + float2(x,y)*texel, refZ);
        }
    }
    return a / 9.0f;
}

float4 main(PSIn i) : SV_Target
{
    float4 al = gAlbedo.Sample(gSmp, i.uv);

    clip(al.a - 0.001f);

    float3 albedo = al.rgb;

    float2 suv = i.sh.xy;
    float  sdz = saturate(i.sh.z);

    float3 N = normalize(i.N);
    float3 L = normalize(-uLightDir.xyz);
    float  NdotL = saturate(dot(N, L));

    // Hemispheric ambient
    const float3 skyColor    = float3(0.6, 0.7, 1.0);
    const float3 groundColor = float3(0.3, 0.25, 0.2);
    const float  hemiAmt     = 0.3;

    float hemi = 0.5 + 0.5 * N.y;
    float3 ambient = lerp(groundColor, skyColor, hemi) * hemiAmt;

    if(any(suv < 0.0f) || any(suv > 1.0f))
    {
        float3 lit = albedo * (ambient + NdotL);
        return float4(lit, 1.0f);
    }

    const float  bias  = 0.0015f;
    const float2 texel = 1.0f / float2(2048.0f, 2048.0f);
    float vis = PCF3x3(suv, sdz - bias, texel);

    float3 direct = albedo * (NdotL * vis);
    float3 lit    = albedo * ambient + direct;

    return float4(lit, 1.0f);
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

extern DirectX::XMMATRIX MakeVP_Dir(const Camera& cam, float aspect)
{
	XMVECTOR eye = XMLoadFloat3(&cam.pos);
	XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&cam.dir));
	XMVECTOR up = XMVector3Normalize(XMLoadFloat3(&cam.up));

	XMMATRIX V = XMMatrixLookToLH(eye, dir, up); 
	XMMATRIX P = XMMatrixPerspectiveFovLH(cam.fovY, aspect, cam.nearZ, cam.farZ);
	return XMMatrixMultiply(V, P);
}


static void MoveCamera(Camera& c, float forward, float right, float upMove)
{
	// dir/up로 right 벡터 도출
	XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&c.dir));
	XMVECTOR up = XMVectorSet(0, 1, 0, 0); // 월드업 고정(롤 없음)
	XMVECTOR rightV = XMVector3Normalize(XMVector3Cross(up, dir)); // RH? LH? 방향만 맞으면 OK

	XMVECTOR pos = XMLoadFloat3(&c.pos);
	pos = XMVectorAdd(pos, XMVectorScale(dir, forward));
	pos = XMVectorAdd(pos, XMVectorScale(rightV, right));
	pos = XMVectorAdd(pos, XMVectorScale(up, upMove));
	XMStoreFloat3(&c.pos, pos);
}

static void MoveCamera_FPS(Camera& c, float forward, float right, float upMove, float dt, float speed)
{
	MoveCamera(c, forward * speed * dt, right * speed * dt, upMove * speed * dt);
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

extern DirectX::XMMATRIX MakeLightViewProj(const ShadowCamera& L, const DirectX::XMFLOAT3& focusWorld)
{
	using namespace DirectX;
	XMFLOAT3 d = DirFromYawPitchLH(L.yaw, L.pitch);
	XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&d));
	// 라이트 위치를 포커스 지점(예: 플레이/장면 중심)에서 라이트 방향 반대로 약간 떨어뜨림
	XMVECTOR focus = XMLoadFloat3(&focusWorld);
	XMVECTOR eye = XMVectorSubtract(focus, XMVectorScale(dir, 50.0f));

	XMVECTOR up = XMVectorSet(0, 1, 0, 0);
	XMMATRIX V = XMMatrixLookAtLH(eye, focus, up);

	float e = L.orthoExtent;
	XMMATRIX P = XMMatrixOrthographicOffCenterLH(-e, +e, -e, +e, L.zNear, L.zFar);

	return XMMatrixMultiply(V, P);
}

static inline void BuildMVPs_(
	const DirectX::XMMATRIX& M,
	const Camera& cam,
	float aspect,
	const DirectX::XMMATRIX& lightVP,
	DirectX::XMFLOAT4X4& outMVP_T,
	DirectX::XMFLOAT4X4& outLightVP_T)
{
	using namespace DirectX;
	XMMATRIX VP = MakeVP_Dir(cam, aspect);

	// ★ 화면용은 'VP만' (월드는 VS에서 wp 만들 때 한 번만)
	XMStoreFloat4x4(&outMVP_T, XMMatrixTranspose(VP));

	// ★ 섀도우 샘플용은 '순수 lightVP' (이미 그렇게 하고 계심)
	XMStoreFloat4x4(&outLightVP_T, XMMatrixTranspose(lightVP));
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

D3D12_GRAPHICS_PIPELINE_STATE_DESC DirectXManager::GetPipelineState(ComPointer<ID3D12RootSignature>& rootSignature, D3D12_INPUT_ELEMENT_DESC* vertexLayout, uint32_t vertexLayoutCount, Shader& inVertexShader, Shader& pixelShader)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gfxPsod{};
	gfxPsod.pRootSignature = rootSignature;
	gfxPsod.InputLayout.NumElements = vertexLayoutCount;
	gfxPsod.InputLayout.pInputElementDescs = vertexLayout;
	gfxPsod.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	gfxPsod.VS.BytecodeLength = inVertexShader.GetSize();
	gfxPsod.VS.pShaderBytecode = inVertexShader.GetBuffer();
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

	DX_INPUT.InitWalk(OBJ_RES_NUM);

	m_Sponza = std::make_unique<SponzaModel>();

	m_RenderingObject1 = std::make_unique<RenderingObject>();
	m_RenderingObject2 = std::make_unique<RenderingObject>();
	m_StyleObject = std::make_unique<RenderingObject>();
	m_PlaneObject = std::make_unique<RenderingObject3D>();
	m_CubeObject = std::make_unique<RenderingObject3D>();

	SetVertexLayout();

	UINT w, h;
	DX_WINDOW.GetBackbufferSize(w, h);
	CreateOffscreen(w, h);

	auto device = DX_CONTEXT.GetDevice();

	InitShader();
	CreateSimpleBlitPipeline(); // 새 전용 블릿 PSO 추가
	InitCubePipeline();
	CreateFullscreenQuadVB(w, h);
	InitUploadRenderingObject();

	SetVerticies();
	InitGeometry();

	InitShadowMap(2048);
	InitShadowPipeline();

	if (OBJ_RES_NUM == RES_ISCV2)
	{
		ObjImportOptions Ooptions;
		Ooptions.zUpToYUp = true;
		Ooptions.toLeftHanded = false;
		Ooptions.uniformScale = 1.0f;
		Ooptions.invertUpRotation = true;

		m_Sponza->InitFromOBJ(OBJ_RES_NAME, OBJ_RES_PTH,
			m_CubePSO, m_CubeRootSig, Ooptions);
	}
	else
	{
		ObjImportOptions Ooptions;
		Ooptions.zUpToYUp = false;
		Ooptions.toLeftHanded = false;
		Ooptions.uniformScale = 1.0f;
		Ooptions.invertUpRotation = false;

		m_Sponza->InitFromOBJ(OBJ_RES_NAME, OBJ_RES_PTH,
			m_CubePSO, m_CubeRootSig, Ooptions);
	}

	CreateOnnxResources(w, h);

	// 리소스 상태 초기화
	m_SceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	m_Vbv1 = DirectXManager::GetVertexBufferView(
		m_RenderingObject1->GetVertexBuffer(),
		m_RenderingObject1->GetVertexCount(), sizeof(Vertex));
	m_Vbv2 = DirectXManager::GetVertexBufferView(
		m_RenderingObject2->GetVertexBuffer(),
		m_RenderingObject2->GetVertexCount(), sizeof(Vertex));

	InitDepth(w, h);
	m_Aspect = (float)w / (float)h;

	if (OBJ_RES_NUM == RES_SPONZA)
	{
		m_Cam.pos = { 2.0f, 120.0f, 50.0f };
	}
	else if (OBJ_RES_NUM == RES_SAN_MIGUEL)
	{
		m_Cam.pos = { -13.2f, 2.2f, 1.4f };
	}
	else if (OBJ_RES_NUM == RES_GALLERY)
	{
		m_Cam.pos = { 2.0f, 2.0f, -6.0f };
	}
	else if (OBJ_RES_NUM == RES_ISCV2)
	{
		m_Cam.pos = { -5.0f, 19.0f, -2.5f };
	}

	return true;
}

void DirectXManager::Update(float deltaTime)
{
	if (FrameNum > 0)
	{
		if (FrameNum >= MAX_FRAME)
		{
			if (FrameNum > MAX_FRAME)
			{
				DX_WINDOW.SetShutdown();
			}
			return;
		}

		const CamWalk& camWalk = DX_INPUT.GetCamWalk(FrameNum);
		RotateCameraYawPitch(m_Cam, camWalk.CamRotate.x, camWalk.CamRotate.y);
		MoveCamera(m_Cam, camWalk.CamMove.y, camWalk.CamMove.x, camWalk.CamMove.z);
		return;
	}

	m_Angle += 1.0f * (1.0f / 60.0f); 

	float moveAxis_X = 0.f;
	moveAxis_X += DX_INPUT.isStateKeyDown('D') == true ? 1.f : 0.f;
	moveAxis_X -= DX_INPUT.isStateKeyDown('A') == true ? 1.f : 0.f;

	float moveAxis_Y = 0.f;
	moveAxis_Y += DX_INPUT.isStateKeyDown('W') == true ? 1.f : 0.f;
	moveAxis_Y -= DX_INPUT.isStateKeyDown('S') == true ? 1.f : 0.f;

	if (DX_WINDOW.IsMouseLock() == true)
	{
		const POINT& deltaPos = DX_WINDOW.GetMouseMove();

		RotateCameraYawPitch(m_Cam, deltaPos.x * deltaTime * CAM_MOVE_SPEED, deltaPos.y * deltaTime * CAM_MOVE_SPEED * (-1.f));
	}

	static float MOVE_SPEED = 5.f;

	if (DX_INPUT.isOneKeyDown('M'))
	{
		FrameNum = 0;
	}

	if (DX_INPUT.isOneKeyDown('J'))
	{
		MOVE_SPEED -= 1.f;
		if (MOVE_SPEED >= 0) MOVE_SPEED = 1.f;
	}
	if (DX_INPUT.isOneKeyDown('K'))
	{
		MOVE_SPEED += 1.f;
		if (MOVE_SPEED <= 0) MOVE_SPEED = 1.f;
	}

	float moveSpeed = MOVE_SPEED;
	if (DX_INPUT.isStateKeyDown(VK_LSHIFT) == true)
	{
		moveSpeed *= 5;
	}

	MoveCamera_FPS(m_Cam, moveAxis_Y, moveAxis_X, 0.f, deltaTime, moveSpeed);
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

	cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(m_RenderingObject2->GetImage()->GetIndex()));
	cmd->IASetVertexBuffers(0, 1, &m_Vbv2);
	cmd->DrawInstanced(m_RenderingObject2->GetVertexCount(), 1, 0, 0);

	cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(m_RenderingObject1->GetImage()->GetIndex()));
	cmd->IASetVertexBuffers(0, 1, &m_Vbv1);
	cmd->DrawInstanced(m_RenderingObject1->GetVertexCount(), 1, 0, 0);

}

void DirectXManager::Shutdown()
{
	DestroyShadowMap();

	m_RenderingObject1.release();
	m_RenderingObject2.release();
	m_StyleObject.release();
	m_PlaneObject.release();
	m_CubeObject.release();

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

	m_Sponza->UploadGPUResource(cmdList);
	m_RenderingObject1->UploadGPUResource(cmdList);
	m_RenderingObject2->UploadGPUResource(cmdList);
	m_StyleObject->UploadGPUResource(cmdList);
	m_PlaneObject->UploadGPUResource(cmdList);
	m_CubeObject->UploadGPUResource(cmdList);
}


void DirectXManager::CreateOnnxResources(UINT W, UINT H)
{
	if (DX_ONNX.IsInitialized() == false)
	{
		return;
	}

	switch (DX_ONNX.GetOnnxType())
	{
	case OnnxType::Sanet:
	case OnnxType::WCT2:
	case OnnxType::AdaIN:
	{
		OnnxService::CreateOnnxResources_AdaIN(
			W, H,
			*m_StyleObject->GetImage().get(),
			m_Onnx.get(),
			m_OnnxGPU.get(),
			mHeapCPU.Get(),
			mSceneColor.Get(),
			m_OnnxTexState,
			m_OnnxInputState);
	}
	break;

	case OnnxType::FastNeuralStyle:
	case OnnxType::ReCoNet:
	{
		OnnxService::CreateOnnxResources_FastNeuralStyle(
			W, H, 
			*m_StyleObject->GetImage().get(),
			m_Onnx.get(), 
			m_OnnxGPU.get(), 
			mHeapCPU.Get(), 
			mSceneColor.Get(),
			m_OnnxTexState,
			m_OnnxInputState);
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
		case OnnxType::Sanet:
		case OnnxType::WCT2:
		case OnnxType::AdaIN:
		{
			OnnxService::RecordPreprocess_AdaIN(
				cmd, 
				m_OnnxGPU->m_Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(), 
				mSceneColor.Get(),
				*m_StyleObject->GetImage().get());
		}
		break;

		case OnnxType::FastNeuralStyle:
		case OnnxType::ReCoNet:
		{
			OnnxService::RecordPreprocess_FastNeuralStyle(
				cmd, 
				m_OnnxGPU->m_Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(), 
				mSceneColor.Get(),
				*m_StyleObject->GetImage().get());
		}
		break;
	}
}

void DirectXManager::RecordPostprocess(ID3D12GraphicsCommandList7* cmd)
{
	switch (DX_ONNX.GetOnnxType())
	{
		case OnnxType::Sanet:
		case OnnxType::WCT2:
		case OnnxType::AdaIN:
		{
			OnnxService::RecordPostprocess_AdaIN(cmd, m_OnnxGPU->m_Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), m_OnnxTexState);
		}
		break;

		case OnnxType::FastNeuralStyle:
		case OnnxType::ReCoNet:
		{
			OnnxService::RecordPostprocess_FastNeuralStyle(cmd, m_OnnxGPU->m_Heap.Get(), m_Onnx.get(), m_OnnxGPU.get(), m_OnnxTexState);
		}
		break;

	}
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

	DX_CONTEXT.ExecuteCommandList();  

	// VBV
	m_FSQuadVBV.BufferLocation = mFSQuadVB->GetGPUVirtualAddress();
	m_FSQuadVBV.SizeInBytes = vbSize;
	m_FSQuadVBV.StrideInBytes = sizeof(Vtx2);
}


bool DirectXManager::CreateSimpleBlitPipeline()
{
	ID3D12Device* dev = DX_CONTEXT.GetDevice();

	// RS: t0(SRV)만
	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_ROOT_PARAMETER1   param[1];
	param[0].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC samp{};
	//samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samp.Filter = D3D12_FILTER_ANISOTROPIC;          // ★
	samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samp.MaxAnisotropy = 8;
	samp.MinLOD = 0.0f;
	samp.MaxLOD = D3D12_FLOAT32_MAX;
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
	pso.RTVFormats[0] = DX_WINDOW.GetBackbuffer() ?
		DX_WINDOW.GetBackbuffer()->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;

	return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_BlitPSO2)));
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
	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0); // t0=albedo, t1=shadow
	CD3DX12_ROOT_PARAMETER1 params[2];
	params[0].InitAsConstants(52, 0, 0, D3D12_SHADER_VISIBILITY_ALL);       // ★ 32개
	params[1].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC samp0{}; // albedo
	samp0.Filter = D3D12_FILTER_ANISOTROPIC;
	samp0.AddressU = samp0.AddressV = samp0.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samp0.MaxAnisotropy = 8;
	samp0.ShaderRegister = 0; // s0
	samp0.MipLODBias = 0.0f;
	samp0.MinLOD = 0.0f;
	samp0.MaxLOD = D3D12_FLOAT32_MAX;

	D3D12_STATIC_SAMPLER_DESC sampCmp{}; // shadow compare
	sampCmp.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	sampCmp.AddressU = sampCmp.AddressV = sampCmp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampCmp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	sampCmp.ShaderRegister = 1; // s1

	D3D12_STATIC_SAMPLER_DESC samplers[] = { samp0, sampCmp };

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(_countof(params), params, _countof(samplers), samplers,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPointer<ID3DBlob> rsBlob, rsErr;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &rsErr);
	if (FAILED(hr)) { if (rsErr) OutputDebugStringA((char*)rsErr->GetBufferPointer()); return false; }

	DX_CONTEXT.GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_CubeRootSig));

	// VS/PS
	ComPointer<ID3DBlob> vs, ps, err;
	hr = D3DCompile(g_VS_Shadow, strlen(g_VS_Shadow), nullptr, nullptr, nullptr, "main", "vs_5_1", 0, 0, &vs, &err);
	if (FAILED(hr)) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return false; }
	hr = D3DCompile(g_PS_ShadowPCF3x3, strlen(g_PS_ShadowPCF3x3), nullptr, nullptr, nullptr, "main", "ps_5_1", 0, 0, &ps, &err);
	if (FAILED(hr)) { if (err) OutputDebugStringA((char*)err->GetBufferPointer()); return false; }

	D3D12_INPUT_ELEMENT_DESC il[] = {
	  { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	  { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, sizeof(float) * 3,          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	  { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(float) * (3 + 2),      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_CubeRootSig.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	pso.InputLayout = { il, _countof(il) };
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthEnable = TRUE;
	pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = mSceneColor ? mSceneColor->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM; // ★ 안전
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.SampleDesc = { 1,0 };
	pso.SampleMask = UINT_MAX;

	hr = DX_CONTEXT.GetDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_CubePSO));
	return SUCCEEDED(hr);

}

bool DirectXManager::InitDepth(UINT w, UINT h)
{
	auto dev = DX_CONTEXT.GetDevice();

	if (!m_DsvHeap) {
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		d.NumDescriptors = 1;
		d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&m_DsvHeap)))) return false;
		m_DSV = m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
	}

	D3D12_CLEAR_VALUE optClear{};
	optClear.Format = DXGI_FORMAT_D32_FLOAT;
	optClear.DepthStencil = { 1.0f, 0 };

	CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, w, h, 1, 1);
	rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	DestroyDepth();
	if (FAILED(dev->CreateCommittedResource(
		&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &optClear, IID_PPV_ARGS(&m_Depth)))) return false;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dev->CreateDepthStencilView(m_Depth, &dsv, m_DSV);

	return true;
}

void DirectXManager::DestroyDepth()
{
	if (m_Depth) { m_Depth.Release(); }
}

DirectX::XMFLOAT3 DirectXManager::GetLightDirWS() const
{
	return DirFromYawPitchLH(mLight.yaw, mLight.pitch);
}

void DirectXManager::TransitionShadowToDSV(ID3D12GraphicsCommandList7* cmd)
{
	if (m_ShadowState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			m_ShadowMap.Get(), m_ShadowState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		cmd->ResourceBarrier(1, &b);
		m_ShadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}
}

DirectX::XMMATRIX DirectXManager::GetLightViewProj()
{
	DirectX::XMFLOAT3 focus{ 0,0,0 }; 
	return MakeLightViewProj(mLight, focus);
}

bool DirectXManager::InitShadowMap(UINT size)
{
	auto dev = DX_CONTEXT.GetDevice();
	m_ShadowSize = size;

	if (!m_ShadowDsvHeap) {
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		d.NumDescriptors = 1;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&m_ShadowDsvHeap));
		m_ShadowDSV = m_ShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
	}

	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	auto tex = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, m_ShadowSize, m_ShadowSize, 1, 1);
	tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clear{};
	clear.Format = DXGI_FORMAT_D32_FLOAT;
	clear.DepthStencil = { 1.0f, 0 };

	dev->CreateCommittedResource(
		&hp, D3D12_HEAP_FLAG_NONE, &tex,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
		IID_PPV_ARGS(&m_ShadowMap));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dev->CreateDepthStencilView(m_ShadowMap.Get(), &dsv, m_ShadowDSV);

	D3D12_DESCRIPTOR_HEAP_DESC h{};
	h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	h.NumDescriptors = 4;
	h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dev->CreateDescriptorHeap(&h, IID_PPV_ARGS(&m_ObjSrvHeap2));
	m_ObjSrvCPU = m_ObjSrvHeap2->GetCPUDescriptorHandleForHeapStart();
	m_ObjSrvGPU = m_ObjSrvHeap2->GetGPUDescriptorHandleForHeapStart();

	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuShadow = m_ObjSrvCPU;
	cpuShadow.ptr += inc * 1; 

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	{
		auto dst0 = m_ObjSrvCPU;    
		auto srcPlane = DX_IMAGE.GetCPUDescriptorHandle(m_PlaneObject->GetImage()->GetIndex());
		dev->CopyDescriptorsSimple(1, dst0, srcPlane, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_CPU_DESCRIPTOR_HANDLE dst1 = m_ObjSrvCPU;  dst1.ptr += inc * 1;  // slot 1
		dev->CreateShaderResourceView(m_ShadowMap.Get(), &srv, dst1);
	}

	{
		D3D12_CPU_DESCRIPTOR_HANDLE dst2 = m_ObjSrvCPU;  dst2.ptr += inc * 2;  // slot 2
		auto srcCube = DX_IMAGE.GetCPUDescriptorHandle(m_CubeObject->GetImage()->GetIndex());
		dev->CopyDescriptorsSimple(1, dst2, srcCube, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_CPU_DESCRIPTOR_HANDLE dst3 = m_ObjSrvCPU;  dst3.ptr += inc * 3;  // slot 3
		dev->CreateShaderResourceView(m_ShadowMap.Get(), &srv, dst3);
	}

	dev->CreateShaderResourceView(m_ShadowMap.Get(), &srv, cpuShadow);

	m_ShadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	return true;
}

void DirectXManager::DestroyShadowMap()
{
	if (m_ShadowMap) m_ShadowMap.Release();
	if (m_ShadowDsvHeap) m_ShadowDsvHeap.Release();
	m_ShadowDSV = {};
	//m_ShadowSRV_CPU = {};
	//m_ShadowSRV_GPU = {};
}

bool DirectXManager::InitShadowPipeline()
{
	auto dev = DX_CONTEXT.GetDevice();

	CD3DX12_ROOT_PARAMETER1 params[1];
	params[0].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // b0: 16개

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
	rsDesc.Init_1_1(_countof(params), params, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPointer<ID3DBlob> rsBlob, rsErr;
	D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &rsErr);
	dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_ShadowRS));

	const char* SHADOW_VS = R"(
        cbuffer CB : register(b0) { float4x4 uLightMVP; }
        struct VSIn  { float3 pos:POSITION; float2 uv:TEXCOORD; };
        struct VSOut { float4 pos:SV_Position; };
        VSOut main(VSIn i){ VSOut o; o.pos = mul(float4(i.pos,1), uLightMVP); return o; }
    )";

	ComPointer<ID3DBlob> vs, err;
	D3DCompile(SHADOW_VS, strlen(SHADOW_VS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, &err);

	D3D12_INPUT_ELEMENT_DESC il[] = {
		{ "POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 },
		{ "TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,   0,12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
	pso.pRootSignature = m_ShadowRS.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { nullptr, 0 };
	pso.InputLayout = { il, _countof(il) };
	pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	pso.RasterizerState.DepthBias = 100;
	pso.RasterizerState.SlopeScaledDepthBias = 2.0f;
	pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	pso.DepthStencilState.DepthEnable = TRUE;
	pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pso.NumRenderTargets = 0;                 // ★ RTV 없음
	pso.SampleDesc = { 1,0 };
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_ShadowPSO)));
}

void DirectXManager::RenderShadowPass(ID3D12GraphicsCommandList7* cmd)
{
	D3D12_VIEWPORT vp{ 0,0,(float)m_ShadowSize,(float)m_ShadowSize, 0,1 };
	D3D12_RECT sc{ 0,0,(LONG)m_ShadowSize,(LONG)m_ShadowSize };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	cmd->OMSetRenderTargets(0, nullptr, FALSE, &m_ShadowDSV);
	cmd->ClearDepthStencilView(m_ShadowDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	cmd->SetPipelineState(m_ShadowPSO.Get());
	cmd->SetGraphicsRootSignature(m_ShadowRS.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	using namespace DirectX;
	const XMMATRIX LVP = GetLightViewProj();

	{
		XMFLOAT4X4 WLP_T;
		XMStoreFloat4x4(&WLP_T, XMMatrixTranspose(XMMatrixIdentity() * LVP));
		cmd->SetGraphicsRoot32BitConstants(0, 16, &WLP_T, 0);
		m_PlaneObject->RenderingDepthOnly(cmd);
	}

	{
		XMMATRIX W = XMMatrixRotationY(m_Angle) * XMMatrixRotationX(0); 
		XMFLOAT4X4 WLP_T;
		XMStoreFloat4x4(&WLP_T, XMMatrixTranspose(W * LVP));
		cmd->SetGraphicsRoot32BitConstants(0, 16, &WLP_T, 0);
		m_CubeObject->RenderingDepthOnly(cmd);
	}
}

void DirectXManager::TransitionShadowToSRV(ID3D12GraphicsCommandList7* cmd)
{
	if (m_ShadowState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			m_ShadowMap.Get(),
			m_ShadowState,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		m_ShadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
}

void DirectXManager::BuildMVPs(const DirectX::XMMATRIX& M, const Camera& cam, float aspect, const DirectX::XMMATRIX& lightVP, DirectX::XMFLOAT4X4& outMVP_T, DirectX::XMFLOAT4X4& outLightVP_T)
{
	BuildMVPs_(M, cam, aspect, lightVP, outMVP_T, outLightVP_T);
}

void DirectXManager::ResizeOnnxResources(UINT W, UINT H)
{
	if (m_Onnx != nullptr && m_OnnxGPU != nullptr)
	{
		if (W == m_Onnx->m_Width && H == m_Onnx->m_Height) return;
		m_OnnxGPU->Reset();

		ID3D12Resource* styleTex = m_StyleObject->GetImage()->GetTexture();
		auto sDesc = styleTex->GetDesc();
		UINT styleW = (UINT)sDesc.Width;
		UINT styleH = sDesc.Height;

		DX_ONNX.ResizeIO(DX_CONTEXT.GetDevice(), W, H, styleW, styleH);

		CreateOnnxResources(W, H);
		m_Onnx->m_Width = W; m_Onnx->m_Height = H;
		return;
	}
	
	ID3D12Resource* styleTex = m_StyleObject->GetImage()->GetTexture();
	auto sDesc = styleTex->GetDesc();
	UINT styleW = (UINT)sDesc.Width;
	UINT styleH = sDesc.Height;

}

bool DirectXManager::CreateOnnxComputePipeline()
{
	ID3D12Device* device = DX_CONTEXT.GetDevice();

	CD3DX12_DESCRIPTOR_RANGE1 rangeSRV(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		2,
		0 
	);

	CD3DX12_DESCRIPTOR_RANGE1 rangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER1 params[3];
	params[0].InitAsDescriptorTable(1, &rangeSRV, D3D12_SHADER_VISIBILITY_ALL); 
	params[1].InitAsDescriptorTable(1, &rangeUAV, D3D12_SHADER_VISIBILITY_ALL); 
	params[2].InitAsConstantBufferView(0);                                      

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
		IID_PPV_ARGS(&m_Onnx->m_PreRS));
	if (FAILED(hr)) return false;

	// CS compile
	ComPointer<ID3DBlob> csPre, csPost;
	hr = D3DCompileFromFile(L"./Shaders/cs_preprocess.hlsl", nullptr, nullptr, "main", "cs_5_0", 0, 0, &csPre, &errBlob);
	if (FAILED(hr) || !csPre)  return false;
	hr = D3DCompileFromFile(L"./Shaders/cs_postprocess.hlsl", nullptr, nullptr, "main", "cs_5_0", 0, 0, &csPost, &errBlob);
	if (FAILED(hr) || !csPost) return false;

	// PSO(pre)
	D3D12_COMPUTE_PIPELINE_STATE_DESC preDesc{};
	preDesc.pRootSignature = m_Onnx->m_PreRS.Get();
	preDesc.CS = { csPre->GetBufferPointer(), csPre->GetBufferSize() };
	hr = device->CreateComputePipelineState(&preDesc, IID_PPV_ARGS(&m_Onnx->m_PrePSO));
	if (FAILED(hr))
	{
		return false;
	}

	// PSO(post)  같은 RS 재사용
	D3D12_COMPUTE_PIPELINE_STATE_DESC postDesc{};
	postDesc.pRootSignature = m_Onnx->m_PreRS.Get();
	postDesc.CS = { csPost->GetBufferPointer(), csPost->GetBufferSize() };
	if (FAILED(device->CreateComputePipelineState(&postDesc, IID_PPV_ARGS(&m_Onnx->m_PostPSO)))) return false;

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

	m_Aspect = (h == 0) ? m_Aspect : (float)w / (float)h;
	InitDepth(w, h);
}

void DirectXManager::RenderOffscreen(ID3D12GraphicsCommandList7* cmd)
{
	 // 섀도우맵을 픽셀셰이더 SRV 상태로
	TransitionShadowToDSV(cmd);
	RenderShadowPass(cmd);
	TransitionShadowToSRV(cmd);

	// SceneColor → RTV
	if (m_SceneColorState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(mSceneColor.Get(), m_SceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1, &b);
		m_SceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	const FLOAT clear[4] = { 0.6f ,0.65f, 0.9f, 1 };
	cmd->OMSetRenderTargets(1, &m_RtvScene, FALSE, nullptr);
	cmd->ClearRenderTargetView(m_RtvScene, clear, 0, nullptr);

	auto vp = DX_WINDOW.CreateViewport();
	auto sc = DX_WINDOW.CreateScissorRect();
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->ClearDepthStencilView(m_DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	UploadGPUResource(cmd);
	RenderImage(cmd);

	auto dev = DX_CONTEXT.GetDevice();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	if (m_Sponza) {
		m_Sponza->Render(cmd, m_Cam, m_Aspect, m_RtvScene, m_DSV);
	}

	{
		DX_MANAGER.m_ObjSrvGPU = m_ObjSrvHeap2->GetGPUDescriptorHandleForHeapStart();
	}

	{
		D3D12_GPU_DESCRIPTOR_HANDLE base = m_ObjSrvHeap2->GetGPUDescriptorHandleForHeapStart();
		base.ptr += inc * 2;
		DX_MANAGER.m_ObjSrvGPU = base;
	}

	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mSceneColor.Get(), m_SceneColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		m_SceneColorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}
}



void DirectXManager::BlitToBackbuffer(ID3D12GraphicsCommandList7* cmd)
{
	if (m_OnnxTexState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			m_OnnxGPU->m_OnnxTex.Get(), m_OnnxTexState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		m_OnnxTexState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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

	ID3D12DescriptorHeap* heaps[] = { m_OnnxGPU->m_Heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->SetGraphicsRootDescriptorTable(0, m_OnnxGPU->m_OnnxTexSRV_GPU);

	cmd->IASetVertexBuffers(0, 0, nullptr);
	cmd->DrawInstanced(3, 1, 0, 0);
}


void DirectXManager::InitBlitPipeline()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = GetPipelineState(
		m_RootSignature, 
		GetVertexLayout(), 
		static_cast<uint32_t>(GetVertexLayoutCount()),
		vertexShader, 
		pixelShader);

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

	m_RenderingObject1->AddTriangle(vertex1, 3);
	m_RenderingObject1->AddTriangle(vertex2, 3);

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

	m_RenderingObject2->AddTriangle(vertex3, 3);
	m_RenderingObject2->AddTriangle(vertex4, 3);
}

void DirectXManager::SetVertexLayout()
{
	m_VertexLayout[0] = { "Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	m_VertexLayout[1] = { "Texcoord", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(float) * 2, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
}


void DirectXManager::InitUploadRenderingObject()
{
	if (m_RenderingObject1->Init("./Resources/TEX_Noise.png", 0) == false)
	{
		return;
	}

	if (m_RenderingObject2->Init("./Resources/Image.png", 1) == false)
	{
		return;
	}

	if (m_StyleObject->Init("./Resources/Style_Bb.png", 2) == false)
	{
		return;
	}

	if (m_PlaneObject->Init("./Resources/Block.png", 3, m_CubePSO, m_CubeRootSig) == false)
	{
		return;
	}

	if (m_CubeObject->Init("./Resources/Dog.png", 4, m_CubePSO, m_CubeRootSig) == false)
	{
		return;
	}

}

void DirectXManager::InitGeometry()
{
	{
		const float H = 50.0f;
		const float tile = 10.0f;

		Vtx v[4] = {
			{{-H, 0.0f, -H}, {0.0f,           0.0f          }, {0,1,0}},
			{{-H, 0.0f,  H}, {0.0f,           100.0f / tile   }, {0,1,0}},
			{{ H, 0.0f,  H}, {100.0f / tile,    100.0f / tile   }, {0,1,0}},
			{{ H, 0.0f, -H}, {100.0f / tile,    0.0f          }, {0,1,0}},
		};
		uint16_t i[6] = { 0,1,2, 0,2,3 };
		// vbSize/ibSize/index 동일
		m_PlaneObject->InitGeometry(v, sizeof(v), i, sizeof(i), 6);
	}

	// Cube (면 고정 노멀)
	{
		const float c = 0.5f;
		Vtx v[] = {
			// +Z
			{{-c,-c, c},{0,1},{0,0,1}}, {{-c, c, c},{0,0},{0,0,1}}, {{ c, c, c},{1,0},{0,0,1}}, {{ c,-c, c},{1,1},{0,0,1}},
			// -Z
			{{ c,-c,-c},{0,1},{0,0,-1}}, {{ c, c,-c},{0,0},{0,0,-1}}, {{-c, c,-c},{1,0},{0,0,-1}}, {{-c,-c,-c},{1,1},{0,0,-1}},
			// +X
			{{ c,-c, c},{0,1},{1,0,0}}, {{ c, c, c},{0,0},{1,0,0}}, {{ c, c,-c},{1,0},{1,0,0}}, {{ c,-c,-c},{1,1},{1,0,0}},
			// -X
			{{-c,-c,-c},{0,1},{-1,0,0}}, {{-c, c,-c},{0,0},{-1,0,0}}, {{-c, c, c},{1,0},{-1,0,0}}, {{-c,-c, c},{1,1},{-1,0,0}},
			// +Y
			{{-c, c, c},{0,1},{0,1,0}}, {{-c, c,-c},{0,0},{0,1,0}}, {{ c, c,-c},{1,0},{0,1,0}}, {{ c, c, c},{1,1},{0,1,0}},
			// -Y
			{{-c,-c,-c},{0,1},{0,-1,0}}, {{-c,-c, c},{0,0},{0,-1,0}}, {{ c,-c, c},{1,0},{0,-1,0}}, {{ c,-c,-c},{1,1},{0,-1,0}},
		};
		uint16_t idx[] = {
			0,1,2, 0,2,3,     4,5,6, 4,6,7,
			8,9,10, 8,10,11,  12,13,14, 12,14,15,
			16,17,18, 16,18,19, 20,21,22, 20,22,23,
		};
		m_CubeObject->InitGeometry(v, sizeof(v), idx, sizeof(idx), (UINT)_countof(idx));
	}
}

void DirectXManager::InitShader()
{
	Shader rootSignatureShader("RootSignature.cso");
	//Shader vertexShader("VertexShader.cso");
	//Shader pixelShader("PixelShader.cso");

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

	m_RtvScene = mOffscreenRtvHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateRenderTargetView(mSceneColor.Get(), nullptr, m_RtvScene);

	// === (CPU 경로용) Resolved: UAV 가능 + SRV ===
	DXGI_FORMAT backFmt = DX_WINDOW.GetBackbuffer()->GetDesc().Format;
	td.Format = backFmt;
	td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	m_SceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	return true;
}

void DirectXManager::DestroyOffscreen()
{
	mSceneColor.Release();
	mOffscreenRtvHeap.Release();
	m_BlitSrvHeap.Release();

	m_RtvScene = {};
	m_ResolvedSrvCPU = {};
	m_ResolvedSrvGPU = {};

	m_SceneColorState = D3D12_RESOURCE_STATE_COMMON;
}

