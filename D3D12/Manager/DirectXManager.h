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
    // 1) ������ũ�� �� �ؽ�ó (RTV+SRV)
    ComPointer<ID3D12Resource> SceneTex;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE OnnxResultRTV{}; // �ɼ�(��ó����)
    D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV{};      // ��ó�� �Է�

    // 2) ��� �Ѹ� �ؽ�ó (UAV+SRV)
    ComPointer<ID3D12Resource> OnnxTex; // ���� ȭ��� RGBA8
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexUAV{};
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexSRV{};

    // 3) ��ó��/��ó���� PSO/RootSig
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

    // ��� ���� ����
    std::vector<uint8_t> mOnnxRGBA;

    // (����) ���ҽ� ���� ����
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
    
    // ������ũ�� Ÿ�� & SRV/RTV
    ComPointer<ID3D12Resource2> mSceneColor; // R16G16B16A16_FLOAT
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvBackbuffer{}; // �ӽ� (��Ÿ�ӿ��� ����)
    D3D12_CPU_DESCRIPTOR_HANDLE mSrvScene{};
    
    D3D12_CPU_DESCRIPTOR_HANDLE mResolvedSrvCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE mResolvedSrvGPU;

    // PostTex (���⼭�� �����ϰ� �ٷ� ��)
    //ComPointer<ID3D12Resource2> mResolved; // R8G8B8A8_UNORM for blit
    D3D12_CPU_DESCRIPTOR_HANDLE mSrvResolved{};
    
    // ��ũ���� ��
    UINT mSrvIncr = 0;
    UINT mRtvIncr = 0;
    
    // Readback/Upload ���� (CPU I/O)
    ComPointer<ID3D12Resource2> mFSQuadVB;
    ComPointer<ID3D12Resource2> mReadback; // SceneColor �� CPU
    ComPointer<ID3D12Resource2> mUpload;   // CPU �� Resolved
    uint32_t mWidth = 0, mHeight = 0;
    
    // ONNX
    std::unique_ptr<class OnnxRunner> mOnnx;
    std::string mOnnxPath;

    ComPointer<ID3D12DescriptorHeap> mOffscreenRtvHeap;

    public:
    D3D12_VERTEX_BUFFER_VIEW vbv1;
    D3D12_VERTEX_BUFFER_VIEW vbv2;
    D3D12_VERTEX_BUFFER_VIEW mFSQuadVBV;


    // === Compute ��/��ó�� ===
    ComPointer<ID3D12RootSignature> onnx_PreRS;      // �� ����: �̸� ����
    ComPointer<ID3D12PipelineState>  onnx_PrePSO;     // ��ó�� CS
    ComPointer<ID3D12PipelineState>  onnx_PostPSO;    // ��ó�� CS

    // �Է�/��� ����(DirectML/ONNX��)
    ComPointer<ID3D12Resource> mOnnxInputBuf;         // �� �߰�: UAV StructuredBuffer<float>
    ComPointer<ID3D12Resource> mOnnxOutputBuf;        // �� �߰�: UAV StructuredBuffer<float> (DML�� ��)

    // ���� �ռ��� �ؽ�ó(���� mResolved�� UAV �����ϰ� �ٲ� ���)
    ComPointer<ID3D12Resource> mResolved;
    D3D12_RESOURCE_STATES       mResolvedState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxInputState{ D3D12_RESOURCE_STATE_COMMON };

    // ��ǻƮ ���� SRV/UAV ��
    ComPointer<ID3D12DescriptorHeap> mCSHeap;         // �� �߰�: shader-visible, SRV/UAV ����
    // ���� �ε��� ����(���� ����)
    enum { kSlot_SceneSRV = 0, kSlot_InputUAV = 1, kSlot_OutputSRV = 2, kSlot_ResolvedUAV = 3 };
    D3D12_CPU_DESCRIPTOR_HANDLE mCS_CPU[4]{};         // �� �߰�
    D3D12_GPU_DESCRIPTOR_HANDLE mCS_GPU[4]{};

    // DirectXManager.h (private:)

    // �� �÷��� SRV, mResolved�� SRV�� ���� m_BlitSrvHeap�� ����
    // (m_BlitSrvHeap�� �׷��Ƚ� �� ���� ������ ����)

    // === ONNX GPU I/O & CS�� ��ũ����, CB ===
    struct {
        // root sig / PSO�� �װ� �̹� �������: PreRS, PrePSO, PostPSO

        // ��/��ó�� ���� �� (SRV+UAV �� ��)
        ComPointer<ID3D12DescriptorHeap> Heap;   // shader visible, 4~5�� ���� ����

        // SceneColor -> SRV (t0, Preprocess���� ����)
        D3D12_CPU_DESCRIPTOR_HANDLE SceneSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV_GPU{};

        // NCHW float ���� (UAV�� ��/����)
        ComPointer<ID3D12Resource>       InputNCHW;  // RWStructuredBuffer<float>
        ComPointer<ID3D12Resource>       OutputNCHW;  // RWStructuredBuffer<float>
        D3D12_CPU_DESCRIPTOR_HANDLE      InputUAV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      InputUAV_GPU{};
        D3D12_CPU_DESCRIPTOR_HANDLE      InputSRV_CPU{}; // (����Ʈ���� SRV�� ���� ��)
        D3D12_GPU_DESCRIPTOR_HANDLE      InputSRV_GPU{};

        // ���� ��� �ؽ�ó (RGBA8, UAV/ SRV �� ��)
        ComPointer<ID3D12Resource>       OnnxTex;    // RWTexture2D<unorm float4>
        CD3DX12_CPU_DESCRIPTOR_HANDLE      OnnxTexUAV_CPU{};
        CD3DX12_GPU_DESCRIPTOR_HANDLE      OnnxTexUAV_GPU{};
        D3D12_CPU_DESCRIPTOR_HANDLE      OnnxTexSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      OnnxTexSRV_GPU{};

        D3D12_CPU_DESCRIPTOR_HANDLE      ModelOutSRV_CPU{};
        D3D12_GPU_DESCRIPTOR_HANDLE      ModelOutSRV_GPU{};

        // ��/��ó���� CB (���ε� ����, 256 ����)
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

