#pragma once

#include "OnnxRunnerInterface.h"

class OnnxRunner_BlindVideo : public OnnxRunnerInterface
{

public: // Functions
    virtual bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue) override;
    virtual bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) override;
    virtual bool Run() override;
    virtual void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) override;
    virtual void Shutdown() override;
    virtual void AllocateOutputForShape(const std::vector<int64_t>& shape) override;

protected:
    std::unique_ptr<Ort::IoBinding> m_Binding; 
    Ort::Value m_InTensorDML{ nullptr };       
    Ort::Value m_OutTensorDML{ nullptr };      
    bool m_OutputBound = false;                
};

