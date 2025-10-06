#include "OnnxManager.h"
#include "DirectXManager.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include "OnnxRunner/OnnxRunner_AdaIN.h"
#include "OnnxRunner/OnnxRunner_Udnie.h"



bool OnnxManager::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    OnnxRunner = std::make_unique<OnnxRunner_AdaIN>();
    if (OnnxRunner == nullptr)
    {
        return false;
    }

    return OnnxRunner->Init(modelPath, dev, queue);
}

bool OnnxManager::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (OnnxRunner == nullptr)
    {
        return false;
    }

    return OnnxRunner->PrepareIO(dev, contentW, contentH, styleW, styleH);
}

bool OnnxManager::Run()
{
    if (OnnxRunner == nullptr)
    {
        return false;
    }

    return OnnxRunner->Run();
}

void OnnxManager::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (OnnxRunner == nullptr)
    {
        return;
    }

    OnnxRunner->ResizeIO(dev, contentW, contentH, styleW, styleH);
}


void OnnxManager::Shutdown()
{
    if (OnnxRunner == nullptr)
    {
        return;
    }

    OnnxRunner->Shutdown();
    OnnxRunner.release();
    OnnxRunner = nullptr;
}
