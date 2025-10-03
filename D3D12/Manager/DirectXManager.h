#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Support/ImageLoader.h"
#include "Util/Util.h"

#include "Object/RenderingObject.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <d3dcompiler.h>

#include <onnxruntime_cxx_api.h>

#define DX_MANAGER DirectXManager::Get()

class Shader;

struct OnnxPassResources {
    // 1) 오프스크린 씬 텍스처 (RTV+SRV)
    ComPointer<ID3D12Resource> SceneTex;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE OnnxResultRTV{}; // 옵션(후처리용)
    D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV{};      // 전처리 입력

    // 2) 결과 뿌릴 텍스처 (UAV+SRV)
    ComPointer<ID3D12Resource> OnnxTex; // 최종 화면용 RGBA8
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexUAV{};
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexSRV{};

    // 3) 전처리/후처리용 PSO/RootSig
    ComPointer<ID3D12RootSignature> PreRS;// , PostRS;
    ComPointer<ID3D12PipelineState> PrePSO, PostPSO;

    UINT Width = 0, Height = 0;
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
    void Update();
    void UploadGPUResource(ID3D12GraphicsCommandList7* cmdList);

    //===========Getter=================//
    D3D12_INPUT_ELEMENT_DESC* GetVertexLayout() { return m_VertexLayout; }
    int GetVertexLayoutCount() { return _countof(m_VertexLayout); }

    RenderingObject& GetRenderingObject1() { return RenderingObject1; }
    RenderingObject& GetRenderingObject2() { return RenderingObject2; }

    ComPointer<ID3D12RootSignature>& GetRootSignature() { return m_RootSignature; }
    ComPointer<ID3D12PipelineState>& GetPipelineStateObj() { return m_PipelineStateObj; }
    //==================================//

    void CreateOnnxResources(UINT W, UINT H);
    void ResizeOnnxResources(UINT W, UINT H);

    void Resize();
    void RenderOffscreen(ID3D12GraphicsCommandList7* cmd);
    void BlitToBackbuffer(ID3D12GraphicsCommandList7* cmd);

    void InitBlitPipeline();
    void RecordPreprocess(ID3D12GraphicsCommandList7* cmd);
    void RecordPostprocess(ID3D12GraphicsCommandList7* cmd);

private: // Functions
    void InitUploadRenderingObject();
    void InitShader();

    bool CreateOnnxComputePipeline();

    void SetVerticies();
    void SetVertexLayout();

    void UploadTextureBuffer();
    void CreateSRV();
    void UploadCPUResource();

    void InitPipelineSate(Shader& vertexShader, Shader& pixelShader);

    bool CreateOffscreen(uint32_t w, uint32_t h);
    void DestroyOffscreen();


    void RunOnnxGPU();

    bool CreateSimpleBlitPipeline();
    void CreateFullscreenQuadVB(UINT w, UINT h);

private: // Variables

    D3D12_INPUT_ELEMENT_DESC m_VertexLayout[2];
    RenderingObject RenderingObject1;
    RenderingObject RenderingObject2;

    ComPointer<ID3D12RootSignature> m_RootSignature;
    ComPointer<ID3D12PipelineState> m_PipelineStateObj;
    ComPointer<ID3D12PipelineState> m_PsoBlitBackbuffer;

    ComPointer<ID3D12DescriptorHeap> m_BlitSrvHeap;

    OnnxPassResources onnx_;
    
    // 오프스크린 타깃 & SRV/RTV
    ComPointer<ID3D12Resource2> mSceneColor; // R16G16B16A16_FLOAT
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvBackbuffer{};
    D3D12_CPU_DESCRIPTOR_HANDLE mSrvScene{};
    
    D3D12_CPU_DESCRIPTOR_HANDLE mResolvedSrvCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE mResolvedSrvGPU;

    // Readback/Upload 버퍼 (CPU I/O)
    ComPointer<ID3D12Resource2> mFSQuadVB;
    uint32_t m_Width = 0, m_Height = 0;
    
    ComPointer<ID3D12DescriptorHeap> mOffscreenRtvHeap;

    // 리소스 상태 추적
    D3D12_RESOURCE_STATES mSceneColorState = D3D12_RESOURCE_STATE_COMMON;

    ComPointer<ID3D12RootSignature> m_BlitRS2;
    ComPointer<ID3D12PipelineState> m_BlitPSO2;

    D3D12_VERTEX_BUFFER_VIEW vbv1;
    D3D12_VERTEX_BUFFER_VIEW vbv2;
    D3D12_VERTEX_BUFFER_VIEW mFSQuadVBV;

    D3D12_RESOURCE_STATES mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxInputState = D3D12_RESOURCE_STATE_COMMON;

private:
    struct {
        // 전/후처리 공용 힙 (SRV+UAV 몇 개)
        ComPointer<ID3D12DescriptorHeap> Heap;   // shader visible, 4~5개 슬롯 정도

        // SceneColor -> SRV (t0, Preprocess에서 읽음)
        D3D12_CPU_DESCRIPTOR_HANDLE SceneSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV_GPU{};

        // NCHW float 버퍼 (UAV로 씀/읽음)
        D3D12_CPU_DESCRIPTOR_HANDLE      InputUAV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      InputUAV_GPU{};

        // 최종 출력 텍스처 (RGBA8, UAV/ SRV 둘 다)
        ComPointer<ID3D12Resource>       OnnxTex;    
        CD3DX12_CPU_DESCRIPTOR_HANDLE    OnnxTexUAV_CPU{};
        CD3DX12_GPU_DESCRIPTOR_HANDLE    OnnxTexUAV_GPU{};
        D3D12_CPU_DESCRIPTOR_HANDLE      OnnxTexSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      OnnxTexSRV_GPU{};

        D3D12_CPU_DESCRIPTOR_HANDLE      ModelOutSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      ModelOutSRV_GPU{};

        ComPointer<ID3D12Resource>       CB;
    } mOnnxGPU;


#pragma region 테스트
public:
    bool CreateGreenPipeline();
    void DrawConstantGreen(ID3D12GraphicsCommandList7* cmd);

    void DrawConstantGreen_Standalone();

private: // Variables
    ComPointer<ID3D12RootSignature> m_RS_Green;
    ComPointer<ID3D12PipelineState>  m_PSO_Green;

#pragma endregion

};

