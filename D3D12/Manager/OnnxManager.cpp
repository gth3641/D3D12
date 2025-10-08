#include "OnnxManager.h"

#include "OnnxRunner/OnnxRunner_AdaIN.h"
#include "OnnxRunner/OnnxRunner_Udnie.h"
#include "OnnxRunner/OnnxRunner_FastNeuralStyle.h"

bool OnnxManager::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
	InitOnnxRunner(modelPath, dev, queue);
    if (m_OnnxRunner == nullptr)
    {
        return false;
    }

    return m_OnnxRunner->Init(modelPath, dev, queue);
}

bool OnnxManager::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (m_OnnxRunner == nullptr)
    {
        return false;
    }

    return m_OnnxRunner->PrepareIO(dev, contentW, contentH, styleW, styleH);
}

bool OnnxManager::Run()
{
    if (m_OnnxRunner == nullptr)
    {
        return false;
    }

    return m_OnnxRunner->Run();
}

void OnnxManager::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (m_OnnxRunner == nullptr)
    {
        return;
    }

    m_OnnxRunner->ResizeIO(dev, contentW, contentH, styleW, styleH);
}


void OnnxManager::Shutdown()
{
    if (m_OnnxRunner == nullptr)
    {
        return;
    }

    m_OnnxRunner->Shutdown();
    m_OnnxRunner.release();
    m_OnnxRunner = nullptr;
}

void OnnxManager::InitOnnxRunner(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    if (modelPath.find(L"dyn") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_FastNeuralStyle>();
        m_OnnxType = OnnxType::FastNeuralStyle;
    }
    else if (modelPath.find(L"adain") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_AdaIN>();
		m_OnnxType = OnnxType::AdaIN;
    }
    else if (modelPath.find(L"udnie") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_Udnie>();
        m_OnnxType = OnnxType::Udnie;
    }
    else
    {
        m_OnnxRunner = std::make_unique<OnnxRunnerInterface>();
		m_OnnxType = OnnxType::None;
	}

    m_Initialized = true;
}
