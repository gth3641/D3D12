#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Support/ImageLoader.h"
#include "Util/Util.h"

#include "Object/RenderingObject.h"
#include "Interface/OnnxRunner.h"

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
    void InitShader();
    void Shutdown();

    void UploadGPUResource(ID3D12GraphicsCommandList7* cmdList);

    //===========Getter=================//
    D3D12_INPUT_ELEMENT_DESC* GetVertexLayout() { return m_VertexLayout; }
    int GetVertexLayoutCount() { return _countof(m_VertexLayout); }

    RenderingObject& GetRenderingObject1() { return RenderingObject1; }
    RenderingObject& GetRenderingObject2() { return RenderingObject2; }

    ComPointer<ID3D12RootSignature>& GetRootSignature() { return m_RootSignature; }
    ComPointer<ID3D12PipelineState>& GetPipelineStateObj() { return m_PipelineStateObj; }
    //ComPointer<ID3D12DescriptorHeap>& GetSrvheap() { return m_Srvheap; }

    //D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const UINT64 index);
    //D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const UINT64 index);
    //==================================//

    void CreateOnnxResources(UINT W, UINT H);
    void ResizeOnnxResources(UINT W, UINT H);
    void RecordOnnxPass(ID3D12GraphicsCommandList* cmd);

    bool CreateOnnxComputePipeline();


    void BeginFrame();
    void Resize();
    void RenderOffscreen(ID3D12GraphicsCommandList7* cmd);
    bool RunOnnx(const std::string& onnxPath);
    void BlitToBackbuffer(ID3D12GraphicsCommandList7* cmd);

    bool RunOnnxCPUOnly(const std::string& onnxPath);
    void UploadOnnxResult(ID3D12GraphicsCommandList7* cmd);

    void InitBlitPipeline();

    // 결과 보관 버퍼
    std::vector<uint8_t> mOnnxRGBA;

    // (선택) 리소스 상태 추적
    D3D12_RESOURCE_STATES mSceneColorState = D3D12_RESOURCE_STATE_COMMON;
    //D3D12_RESOURCE_STATES mResolvedState = D3D12_RESOURCE_STATE_COMMON;

private: // Functions
    void SetVerticies();
    void SetVertexLayout();

    void InitUploadRenderingObject();

    void UploadTextureBuffer();
    void CreateDescriptorHipForTexture();
    void CreateSRV();
    void UploadCPUResource();

    void InitPipelineSate(Shader& vertexShader, Shader& pixelShader);



