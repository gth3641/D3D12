#include "OnnxManager.h"

#include "OnnxRunner/OnnxRunner_AdaIN.h"
//#include "OnnxRunner/OnnxRunner_Udnie.h"
#include "OnnxRunner/OnnxRunner_FastNeuralStyle.h"
//#include "OnnxRunner/OnnxRunner_ReCoNet.h"
//#include "OnnxRunner/OnnxRunner_BlindVideo.h"
//#include "OnnxRunner/OnnxRunner_Sanet.h"

bool OnnxManager::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
	InitOnnxRunner(modelPath, dev, queue);
    if (m_OnnxRunner == nullptr)
    {
        return false;
    }

    return m_OnnxRunner->Init(modelPath, dev, queue);
}

bool OnnxManager::Init(OnnxType type, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    switch (type)
	{
	    case OnnxType::WCT2:
        {
		    return Init(L"./Resources/Onnx/1x1_Conv.onnx", dev, queue);
        }
	    case OnnxType::AdaIN:
        {
		    return Init(L"./Resources/Onnx/adain_end2end_2inputs_op17.onnx", dev, queue);
        }
	    case OnnxType::FastNeuralStyle:
        {
		    return Init(L"./Resources/Onnx/FHD/FST_dyn_TheStarryNight.onnx", dev, queue);
        }
	    case OnnxType::ReCoNet:
        {
		    return Init(L"./Resources/Onnx/FHD/ReCoNet_TheStarryNight.onnx", dev, queue);
        }
	    case OnnxType::Sanet:
        {
		    return Init(L"./Resources/Onnx/sanet_end2end_2inputs_op17.onnx", dev, queue);
        }

	    default:
		    return false;
	}

    return false;
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
    if (modelPath.find(L"sanet") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_AdaIN>();
        m_OnnxType = OnnxType::Sanet;
    }
    else if (modelPath.find(L"Conv") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_AdaIN>();
        m_OnnxType = OnnxType::WCT2;
    }
    else if (modelPath.find(L"ReCoNet") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_FastNeuralStyle>();
        m_OnnxType = OnnxType::ReCoNet;
    }
    else if (modelPath.find(L"dyn") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_FastNeuralStyle>();
        m_OnnxType = OnnxType::ReCoNet;
    }
    else if (modelPath.find(L"adain") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_AdaIN>();
		m_OnnxType = OnnxType::AdaIN;
    }
    else
    {
        m_OnnxRunner = std::make_unique<OnnxRunnerInterface>();
		m_OnnxType = OnnxType::None;
	}

    m_ChangeOnnxType = m_OnnxType;
    m_Initialized = true;
}
