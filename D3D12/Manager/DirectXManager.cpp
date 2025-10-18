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

#define MOVE_SPEED 50
#define CAM_MOVE_SPEED 0.05f

Shader vertexShader("VertexShader.cso");
Shader pixelShader("PixelShader.cso");

struct ShadowCamera {
	DirectX::XMFLOAT3 dir{ 0.5f, -1.0f, 0.3f };
	float yaw = 0.0f, pitch = -0.9f;
	float orthoExtent = 60.0f; // 바닥 평면이 ±50m니까 60m로 커버
	float zNear = 0.1f, zFar = 150.0f;
};
ShadowCamera mLight;



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
    float4 c = gTex.Sample(gSmp, i.uv);

    clip(c.a - 0.001f);

    return float4(c.rgb, 1.0f);
}
)";

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

  // 월드 위치: (로컬) * (transposed world)
  float4 wp = mul(float4(i.pos,1), uWorld);

  // 화면 변환 (transposed)
  o.pos = mul(wp, uMVP);
  o.uv  = i.uv;

  // 섀도우 좌표: (월드) * (transposed LightVP)
  float4 lp = mul(wp, uLightVP);
  float3 ndc = lp.xyz / lp.w;
  o.sh = float4(ndc*0.5f + 0.5f, 1.0f);

  // 노멀 변환: 벡터 * (transposed world)의 상단 3x3
  // (SetGraphicsRoot32BitConstants로 넘길 때 transpose한 행렬을 쓰므로, vector*matrix가 맞습니다)
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

    // ★ 알파 테스트 추가
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
	mSponza = std::make_unique<SponzaModel>();

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

	mSponza->InitFromOBJ("./Resources/Sponza_B/sponza.obj",
		"./Resources/Sponza_B/",
		mCubePSO, mCubeRootSig);

	SetVerticies();
	InitGeometry();

	InitShadowMap(2048);
	InitShadowPipeline();

	CreateOnnxResources(w, h);

	// 리소스 상태 초기화
	mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

	vbv1 = DirectXManager::GetVertexBufferView(
		m_RenderingObject1->GetVertexBuffer(),
		m_RenderingObject1->GetVertexCount(), sizeof(Vertex));
	vbv2 = DirectXManager::GetVertexBufferView(
		m_RenderingObject2->GetVertexBuffer(),
		m_RenderingObject2->GetVertexCount(), sizeof(Vertex));

	InitDepth(w, h);
	mAspect = (float)w / (float)h;

	mCam.pos = { 0, 2.0f, -6.0f };

	return true;
}

