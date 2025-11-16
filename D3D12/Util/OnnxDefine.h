#pragma once
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "d3dx12.h"
#include "d3d12.h"
#include <d3dcompiler.h>
#include <onnxruntime_cxx_api.h>
#include <memory>

enum class OnnxType : short
{
    None            = 0,
    AdaIN           = 2,
    FastNeuralStyle = 3,
	ReCoNet         = 4,
    Sanet           = 6,
    WCT2            = 9,
};


class OnnxPassResources {
public:
    //ComPointer<ID3D12Resource> SceneTex;
    //D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV{};
    //D3D12_CPU_DESCRIPTOR_HANDLE OnnxResultRTV{}; 
    //D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV{};    

    //ComPointer<ID3D12Resource> OnnxTex; 
    //D3D12_GPU_DESCRIPTOR_HANDLE m_OnnxTexUAV{};
    //D3D12_GPU_DESCRIPTOR_HANDLE m_OnnxTexSRV{};

    ComPointer<ID3D12RootSignature> m_PreRS;
    ComPointer<ID3D12PipelineState> m_PrePSO, m_PostPSO;

    UINT m_Width = 0, m_Height = 0;
};


struct OnnxGPUResources{
    // 디스크립터 힙
    ComPointer<ID3D12DescriptorHeap> m_Heap;
    // CB
    ComPointer<ID3D12Resource> m_CB;
    ComPointer<ID3D12Resource2> m_OnnxTex;

    // Scene SRV
    D3D12_CPU_DESCRIPTOR_HANDLE m_SceneSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_SceneSRV_GPU{};

    // Input (content/style) UAV
    D3D12_CPU_DESCRIPTOR_HANDLE m_InputContentUAV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_InputContentUAV_GPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_InputStyleUAV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_InputStyleUAV_GPU{};

    D3D12_CPU_DESCRIPTOR_HANDLE m_InputContentSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_InputContentSRV_GPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_InputStyleSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_InputStyleSRV_GPU{};

    // ModelOut SRV  
    D3D12_CPU_DESCRIPTOR_HANDLE m_ModelOutSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_ModelOutSRV_GPU{};

    // 
    D3D12_CPU_DESCRIPTOR_HANDLE m_StyleSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_StyleSRV_GPU{};

    D3D12_CPU_DESCRIPTOR_HANDLE m_OnnxTexUAV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_OnnxTexUAV_GPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_OnnxTexSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_OnnxTexSRV_GPU{};

    D3D12_GPU_DESCRIPTOR_HANDLE m_InputStyleUAV_GPU_ForClear{};
    D3D12_CPU_DESCRIPTOR_HANDLE m_InputStyleUAV_CPU_ForClear{};

public:
    void Reset() {
        m_OnnxTex.Release();
        m_CB.Release();
        m_Heap.Release();

        *this = {};
    }

};

constexpr UINT LINEAR_TO_SRGB = 0x0001; 
constexpr UINT PRE_BGR_SWAP = 0x0010;
constexpr UINT PRE_MUL_255 = 0x0100;
constexpr UINT PRE_IMAGENET_MEANSTD = 0x0400; 
constexpr UINT PRE_CAFFE_BGR_MEAN = 0x0800;
constexpr UINT PRE_TANH_INPUT = 0x1000; 
constexpr UINT PRE_PT_VALID = 0x2000;
constexpr UINT OUT_TANH = 0x0001; 
constexpr UINT OUT_255 = 0x0002; 