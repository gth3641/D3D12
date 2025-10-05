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

#define DX_ONNX OnnxManager::Get()


class OnnxManager
{
public: // Singleton pattern to ensure only one instance exists 
    OnnxManager(const OnnxManager&) = delete;
    OnnxManager& operator=(const OnnxManager&) = delete;

    inline static OnnxManager& Get()
    {
        static OnnxManager instance;
        return instance;
    }

public:
    OnnxManager() = default;

public: // Static & Override
  
public: // Functions
    // ONNX Runtime + DirectML EP �ʱ�ȭ
    bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);

    // ���� �ػ� �غ�(����): content/style�� ���� ����
    bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);

    // ȣȯ �����ε�: style = content
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H) { return PrepareIO(dev, W, H, W, H); }

    // ����(���� ���ε��� GPU ���۸� ���)
    bool Run();

    // ��������(����)
    void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);

    // ȣȯ �����ε�: style = content
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H) { ResizeIO(dev, W, H, W, H); }

    // ����
    void Shutdown();

    //===========Getter=================//
    // �� ����(�����): content/style �Է� ���ۿ� shape
    ComPointer<ID3D12Resource> GetContentInputBuffer() const { return m_InputBufContent; }
    ComPointer<ID3D12Resource> GetStyleInputBuffer()   const { return m_InputBufStyle; }
    ComPointer<ID3D12Resource> GetOutputBuffer()       const { return m_OutputBuf; }

    const std::vector<int64_t>& GetContentInputShape() const { return m_InShapeContent; }
    const std::vector<int64_t>& GetStyleInputShape()   const { return m_InShapeStyle; }
    const std::vector<int64_t>& GetOutputShape()       const { return m_OutShape; }

    // ���� �ڵ� ȣȯ�� alias(����� content �Է��� ����Ŵ)
    ComPointer<ID3D12Resource> GetInputBuffer()  const { return m_InputBufContent; }
    const std::vector<int64_t>& GetInputShape()  const { return m_InShapeContent; }

    ComPointer<ID3D12Resource> GetInputBufferContent() const { return m_InputBufContent; }
    ComPointer<ID3D12Resource> GetInputBufferStyle()   const { return m_InputBufStyle; }
    const std::vector<int64_t>& GetInputShapeContent() const { return m_InShapeContent; }
    const std::vector<int64_t>& GetInputShapeStyle()   const { return m_InShapeStyle; }
    //==================================//


    bool RunCPUOnce_Debug();
    bool Run_FillInputsHalf_Debug();

private: // Functions
    void AllocateOutputForShape(const std::vector<int64_t>& shape);
    inline uint64_t BytesOf(const std::vector<int64_t>& shape, size_t elemBytes)
    {
        uint64_t n = 1;
        for (auto d : shape)
            n *= static_cast<uint64_t>(d > 0 ? d : 1); // ����(-1) ������ �ӽ÷� 1�� ���
        return n * static_cast<uint64_t>(elemBytes);
    }
private: // Variables

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