void DirectXManager::Update(float deltaTime)
{
	static int frame = 0;
	std::vector<Triangle>& triangles = m_RenderingObject1->GetTriangle();
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

	//m_RenderingObject1->UploadCPUResource(false, true);

	mAngle += 1.0f * (1.0f / 60.0f); 

	float moveAxis_X = 0.f;
	moveAxis_X += DX_INPUT.isStateKeyDown('D') == true ? 1.f : 0.f;
	moveAxis_X -= DX_INPUT.isStateKeyDown('A') == true ? 1.f : 0.f;

	float moveAxis_Y = 0.f;
	moveAxis_Y += DX_INPUT.isStateKeyDown('W') == true ? 1.f : 0.f;
	moveAxis_Y -= DX_INPUT.isStateKeyDown('S') == true ? 1.f : 0.f;

	if (DX_WINDOW.IsMouseLock() == true)
	{
		const POINT& deltaPos = DX_WINDOW.GetMouseMove();

		RotateCameraYawPitch(mCam, deltaPos.x * deltaTime * CAM_MOVE_SPEED, deltaPos.y * deltaTime * CAM_MOVE_SPEED * (-1.f));
	}

	float moveSpeed = MOVE_SPEED;
	if (DX_INPUT.isStateKeyDown(VK_LSHIFT) == true)
	{
		moveSpeed *= 5;
	}

	MoveCamera_FPS(mCam, moveAxis_Y, moveAxis_X, 0.f, deltaTime, moveSpeed);
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
	cmd->IASetVertexBuffers(0, 1, &vbv2);
	cmd->DrawInstanced(m_RenderingObject2->GetVertexCount(), 1, 0, 0);

	cmd->SetGraphicsRootDescriptorTable(2, DX_IMAGE.GetGPUDescriptorHandle(m_RenderingObject1->GetImage()->GetIndex()));
	cmd->IASetVertexBuffers(0, 1, &vbv1);
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

	mSponza->UploadGPUResource(cmdList);
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
	case OnnxType::Udnie:
	{
		OnnxService::CreateOnnxResources_Udnie(
			W, H,
			*m_StyleObject->GetImage().get(),
			m_Onnx.get(),
			m_OnnxGPU.get(),
			mHeapCPU.Get(),
			mSceneColor.Get(),
			mOnnxTexState,
			mOnnxInputState
			);
		break;
	}

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
			mOnnxTexState,
			mOnnxInputState);
	}
	break;

	case OnnxType::FastNeuralStyle:
	{
		OnnxService::CreateOnnxResources_FastNeuralStyle(
			W, H, 
			*m_StyleObject->GetImage().get(),
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
			*m_StyleObject->GetImage().get(),
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
			*m_StyleObject->GetImage().get(),
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

		case OnnxType::Sanet:
		case OnnxType::WCT2:
		case OnnxType::AdaIN:
		{
			OnnxService::RecordPreprocess_AdaIN(
				cmd, 
				m_OnnxGPU->Heap.Get(),
				m_Onnx.get(),
				m_OnnxGPU.get(), 
				mSceneColor.Get(),
				*m_StyleObject->GetImage().get());
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
				*m_StyleObject->GetImage().get());
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
				*m_StyleObject->GetImage().get());

		}
		break;

		case OnnxType::BlindVideo:
		{
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

		case OnnxType::Sanet:
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

	DX_CONTEXT.GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&mCubeRootSig));

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
	pso.pRootSignature = mCubeRootSig.Get();
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

	hr = DX_CONTEXT.GetDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mCubePSO));
	return SUCCEEDED(hr);

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

DirectX::XMFLOAT3 DirectXManager::GetLightDirWS() const
{
	return DirFromYawPitchLH(mLight.yaw, mLight.pitch);
}

