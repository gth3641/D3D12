#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Support/ImageLoader.h"
#include "Util/Util.h"

#include "Object/RenderingObject.h"



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

    //ComPointer<ID3D12DescriptorHeap> m_Srvheap;

    OnnxPassResources onnx_;

};

