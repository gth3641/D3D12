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
    // 내부 유틸
    D3D12_RESOURCE_DESC MakeBufDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) const;
    D3D12_HEAP_PROPERTIES HeapDefault() const;
    D3D12_HEAP_PROPERTIES HeapUpload()  const;
    D3D12_HEAP_PROPERTIES HeapReadback() const;

    void CopyDefaultToReadback(ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, ID3D12Resource* dst, size_t bytes);
    void CopyUploadToDefault(ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, ID3D12Resource* dst, size_t bytes);


protected:
    // IoBinding & DML 텐서 핸들
    std::unique_ptr<Ort::IoBinding> m_Binding;
    Ort::Value m_InTensorDML{ nullptr };
    Ort::Value m_OutTensorDML{ nullptr };
    void* m_InDmlAlloc = nullptr;   // DML allocation handle
    void* m_OutDmlAlloc = nullptr;   // DML allocation handle

    // 입력/출력 텐서 shape 캐시
    std::vector<int64_t> m_ModelInShape;  // 모델 선언 shape (동적 포함)
    std::vector<int64_t> m_ModelOutShape; // 모델 선언 shape (동적 포함)

    // 입출력 이름
    std::string m_InputName;  // content
    std::string m_OutputName; // stylized

    // 실행에 필요한 크기 캐시
    UINT m_W = 0, m_H = 0;
    UINT64 m_InputBytes = 0;
    UINT64 m_OutputBytes = 0;
};

