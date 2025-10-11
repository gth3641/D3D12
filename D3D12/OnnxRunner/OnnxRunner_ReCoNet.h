#pragma once

#include "OnnxRunnerInterface.h"

class OnnxRunner_ReCoNet : public OnnxRunnerInterface
{

public: // Functions
    virtual bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue) override;
    virtual bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) override;
    virtual bool Run() override;
    virtual void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) override;
    virtual void Shutdown() override;
    virtual void AllocateOutputForShape(const std::vector<int64_t>& shape) override;

protected:
    // ���� ��ƿ
    D3D12_RESOURCE_DESC MakeBufDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) const;
    D3D12_HEAP_PROPERTIES HeapDefault() const;
    D3D12_HEAP_PROPERTIES HeapUpload()  const;
    D3D12_HEAP_PROPERTIES HeapReadback() const;

    void CopyDefaultToReadback(ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, ID3D12Resource* dst, size_t bytes);
    void CopyUploadToDefault(ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, ID3D12Resource* dst, size_t bytes);


protected:
    // IoBinding & DML �ټ� �ڵ�
    std::unique_ptr<Ort::IoBinding> m_Binding;
    Ort::Value m_InTensorDML{ nullptr };
    Ort::Value m_OutTensorDML{ nullptr };
    void* m_InDmlAlloc = nullptr;   // DML allocation handle
    void* m_OutDmlAlloc = nullptr;   // DML allocation handle

    // �Է�/��� �ټ� shape ĳ��
    std::vector<int64_t> m_ModelInShape;  // �� ���� shape (���� ����)
    std::vector<int64_t> m_ModelOutShape; // �� ���� shape (���� ����)

    // ����� �̸�
    std::string m_InputName;  // content
    std::string m_OutputName; // stylized

    // ���࿡ �ʿ��� ũ�� ĳ��
    UINT m_W = 0, m_H = 0;
    UINT64 m_InputBytes = 0;
    UINT64 m_OutputBytes = 0;
};