private: // Variables

    D3D12_INPUT_ELEMENT_DESC m_VertexLayout[2];
    RenderingObject RenderingObject1;
    RenderingObject RenderingObject2;

    ComPointer<ID3D12RootSignature> m_RootSignature;
    ComPointer<ID3D12PipelineState> m_PipelineStateObj;
    ComPointer<ID3D12PipelineState> m_PsoBlitBackbuffer;

    //ComPointer<ID3D12DescriptorHeap> m_Srvheap;
    ComPointer<ID3D12DescriptorHeap> m_BlitSrvHeap;

    OnnxPassResources onnx_;

    bool CreateOffscreen(uint32_t w, uint32_t h);
    void DestroyOffscreen();
    
    // 오프스크린 타깃 & SRV/RTV
    ComPointer<ID3D12Resource2> mSceneColor; // R16G16B16A16_FLOAT
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvBackbuffer{}; // 임시 (런타임에서 얻음)
    D3D12_CPU_DESCRIPTOR_HANDLE mSrvScene{};
    
    D3D12_CPU_DESCRIPTOR_HANDLE mResolvedSrvCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE mResolvedSrvGPU;

    // PostTex (여기서는 생략하고 바로 블릿)
    //ComPointer<ID3D12Resource2> mResolved; // R8G8B8A8_UNORM for blit
    D3D12_CPU_DESCRIPTOR_HANDLE mSrvResolved{};
    
    // 디스크립터 힙
    UINT mSrvIncr = 0;
    UINT mRtvIncr = 0;
    
    // Readback/Upload 버퍼 (CPU I/O)
    ComPointer<ID3D12Resource2> mFSQuadVB;
    ComPointer<ID3D12Resource2> mReadback; // SceneColor → CPU
    ComPointer<ID3D12Resource2> mUpload;   // CPU → Resolved
    uint32_t mWidth = 0, mHeight = 0;
    
    // ONNX
    std::unique_ptr<class OnnxRunner> mOnnx;
    std::string mOnnxPath;

    ComPointer<ID3D12DescriptorHeap> mOffscreenRtvHeap;

    public:
    D3D12_VERTEX_BUFFER_VIEW vbv1;
    D3D12_VERTEX_BUFFER_VIEW vbv2;
    D3D12_VERTEX_BUFFER_VIEW mFSQuadVBV;


    // === Compute 전/후처리 ===
    ComPointer<ID3D12RootSignature> onnx_PreRS;      // ★ 변경: 이름 통일
    ComPointer<ID3D12PipelineState>  onnx_PrePSO;     // 전처리 CS
    ComPointer<ID3D12PipelineState>  onnx_PostPSO;    // 후처리 CS

    // 입력/출력 버퍼(DirectML/ONNX용)
    ComPointer<ID3D12Resource> mOnnxInputBuf;         // ★ 추가: UAV StructuredBuffer<float>
    ComPointer<ID3D12Resource> mOnnxOutputBuf;        // ★ 추가: UAV StructuredBuffer<float> (DML이 씀)

    // 최종 합성용 텍스처(기존 mResolved를 UAV 가능하게 바꿔 사용)
    ComPointer<ID3D12Resource> mResolved;
    D3D12_RESOURCE_STATES       mResolvedState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxInputState{ D3D12_RESOURCE_STATE_COMMON };

    // 컴퓨트 전용 SRV/UAV 힙
    ComPointer<ID3D12DescriptorHeap> mCSHeap;         // ★ 추가: shader-visible, SRV/UAV 모음
    // 슬롯 인덱스 고정(보기 쉽게)
    enum { kSlot_SceneSRV = 0, kSlot_InputUAV = 1, kSlot_OutputSRV = 2, kSlot_ResolvedUAV = 3 };
    D3D12_CPU_DESCRIPTOR_HANDLE mCS_CPU[4]{};         // ★ 추가
    D3D12_GPU_DESCRIPTOR_HANDLE mCS_GPU[4]{};

    // DirectXManager.h (private:)

    // 씬 컬러의 SRV, mResolved의 SRV는 기존 m_BlitSrvHeap에 있음
    // (m_BlitSrvHeap는 그래픽스 블릿 전용 힙으로 유지)

    // === ONNX GPU I/O & CS용 디스크립터, CB ===
    struct {
        // root sig / PSO는 네가 이미 만들었음: PreRS, PrePSO, PostPSO

        // 전/후처리 공용 힙 (SRV+UAV 몇 개)
        ComPointer<ID3D12DescriptorHeap> Heap;   // shader visible, 4~5개 슬롯 정도

        // SceneColor -> SRV (t0, Preprocess에서 읽음)
        D3D12_CPU_DESCRIPTOR_HANDLE SceneSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV_GPU{};

        // NCHW float 버퍼 (UAV로 씀/읽음)
        ComPointer<ID3D12Resource>       InputNCHW;  // RWStructuredBuffer<float>
        ComPointer<ID3D12Resource>       OutputNCHW;  // RWStructuredBuffer<float>
        D3D12_CPU_DESCRIPTOR_HANDLE      InputUAV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      InputUAV_GPU{};
        D3D12_CPU_DESCRIPTOR_HANDLE      InputSRV_CPU{}; // (포스트에서 SRV로 읽을 때)
        D3D12_GPU_DESCRIPTOR_HANDLE      InputSRV_GPU{};

        // 최종 출력 텍스처 (RGBA8, UAV/ SRV 둘 다)
        ComPointer<ID3D12Resource>       OnnxTex;    // RWTexture2D<unorm float4>
        CD3DX12_CPU_DESCRIPTOR_HANDLE      OnnxTexUAV_CPU{};
        CD3DX12_GPU_DESCRIPTOR_HANDLE      OnnxTexUAV_GPU{};
        D3D12_CPU_DESCRIPTOR_HANDLE      OnnxTexSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      OnnxTexSRV_GPU{};

        D3D12_CPU_DESCRIPTOR_HANDLE      ModelOutSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      ModelOutSRV_GPU{};

        // 전/후처리용 CB (업로드 버퍼, 256 정렬)
        ComPointer<ID3D12Resource>       CB;
    } mOnnxGPU;


    void RecordPreprocess(ID3D12GraphicsCommandList7* cmd);
    void RecordPostprocess(ID3D12GraphicsCommandList7* cmd);
    void RunOnnxGPU();
    void CreateFullscreenQuadVB(UINT w, UINT h);

    ComPointer<ID3D12RootSignature> m_BlitRS2;
    ComPointer<ID3D12PipelineState>  m_BlitPSO2;

    bool CreateSimpleBlitPipeline();
    void DebugFillOnnxTex(ID3D12GraphicsCommandList7* cmd);

    ComPointer<ID3D12RootSignature> m_RS_Green;
    ComPointer<ID3D12PipelineState>  m_PSO_Green;
    bool CreateGreenPipeline();
    void DrawConstantGreen(ID3D12GraphicsCommandList7* cmd);

    void DrawConstantGreen_Standalone();
};

