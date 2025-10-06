#pragma once

#include "OnnxRunnerInterface.h"

class OnnxRunner_Udnie : public OnnxRunnerInterface
{

public: // Functions
    virtual bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);
    virtual bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);
    virtual bool Run();
    virtual void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);
    virtual void Shutdown();
    virtual void AllocateOutputForShape(const std::vector<int64_t>& shape);

};

