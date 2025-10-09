#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Support/ImageLoader.h"
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

class Shader;

using namespace DirectX;



struct Camera {
    XMFLOAT3 pos{ 0, 2.0f, -6.0f };     // 월드 위치
    XMFLOAT3 dir{ 0, 0,  1.0f };        // 바라보는 “방향”(정규화 권장, LH 기준 +Z가 앞)
    XMFLOAT3 up{ 0, 1.0f, 0 };          // 보통 월드업
    float fovY = XM_PIDIV4;
    float nearZ = 0.1f;
    float farZ = 500.0f;
    float yaw = 0.0f;                        // +Y축 기준 회전
    float pitch = 0.0f;                        // X축 기준 회전(위/아래)
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

public: // Functions
    bool Init();
    void Shutdown();
    void Update(float deltaTime);
    void RenderCube(ID3D12GraphicsCommandList7* cmd); 
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

    void OnChangedMouseLock();

    bool CreateOnnxComputePipeline();

    //===========Getter=================//
    D3D12_INPUT_ELEMENT_DESC* GetVertexLayout() { return m_VertexLayout; }
    int GetVertexLayoutCount() { return _countof(m_VertexLayout); }

    RenderingObject& GetRenderingObject1() { return RenderingObject1; }
    RenderingObject& GetRenderingObject2() { return RenderingObject2; }

    ComPointer<ID3D12RootSignature>& GetRootSignature() { return m_RootSignature; }
    ComPointer<ID3D12PipelineState>& GetPipelineStateObj() { return m_PipelineStateObj; }
    //==================================//
private: // Functions
    void InitUploadRenderingObject();
    void InitGeometry();
    void InitShader();


    void SetVerticies();
    void SetVertexLayout();

    void UploadTextureBuffer();
    void CreateSRV();
    //void UploadCPUResource();

    void InitPipelineSate(Shader& vertexShader, Shader& pixelShader);

    bool CreateOffscreen(uint32_t w, uint32_t h);
    void DestroyOffscreen();

    bool CreateSimpleBlitPipeline();
    void CreateFullscreenQuadVB(UINT w, UINT h);

private: // Variables

    D3D12_INPUT_ELEMENT_DESC m_VertexLayout[2];

    RenderingObject RenderingObject1;
    RenderingObject RenderingObject2;
    RenderingObject m_StyleObject;
    RenderingObject3D m_PlaneObject;
    RenderingObject3D m_CubeObject;


    ComPointer<ID3D12RootSignature> m_RootSignature;
    ComPointer<ID3D12RootSignature> m_BlitRS2;
    ComPointer<ID3D12PipelineState> m_PipelineStateObj;
    ComPointer<ID3D12PipelineState> m_PsoBlitBackbuffer;
    ComPointer<ID3D12PipelineState> m_BlitPSO2;


    std::unique_ptr<OnnxPassResources> m_Onnx = nullptr;
    std::unique_ptr<OnnxGPUResources> m_OnnxGPU = nullptr;

    ComPointer<ID3D12DescriptorHeap> m_BlitSrvHeap;
    ComPointer<ID3D12DescriptorHeap> mHeapCPU;
    ComPointer<ID3D12DescriptorHeap> mOffscreenRtvHeap;

    // 오프스크린 타깃 & SRV/RTV
    uint32_t m_Width = 0, m_Height = 0;

    ComPointer<ID3D12Resource2> mFSQuadVB;
    ComPointer<ID3D12Resource2> mSceneColor; // R16G16B16A16_FLOAT

    D3D12_CPU_DESCRIPTOR_HANDLE mRtvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvBackbuffer{};
    D3D12_CPU_DESCRIPTOR_HANDLE mSrvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE mResolvedSrvCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE mResolvedSrvGPU;

    // 리소스 상태 추적
    D3D12_VERTEX_BUFFER_VIEW vbv1;
    D3D12_VERTEX_BUFFER_VIEW vbv2;
    D3D12_VERTEX_BUFFER_VIEW mFSQuadVBV;

    D3D12_RESOURCE_STATES mSceneColorState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxInputState = D3D12_RESOURCE_STATE_COMMON;

#pragma region 테스트
public:
    bool CreateGreenPipeline();
    void DrawConstantGreen(ID3D12GraphicsCommandList7* cmd);

    void DrawConstantGreen_Standalone();

private: // Variables
    ComPointer<ID3D12RootSignature> m_RS_Green;
    ComPointer<ID3D12PipelineState>  m_PSO_Green;

#pragma endregion

public:
    void Debug_DumpOrtOutput(ID3D12GraphicsCommandList7* cmd);
    void Debug_DumpBuffer(ID3D12Resource* src, const char* tag);

// Render Cube
private:// Functions
    bool InitCubePipeline();     // RS/PSO/셰이더
    //bool InitCubeGeometry();     // VB/IB
    bool InitDepth(UINT w, UINT h);
    void DestroyDepth();

private: // Variables
    // PSO / RootSig
    ComPointer<ID3D12PipelineState>     mCubePSO;
    ComPointer<ID3D12RootSignature>     mCubeRootSig;

    // 버퍼들
    ComPointer<ID3D12Resource2>         mVB;
    ComPointer<ID3D12Resource2>         mIB;
    D3D12_VERTEX_BUFFER_VIEW            mVBV{};
    D3D12_INDEX_BUFFER_VIEW             mIBV{};
    UINT                                mIndexCount = 0;

    // 깊이버퍼 + DSV 힙
    ComPointer<ID3D12DescriptorHeap>    mDsvHeap;
    ComPointer<ID3D12Resource2>         mDepth;
    D3D12_CPU_DESCRIPTOR_HANDLE         mDSV{};

    // 뷰/프로젝션
    float mAngle = 0.f;
    float mAspect = 16.f / 9.f;


// Render Cam
public:
    // === Ground geometry ===
    //bool InitGroundGeometry();
   // void RenderGround(ID3D12GraphicsCommandList7* cmd);
private:

    Camera mCam;

    //ComPointer<ID3D12Resource2>  mGroundVB;
    //ComPointer<ID3D12Resource2>  mGroundIB;
    //D3D12_VERTEX_BUFFER_VIEW     mGroundVBV{};
    //D3D12_INDEX_BUFFER_VIEW      mGroundIBV{};
    //UINT                         mGroundIndexCount = 0;

};

