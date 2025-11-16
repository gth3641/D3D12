#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Support/ImageLoader.h"
#include "Support/SponzaModel.h"
#include "Util/Util.h"
#include "Util/OnnxDefine.h"

#include "Object/RenderingObject3D.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <d3dcompiler.h>
#include <onnxruntime_cxx_api.h>
#include <DirectXMath.h>
#include <memory>

#define DX_MANAGER DirectXManager::Get()

#define RES_SPONZA      1
#define RES_SAN_MIGUEL  3
#define RES_GALLERY     4
#define RES_ISCV2       5

class Shader;
using namespace DirectX;

struct Camera {
    XMFLOAT3    pos{ 0, 2.0f, -6.0f }; 
    XMFLOAT3    dir{ 0, 0,  1.0f };
    XMFLOAT3    up{ 0, 1.0f, 0 };
    float       fovY    = XM_PIDIV4;
    float       nearZ   = 0.1f;
    float       farZ    = 5000.0f;
    float       yaw     = 0.0f;
    float       pitch   = 0.0f;                  
};

class DirectXManager
{
public: // Singleton pattern to ensure only one instance exists 
    DirectXManager(const DirectXManager&) = delete;
    DirectXManager& operator=(const DirectXManager&) = delete;

    inline static DirectXManager& Get()
    {
        static DirectXManager instance;
        return instance;
    }

public:
    DirectXManager() = default;

public: // Static & Override
    static D3D12_HEAP_PROPERTIES GetHeapUploadProperties();
    static D3D12_HEAP_PROPERTIES GetDefaultUploadProperties();
    static D3D12_GRAPHICS_PIPELINE_STATE_DESC GetPipelineState(
        ComPointer<ID3D12RootSignature>& rootSignature, 
        D3D12_INPUT_ELEMENT_DESC* vertexLayout, 
        uint32_t vertexLayoutCount, 
        Shader& vertexShader,
        Shader& pixelShader
    );
    static D3D12_RESOURCE_DESC GetVertexResourceDesc();
    static D3D12_RESOURCE_DESC GetUploadResourceDesc(uint32_t textureSize);
    static D3D12_RESOURCE_DESC GetTextureResourceDesc(const ImageData& textureData);

    static D3D12_TEXTURE_COPY_LOCATION GetTextureSource(
        ComPointer<ID3D12Resource2>& uploadBuffer,
        ImageData& textureData,
        uint32_t textureStride
    );

    static D3D12_TEXTURE_COPY_LOCATION GetTextureDestination(ComPointer<ID3D12Resource2>& texture);
    static D3D12_BOX GetTextureSizeAsBox(const ImageData& textureData);
    static D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView(ComPointer<ID3D12Resource2>& vertexBuffer, uint32_t vertexCount, uint32_t vertexSize);

    static void BuildMVPs(
        const DirectX::XMMATRIX& M,
        const Camera& cam,
        float aspect,
        const DirectX::XMMATRIX& lightVP,
        DirectX::XMFLOAT4X4& outMVP_T,
        DirectX::XMFLOAT4X4& outLightVP_T);

public: // Functions
    bool Init();
    void Shutdown();
    void Update(float deltaTime);
    void RenderImage(ID3D12GraphicsCommandList7* cmd); 
    void Resize();

    void RenderOffscreen(ID3D12GraphicsCommandList7* cmd);
    void BlitToBackbuffer(ID3D12GraphicsCommandList7* cmd);

    void UploadGPUResource(ID3D12GraphicsCommandList7* cmdList);
    void CreateOnnxResources(UINT W, UINT H);
    void ResizeOnnxResources(UINT W, UINT H);

    void InitBlitPipeline();
    void RecordPreprocess(ID3D12GraphicsCommandList7* cmd);
    void RecordPostprocess(ID3D12GraphicsCommandList7* cmd);

    bool CreateOnnxComputePipeline();

    void Debug_DumpOrtOutput(ID3D12GraphicsCommandList7* cmd);
    void Debug_DumpBuffer(ID3D12Resource* src, const char* tag);

    void TransitionShadowToDSV(ID3D12GraphicsCommandList7* cmd);
    bool InitShadowMap(UINT size = 2048);
    bool InitShadowPipeline();
    void DestroyShadowMap();

    void RenderShadowPass(ID3D12GraphicsCommandList7* cmd);
    void TransitionShadowToSRV(ID3D12GraphicsCommandList7* cmd);

    DirectX::XMFLOAT3 GetLightDirWS() const;
    DirectX::XMMATRIX GetLightViewProj();

