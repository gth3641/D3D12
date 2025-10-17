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
    Udnie           = 1,
    AdaIN           = 2,
    FastNeuralStyle = 3,
	ReCoNet         = 4,
	BlindVideo      = 5,
    Sanet           = 6,
    AdaAttN         = 7,
    MsgNet          = 8,
    WCT2            = 9,
};


class OnnxPassResources {
public:
    // 1) 오프스크린 씬 텍스처 (RTV+SRV)
    ComPointer<ID3D12Resource> SceneTex;
    D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV{};
    D3D12_CPU_DESCRIPTOR_HANDLE OnnxResultRTV{}; 
    D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV{};    

    // 2) 결과 뿌릴 텍스처 (UAV+SRV)
    ComPointer<ID3D12Resource> OnnxTex; 
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexUAV{};
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexSRV{};

    // 3) 전처리/후처리용 PSO/RootSig
    ComPointer<ID3D12RootSignature> PreRS;// , PostRS;
    ComPointer<ID3D12PipelineState> PrePSO, PostPSO;

    UINT Width = 0, Height = 0;
};


struct OnnxGPUResources{
    // 디스크립터 힙
    ComPointer<ID3D12DescriptorHeap> Heap;
    // CB
    ComPointer<ID3D12Resource> CB;
    ComPointer<ID3D12Resource2> OnnxTex;

    // Scene SRV
    D3D12_CPU_DESCRIPTOR_HANDLE SceneSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE SceneSRV_GPU{};

    // Input (content/style) UAV
    D3D12_CPU_DESCRIPTOR_HANDLE InputContentUAV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE InputContentUAV_GPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE InputStyleUAV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE InputStyleUAV_GPU{};

    D3D12_CPU_DESCRIPTOR_HANDLE InputContentSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE InputContentSRV_GPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE InputStyleSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE InputStyleSRV_GPU{};

    // ModelOut SRV  
    D3D12_CPU_DESCRIPTOR_HANDLE ModelOutSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE ModelOutSRV_GPU{};

    // 
    D3D12_CPU_DESCRIPTOR_HANDLE StyleSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE StyleSRV_GPU{};

    D3D12_CPU_DESCRIPTOR_HANDLE PtSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE PtSRV_GPU{};

    D3D12_CPU_DESCRIPTOR_HANDLE DummySRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE DummySRV_GPU{};

    // OnnxTex UAV/SRV

    D3D12_CPU_DESCRIPTOR_HANDLE OnnxTexUAV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexUAV_GPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE OnnxTexSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexSRV_GPU{};

    D3D12_GPU_DESCRIPTOR_HANDLE InputStyleUAV_GPU_ForClear{};
    D3D12_CPU_DESCRIPTOR_HANDLE InputStyleUAV_CPU_ForClear{};

public:
    void Reset() {
        OnnxTex.Release();
        CB.Release();
        Heap.Release();

        *this = {};
    }

};

constexpr UINT LINEAR_TO_SRGB = 0x0001; // 이미 사용 중
constexpr UINT PRE_BGR_SWAP = 0x0010; // 이미 사용 중
constexpr UINT PRE_MUL_255 = 0x0100; // 이미 사용 중
constexpr UINT PRE_IMAGENET_MEANSTD = 0x0400; // 새로 추가
constexpr UINT PRE_CAFFE_BGR_MEAN = 0x0800; // 새로 추가 (VGG/Caffe 스타일)
constexpr UINT PRE_TANH_INPUT = 0x1000; // 새로 추가 [-1,1] 입력
constexpr UINT PRE_PT_VALID = 0x2000; // 새로 추가 [-1,1] 입력
constexpr UINT OUT_TANH = 0x0001; // 출력 tanh -> (x+1)/2
constexpr UINT OUT_255 = 0x0002; // 출력 0..255 -> /255