void DirectXManager::TransitionShadowToDSV(ID3D12GraphicsCommandList7* cmd)
{
	if (mShadowState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mShadowMap.Get(), mShadowState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		cmd->ResourceBarrier(1, &b);
		mShadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
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
	mShadowSize = size;

	if (!mShadowDsvHeap) {
		D3D12_DESCRIPTOR_HEAP_DESC d{};
		d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		d.NumDescriptors = 1;
		dev->CreateDescriptorHeap(&d, IID_PPV_ARGS(&mShadowDsvHeap));
		mShadowDSV = mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
	}

	// R32_TYPELESS + DSV/clear
	CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
	auto tex = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, mShadowSize, mShadowSize, 1, 1);
	tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clear{};
	clear.Format = DXGI_FORMAT_D32_FLOAT;
	clear.DepthStencil = { 1.0f, 0 };

	dev->CreateCommittedResource(
		&hp, D3D12_HEAP_FLAG_NONE, &tex,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
		IID_PPV_ARGS(&mShadowMap));

	// DSV
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dev->CreateDepthStencilView(mShadowMap.Get(), &dsv, mShadowDSV);

	// ★ 오브젝트 전용 SRV 힙(2칸) 생성: t0=알베도, t1=섀도우
	D3D12_DESCRIPTOR_HEAP_DESC h{};
	h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	h.NumDescriptors = 4;
	h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dev->CreateDescriptorHeap(&h, IID_PPV_ARGS(&mObjSrvHeap2));
	mObjSrvCPU = mObjSrvHeap2->GetCPUDescriptorHandleForHeapStart();
	mObjSrvGPU = mObjSrvHeap2->GetGPUDescriptorHandleForHeapStart();

	// t1 = Shadow SRV 만들기(일단 t1에 쓸 SRV만 만들어 둔다)
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuShadow = mObjSrvCPU;
	cpuShadow.ptr += inc * 1; // 슬롯 1 == t1

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// [0,1] = Plane (t0=albedo_plane, t1=shadow)
	{
		// t0 <- plane albedo
		auto dst0 = mObjSrvCPU;                         // slot 0
		auto srcPlane = DX_IMAGE.GetCPUDescriptorHandle(m_PlaneObject->GetImage()->GetIndex());
		dev->CopyDescriptorsSimple(1, dst0, srcPlane, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// t1 <- shadow
		D3D12_CPU_DESCRIPTOR_HANDLE dst1 = mObjSrvCPU;  dst1.ptr += inc * 1;  // slot 1
		dev->CreateShaderResourceView(mShadowMap.Get(), &srv, dst1);
	}

	// [2,3] = Cube (t0=albedo_cube, t1=shadow)
	{
		// t0 <- cube albedo
		D3D12_CPU_DESCRIPTOR_HANDLE dst2 = mObjSrvCPU;  dst2.ptr += inc * 2;  // slot 2
		auto srcCube = DX_IMAGE.GetCPUDescriptorHandle(m_CubeObject->GetImage()->GetIndex());
		dev->CopyDescriptorsSimple(1, dst2, srcCube, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// t1 <- shadow (중복 생성해도 OK; 같은 리소스)
		D3D12_CPU_DESCRIPTOR_HANDLE dst3 = mObjSrvCPU;  dst3.ptr += inc * 3;  // slot 3
		dev->CreateShaderResourceView(mShadowMap.Get(), &srv, dst3);
	}

	dev->CreateShaderResourceView(mShadowMap.Get(), &srv, cpuShadow);

	mShadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	return true;
}

void DirectXManager::DestroyShadowMap()
{
	if (mShadowMap) mShadowMap.Release();
	if (mShadowDsvHeap) mShadowDsvHeap.Release();
	mShadowDSV = {};
	mShadowSRV_CPU = {};
	mShadowSRV_GPU = {};
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
	dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&mShadowRS));

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
	pso.pRootSignature = mShadowRS.Get();
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

	return SUCCEEDED(dev->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mShadowPSO)));
}

