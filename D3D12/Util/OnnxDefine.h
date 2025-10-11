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
	ReCoNet = 4,
	BlindVideo = 5,
    Sanet = 6,
	E7 = 7,
    E8 = 8,
    E9 = 9,
    E10 = 10,
    E11 = 11,
    E12 = 12,
    E13 = 13,
    E14 = 14,
    E15 = 15,
    E16 = 16,
    E17 = 17,
    E18 = 18,
    E19 = 19,
    E20 = 20,
    E21 = 21,
    E22 = 22,
    E23 = 23,
    E24 = 24,
    E25 = 25,
    E26 = 26,
    E27 = 27,
    E28 = 28,
    E29 = 29,
	E30 = 30,
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

constexpr UINT LINEAR_TO_SRGB = 0x0001; // �̹� ��� ��
constexpr UINT PRE_BGR_SWAP = 0x0010; // �̹� ��� ��
constexpr UINT PRE_MUL_255 = 0x0100; // �̹� ��� ��
constexpr UINT PRE_IMAGENET_MEANSTD = 0x0400; // ���� �߰�
constexpr UINT PRE_CAFFE_BGR_MEAN = 0x0800; // ���� �߰� (VGG/Caffe ��Ÿ��)
constexpr UINT PRE_TANH_INPUT = 0x1000; // ���� �߰� [-1,1] �Է�
constexpr UINT PRE_PT_VALID = 0x2000; // ���� �߰� [-1,1] �Է�
constexpr UINT OUT_TANH = 0x0001; // ��� tanh -> (x+1)/2
constexpr UINT OUT_255 = 0x0002; // ��� 0..255 -> /255