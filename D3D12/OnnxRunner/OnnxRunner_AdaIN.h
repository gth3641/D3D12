#pragma once

#include "OnnxRunnerInterface.h"

class OnnxRunner_AdaIN : public OnnxRunnerInterface
{

public: // Functions
    virtual bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue) override;
    virtual bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) override;
    virtual bool Run() override;
    virtual void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) override;
    virtual void Shutdown() override;
    virtual void AllocateOutputForShape(const std::vector<int64_t>& shape) override;

protected:
    //// �� IO �̸�
    //std::string m_InNameContent; // input[0] : "content"
    //std::string m_InNameStyle;   // input[1] : "style"
    //std::string m_OutName;       // output[0]: "stylized"

    //// �� IO shape (������ �� ����; PrepareIO �� Ȯ��ġ�� ����)
    //std::vector<int64_t> m_InShapeContent; // ���� [-1,3,-1,-1] �� [1,3,Hc,Wc]
    //std::vector<int64_t> m_InShapeStyle;   // ���� [-1,3,-1,-1] �� [1,3,Hs,Ws]
    //std::vector<int64_t> m_OutShape;       // ���� [-1,3,-1,-1] �� [1,3,Ho,Wo] (Ho/Wo�� 8�� ���)

    //// GPU ���� + DML allocation �ڵ�
    //ComPointer<ID3D12Resource> m_InputBufContent; // content NCHW FP32
    //ComPointer<ID3D12Resource> m_InputBufStyle;   // style   NCHW FP32
    //ComPointer<ID3D12Resource> m_OutputBuf;       // output  NCHW FP32
    //void* m_InAllocContent = nullptr; // DML GPU allocation handle
    //void* m_InAllocStyle = nullptr;
    //void* m_OutAlloc = nullptr;

    //// ����Ʈ ũ��
    //UINT64 m_InBytesContent = 0;
    //UINT64 m_InBytesStyle = 0;
    //UINT64 m_OutBytes = 0;



};