void DirectXManager::RenderShadowPass(ID3D12GraphicsCommandList7* cmd)
{
	D3D12_VIEWPORT vp{ 0,0,(float)mShadowSize,(float)mShadowSize, 0,1 };
	D3D12_RECT sc{ 0,0,(LONG)mShadowSize,(LONG)mShadowSize };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	cmd->OMSetRenderTargets(0, nullptr, FALSE, &mShadowDSV);
	cmd->ClearDepthStencilView(mShadowDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	cmd->SetPipelineState(mShadowPSO.Get());
	cmd->SetGraphicsRootSignature(mShadowRS.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	using namespace DirectX;
	const XMMATRIX LVP = GetLightViewProj();

	// Plane: W = I
	{
		XMFLOAT4X4 WLP_T;
		XMStoreFloat4x4(&WLP_T, XMMatrixTranspose(XMMatrixIdentity() * LVP));
		cmd->SetGraphicsRoot32BitConstants(0, 16, &WLP_T, 0);
		m_PlaneObject->RenderingDepthOnly(cmd);
	}

	// Cube: W = 회전 (mAngle 사용)
	{
		XMMATRIX W = XMMatrixRotationY(mAngle) * XMMatrixRotationX(0); // 필요시 이동 더하기
		XMFLOAT4X4 WLP_T;
		XMStoreFloat4x4(&WLP_T, XMMatrixTranspose(W * LVP));
		cmd->SetGraphicsRoot32BitConstants(0, 16, &WLP_T, 0);
		m_CubeObject->RenderingDepthOnly(cmd);
	}
}

void DirectXManager::TransitionShadowToSRV(ID3D12GraphicsCommandList7* cmd)
{
	if (mShadowState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mShadowMap.Get(),
			mShadowState,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		cmd->ResourceBarrier(1, &b);
		mShadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
		if (W == m_Onnx->Width && H == m_Onnx->Height) return;
		m_OnnxGPU->Reset();

		ID3D12Resource* styleTex = m_StyleObject->GetImage()->GetTexture();
		auto sDesc = styleTex->GetDesc();
		UINT styleW = (UINT)sDesc.Width;
		UINT styleH = sDesc.Height;

		DX_ONNX.ResizeIO(DX_CONTEXT.GetDevice(), W, H, styleW, styleH);

		CreateOnnxResources(W, H);
		m_Onnx->Width = W; m_Onnx->Height = H;
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

	mAspect = (h == 0) ? mAspect : (float)w / (float)h;
	InitDepth(w, h);
}

void DirectXManager::RenderOffscreen(ID3D12GraphicsCommandList7* cmd)
{
	 // 섀도우맵을 픽셀셰이더 SRV 상태로
	TransitionShadowToDSV(cmd);
	RenderShadowPass(cmd);
	TransitionShadowToSRV(cmd);

	// SceneColor → RTV
	if (mSceneColorState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(mSceneColor.Get(), mSceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmd->ResourceBarrier(1, &b);
		mSceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	const FLOAT clear[4] = { 0.6f ,0.65f, 0.9f, 1 };
	cmd->OMSetRenderTargets(1, &mRtvScene, FALSE, nullptr);
	cmd->ClearRenderTargetView(mRtvScene, clear, 0, nullptr);

	auto vp = DX_WINDOW.CreateViewport();
	auto sc = DX_WINDOW.CreateScissorRect();
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->ClearDepthStencilView(mDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	UploadGPUResource(cmd);
	RenderImage(cmd);

	auto dev = DX_CONTEXT.GetDevice();
	const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ★ 이 힙을 바인딩해야 PS에서 t0/t1을 읽는다
	//ID3D12DescriptorHeap* heaps[] = { mSponza->GetHeap() };
	//cmd->SetDescriptorHeaps(1, heaps);   // ★ 추가

	if (mSponza) {
		mSponza->Render(cmd, mCam, mAspect, mRtvScene, mDSV);
	}

	// Plane: base = slot 0 (t0=plane, t1=shadow)
	{
		DX_MANAGER.mObjSrvGPU = mObjSrvHeap2->GetGPUDescriptorHandleForHeapStart();
		m_PlaneObject->Rendering(cmd, mCam, mAspect, mRtvScene, mDSV, 0.f);
	}

	// Cube: base = slot 2 (t0=cube, t1=shadow)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE base = mObjSrvHeap2->GetGPUDescriptorHandleForHeapStart();
		base.ptr += inc * 2;
		DX_MANAGER.mObjSrvGPU = base;
		m_CubeObject->Rendering(cmd, mCam, mAspect, mRtvScene, mDSV, mAngle);
	}

	// SceneColor → NPSR
	{
		auto b = CD3DX12_RESOURCE_BARRIER::Transition(
			mSceneColor.Get(), mSceneColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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

	if (m_StyleObject->Init("./Resources/Style_.png", 2) == false)
	{
		return;
	}

	if (m_PlaneObject->Init("./Resources/Block.png", 3, mCubePSO, mCubeRootSig) == false)
	{
		return;
	}

	if (m_CubeObject->Init("./Resources/Dog.png", 4, mCubePSO, mCubeRootSig) == false)
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

