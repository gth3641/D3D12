#pragma once
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
//#include "Support/Window.h"
#include "d3dx12.h"
#include "d3d12.h"
#include <d3dcompiler.h>
#include <onnxruntime_cxx_api.h>
#include <memory>

enum class OnnxType : short
{
    None = 0,
    Udnie = 1,
    AdaIN = 2,
    FastNeuralStyle = 3,
};


class OnnxPassResources {
public:
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


struct OnnxGPUResources{
    // ��ũ���� ��
    ComPointer<ID3D12DescriptorHeap> Heap;
    // CB
    ComPointer<ID3D12Resource> CB;
    ComPointer<ID3D12Resource> OnnxTex;

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

    // OnnxTex UAV/SRV

    D3D12_CPU_DESCRIPTOR_HANDLE OnnxTexUAV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexUAV_GPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE OnnxTexSRV_CPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE OnnxTexSRV_GPU{};

    D3D12_GPU_DESCRIPTOR_HANDLE InputStyleUAV_GPU_ForClear{};
    D3D12_CPU_DESCRIPTOR_HANDLE InputStyleUAV_CPU_ForClear{};;

public:
    void Reset() {
        OnnxTex.Release();
        CB.Release();
        Heap.Release();

        *this = {};
    }

};