#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Util/Util.h"

#include <string>
#include <vector>
#include <wrl.h>
#include <DirectML.h>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

class OnnxRunnerInterface
{

protected:
    // 8�� ����� ����
    inline int AlignDown8(int v) { return (v / 8) * 8; }

    // shape�� ����(-1)�� ���� ũ��� �޿��
    void FillDynamicNCHW(std::vector<int64_t>& s, int N, int C, int H, int W)
    {
        if (s.size() < 4) s = { N, C, H, W };
        if (s[0] < 0) s[0] = N;
        if (s[1] < 0) s[1] = C;
        if (s[2] < 0) s[2] = H;
        if (s[3] < 0) s[3] = W;
    }


public: // Functions
    // ONNX Runtime + DirectML EP �ʱ�ȭ
    virtual bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue) { return false; }

    // ���� �ػ� �غ�(����): content/style�� ���� ����
    virtual bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) { return false; }

    // ȣȯ �����ε�: style = content
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H) { return PrepareIO(dev, W, H, W, H); }

    // ����(���� ���ε��� GPU ���۸� ���)
    virtual bool Run() { return false; }

    // ��������(����)
    virtual void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) {}

    // ȣȯ �����ε�: style = content
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H) { ResizeIO(dev, W, H, W, H); }

    // ����
    virtual void Shutdown() {}

    //===========Getter=================//
    ComPointer<ID3D12Resource> GetOutputBuffer()       const { return m_OutputBuf; }
    ComPointer<ID3D12Resource> GetInputBufferContent() const { return m_InputBufContent; }
    ComPointer<ID3D12Resource> GetInputBufferStyle()   const { return m_InputBufStyle; }
    const std::vector<int64_t>& GetOutputShape()       const { return m_OutShape; }
    const std::vector<int64_t>& GetInputShapeContent() const { return m_InShapeContent; }
    const std::vector<int64_t>& GetInputShapeStyle()   const { return m_InShapeStyle; }
    //==================================//

    virtual void AllocateOutputForShape(const std::vector<int64_t>& shape) {}
protected: // Functions
    inline uint64_t BytesOf(const std::vector<int64_t>& shape, size_t elemBytes)
    {
        uint64_t n = 1;
        for (auto d : shape)
            n *= static_cast<uint64_t>(d > 0 ? d : 1); // ����(-1) ������ �ӽ÷� 1�� ���
        return n * static_cast<uint64_t>(elemBytes);
    }
protected: // Variables

    // ORT/DML
    Ort::Env           m_Env{ ORT_LOGGING_LEVEL_WARNING, "app" };
    Ort::SessionOptions m_So;
    std::unique_ptr<Ort::Session> m_Session;
    const OrtDmlApi* m_DmlApi = nullptr;

    // DML �޸� ���� (GPU)
    Ort::MemoryInfo miDml_{ nullptr }; // Init���� "DML"�� ä��

    // ����̽�/ť(���� ���� ����)
    ComPointer<ID3D12Device>        m_Dev;
    ComPointer<ID3D12CommandQueue>  m_Queue;

protected:
    // �� IO �̸�
    std::string m_InNameContent; // input[0] : "content"
    std::string m_InNameStyle;   // input[1] : "style"
    std::string m_OutName;       // output[0]: "stylized"

    // �� IO shape (������ �� ����; PrepareIO �� Ȯ��ġ�� ����)
    std::vector<int64_t> m_InShapeContent; // ���� [-1,3,-1,-1] �� [1,3,Hc,Wc]
    std::vector<int64_t> m_InShapeStyle;   // ���� [-1,3,-1,-1] �� [1,3,Hs,Ws]
    std::vector<int64_t> m_OutShape;       // ���� [-1,3,-1,-1] �� [1,3,Ho,Wo] (Ho/Wo�� 8�� ���)

    // GPU ���� + DML allocation �ڵ�
    ComPointer<ID3D12Resource> m_InputBufContent; // content NCHW FP32
    ComPointer<ID3D12Resource> m_InputBufStyle;   // style   NCHW FP32
    ComPointer<ID3D12Resource> m_OutputBuf;       // output  NCHW FP32
    void* m_InAllocContent = nullptr; // DML GPU allocation handle
    void* m_InAllocStyle = nullptr;
    void* m_OutAlloc = nullptr;

    // ����Ʈ ũ��
    UINT64 m_InBytesContent = 0;
    UINT64 m_InBytesStyle = 0;
    UINT64 m_OutBytes = 0;


};

