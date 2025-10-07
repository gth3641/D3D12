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
};

