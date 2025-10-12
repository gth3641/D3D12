#include "OnnxManager.h"

#include "OnnxRunner/OnnxRunner_AdaIN.h"
#include "OnnxRunner/OnnxRunner_Udnie.h"
#include "OnnxRunner/OnnxRunner_FastNeuralStyle.h"
#include "OnnxRunner/OnnxRunner_ReCoNet.h"
#include "OnnxRunner/OnnxRunner_BlindVideo.h"
#include "OnnxRunner/OnnxRunner_Sanet.h"

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
	    case OnnxType::Udnie:
        {
		    return Init(L"./Resources/Onnx/udnie-9.onnx", dev, queue);
        }
	    case OnnxType::WCT2:
        {
		    return Init(L"./Resources/Onnx/WCT2_dynamic.onnx", dev, queue);
        }
	    case OnnxType::AdaIN:
        {
		    return Init(L"./Resources/Onnx/adain_end2end.onnx", dev, queue);
        }
	    case OnnxType::FastNeuralStyle:
        {
		    return Init(L"./Resources/Onnx/FHD/rain_princess_opset12_dyn.onnx", dev, queue);
		    //return Init(L"./Resources/Onnx/FHD/mosaic_opset12_dyn.onnx", dev, queue);
		    //return Init(L"./Resources/Onnx/FHD/udnie_opset12_dyn.onnx", dev, queue);
        }
	    case OnnxType::ReCoNet:
        {
		    return Init(L"./Resources/Onnx/reconet.onnx", dev, queue);
        }
	    case OnnxType::BlindVideo:
        {
		    return Init(L"./Resources/Onnx/stylize_blindvideo.onnx", dev, queue);
        }
	    case OnnxType::Sanet:
        {
            //https://github.com/dypark86/SANET
		    return Init(L"./Resources/Onnx/sanet_pipeline_img_ms_dynsim.onnx", dev, queue);
        }
        case OnnxType::AdaAttN:
        {
            return Init(L"./Resources/Onnx/adaattn_img_512.onnx", dev, queue);
		}

        case OnnxType::MsgNet:
        {
            return Init(L"./Resources/Onnx/msgnet_content_only.onnx", dev, queue);
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
        m_OnnxType = OnnxType::AdaIN;
    }
    else if (modelPath.find(L"WCT") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_AdaIN>();
        m_OnnxType = OnnxType::AdaIN;
    }
    else if (modelPath.find(L"msg") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_ReCoNet>();
        m_OnnxType = OnnxType::MsgNet;
    }
    else if (modelPath.find(L"blind") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_BlindVideo>();
        m_OnnxType = OnnxType::BlindVideo;
    }
    else if (modelPath.find(L"reconet") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_ReCoNet>();
        m_OnnxType = OnnxType::ReCoNet;
    }
    else if (modelPath.find(L"adaattn") != std::wstring::npos)
    {
        m_OnnxRunner = std::make_unique<OnnxRunner_AdaIN>();
        m_OnnxType = OnnxType::AdaIN;
    }
    else if (modelPath.find(L"dyn") != std::wstring::npos)
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
