#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Support/ImageLoader.h"
#include "Util/Util.h"
#include "Util/OnnxDefine.h"

#include "Object/RenderingObject.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <d3dcompiler.h>
#include <onnxruntime_cxx_api.h>
#include <memory>

#define DX_MANAGER DirectXManager::Get()

class Shader;

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

    bool CreateOnnxComputePipeline();
private: // Functions
    void InitUploadRenderingObject();
    void InitShader();


    void SetVerticies();
    void SetVertexLayout();

    void UploadTextureBuffer();
    void CreateSRV();
    void UploadCPUResource();

    void InitPipelineSate(Shader& vertexShader, Shader& pixelShader);

    bool CreateOffscreen(uint32_t w, uint32_t h);
    void DestroyOffscreen();

    bool CreateSimpleBlitPipeline();
    void CreateFullscreenQuadVB(UINT w, UINT h);

	//void RecordPreprocess_Udnie(ID3D12GraphicsCommandList7* cmd);
	//void RecordPostprocess_Udnie(ID3D12GraphicsCommandList7* cmd);
    //void CreateOnnxResources_Udnie(UINT W, UINT H);

private: // Variables

    D3D12_INPUT_ELEMENT_DESC m_VertexLayout[2];
    RenderingObject RenderingObject1;
    RenderingObject RenderingObject2;
    RenderingObject m_StyleObject;


    ComPointer<ID3D12RootSignature> m_RootSignature;
    ComPointer<ID3D12PipelineState> m_PipelineStateObj;
    ComPointer<ID3D12PipelineState> m_PsoBlitBackbuffer;

    ComPointer<ID3D12DescriptorHeap> m_BlitSrvHeap;

    std::unique_ptr<OnnxPassResources> m_Onnx = nullptr;
    std::unique_ptr<OnnxGPUResources> m_OnnxGPU = nullptr;

    
    // ������ũ�� Ÿ�� & SRV/RTV
    ComPointer<ID3D12Resource2> mSceneColor; // R16G16B16A16_FLOAT
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvScene{};
    D3D12_CPU_DESCRIPTOR_HANDLE mRtvBackbuffer{};
    D3D12_CPU_DESCRIPTOR_HANDLE mSrvScene{};
    
    D3D12_CPU_DESCRIPTOR_HANDLE mResolvedSrvCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE mResolvedSrvGPU;

    // Readback/Upload ���� (CPU I/O)
    ComPointer<ID3D12Resource2> mFSQuadVB;
    uint32_t m_Width = 0, m_Height = 0;
    
    ComPointer<ID3D12DescriptorHeap> mOffscreenRtvHeap;

    // ���ҽ� ���� ����
    D3D12_RESOURCE_STATES mSceneColorState = D3D12_RESOURCE_STATE_COMMON;

    ComPointer<ID3D12RootSignature> m_BlitRS2;
    ComPointer<ID3D12PipelineState> m_BlitPSO2;

    D3D12_VERTEX_BUFFER_VIEW vbv1;
    D3D12_VERTEX_BUFFER_VIEW vbv2;
    D3D12_VERTEX_BUFFER_VIEW mFSQuadVBV;

    D3D12_RESOURCE_STATES mOnnxTexState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES mOnnxInputState = D3D12_RESOURCE_STATE_COMMON;

private:
    


#pragma region �׽�Ʈ
public:
    bool CreateGreenPipeline();
    void DrawConstantGreen(ID3D12GraphicsCommandList7* cmd);

    void DrawConstantGreen_Standalone();

private: // Variables
    ComPointer<ID3D12RootSignature> m_RS_Green;
    ComPointer<ID3D12PipelineState>  m_PSO_Green;

#pragma endregion


public:
    void WriteSceneSRVToSlot0();
    void WriteStyleSRVToSlot6(ID3D12Resource* styleTex, DXGI_FORMAT fmt);

    void Debug_ShowPreprocessedToScreen(ID3D12GraphicsCommandList7* cmd, bool showContent);
    void Debug_CopyStyleToScreen(ID3D12GraphicsCommandList7* cmd);
    void Debug_DumpOrtOutput(ID3D12GraphicsCommandList7* cmd);
    void Debug_DumpBuffer(ID3D12Resource* src, const char* tag);

private:
    ComPointer<ID3D12PipelineState> m_DebugShowInputPSO;
    ComPointer<ID3D12PipelineState> m_CopyTexToTex2DPSO;
    ComPointer<ID3D12PipelineState> m_FillPSO;
    ComPointer<ID3D12DescriptorHeap> mHeapCPU;
};