    //===========Getter=================//
    size_t GetVertexLayoutCount() { return _countof(m_VertexLayout); }
    D3D12_INPUT_ELEMENT_DESC* GetVertexLayout() { return m_VertexLayout; }
    ComPointer<ID3D12RootSignature>& GetRootSignature() { return m_RootSignature; }
    ComPointer<ID3D12PipelineState>& GetPipelineStateObj() { return m_PipelineStateObj; }
    ID3D12Resource* GetShadowMap() { return m_ShadowMap.Get(); }
    UINT GetShadowSize() const { return m_ShadowSize; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetObjSrvGPU() { return m_ObjSrvGPU; }
    //==================================//

    void SetObjSrvGPU(D3D12_GPU_DESCRIPTOR_HANDLE ObjSrvGPU) { m_ObjSrvGPU = ObjSrvGPU; }

private: // Functions
    void InitUploadRenderingObject();
    void InitGeometry();
    void InitShader();

    void SetVerticies();
    void SetVertexLayout();

    void UploadTextureBuffer();
    void CreateSRV();

    void InitPipelineSate(Shader& vertexShader, Shader& pixelShader);

    bool CreateOffscreen(uint32_t w, uint32_t h);
    void DestroyOffscreen();

    bool CreateSimpleBlitPipeline();
    void CreateFullscreenQuadVB(UINT w, UINT h);

    bool InitCubePipeline();
    bool InitDepth(UINT w, UINT h);
    void DestroyDepth();

public:
    D3D12_GPU_DESCRIPTOR_HANDLE m_ObjSrvGPU{};

private: // Variables

    D3D12_INPUT_ELEMENT_DESC m_VertexLayout[2];

    std::unique_ptr<RenderingObject> m_RenderingObject1;
    std::unique_ptr<RenderingObject> m_RenderingObject2;
    std::unique_ptr<RenderingObject> m_StyleObject;
    std::unique_ptr<RenderingObject3D> m_PlaneObject;
    std::unique_ptr<RenderingObject3D> m_CubeObject;

    std::unique_ptr<OnnxPassResources> m_Onnx = nullptr;
    std::unique_ptr<OnnxGPUResources> m_OnnxGPU = nullptr;
    std::unique_ptr<SponzaModel> m_Sponza;

    ComPointer<ID3D12RootSignature>     m_RootSignature;
    ComPointer<ID3D12RootSignature>     m_BlitRS2;
    ComPointer<ID3D12PipelineState>     m_PipelineStateObj;
    ComPointer<ID3D12PipelineState>     m_PsoBlitBackbuffer;
    ComPointer<ID3D12PipelineState>     m_BlitPSO2;
    ComPointer<ID3D12PipelineState>     m_CubePSO;
    ComPointer<ID3D12RootSignature>     m_CubeRootSig;

    ComPointer<ID3D12DescriptorHeap>    m_BlitSrvHeap;
    ComPointer<ID3D12DescriptorHeap>    mHeapCPU;
    ComPointer<ID3D12DescriptorHeap>    mOffscreenRtvHeap;

    ComPointer<ID3D12Resource2> mFSQuadVB;
    ComPointer<ID3D12Resource2> mSceneColor; // R16G16B16A16_FLOAT

    // offscreen Å¸±ê & SRV/RTV
    uint32_t m_Width = 0, m_Height = 0;

    Camera m_Cam;

    ComPointer<ID3D12DescriptorHeap>    m_DsvHeap;
    ComPointer<ID3D12Resource2>         m_Depth;

    ComPointer<ID3D12Resource> m_ShadowMap;
    ComPointer<ID3D12DescriptorHeap> m_ShadowDsvHeap;
    UINT m_ShadowSize = 2048;

    ComPointer<ID3D12RootSignature> m_ShadowRS;
    ComPointer<ID3D12PipelineState>  m_ShadowPSO;
    ComPointer<ID3D12DescriptorHeap> m_ObjSrvHeap2; 

    D3D12_CPU_DESCRIPTOR_HANDLE m_DSV{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowDSV{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_ObjSrvCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_RtvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_RtvBackbuffer{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_SrvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_ResolvedSrvCPU;

    D3D12_GPU_DESCRIPTOR_HANDLE m_ResolvedSrvGPU;

    D3D12_VERTEX_BUFFER_VIEW m_Vbv1;
    D3D12_VERTEX_BUFFER_VIEW m_Vbv2;
    D3D12_VERTEX_BUFFER_VIEW m_FSQuadVBV;

    D3D12_RESOURCE_STATES m_ShadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    D3D12_RESOURCE_STATES m_SceneColorState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_OnnxTexState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_OnnxInputState = D3D12_RESOURCE_STATE_COMMON;

    float m_Angle = 0.f;
    float m_Aspect = 16.f / 9.f;

};

