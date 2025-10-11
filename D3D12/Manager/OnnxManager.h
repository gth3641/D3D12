#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Util/Util.h"
#include "OnnxRunner/OnnxRunnerInterface.h"
#include "Util/OnnxDefine.h"

#include <string>
#include <vector>
#include <wrl.h>
#include <DirectML.h>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <memory>

#define DX_ONNX OnnxManager::Get()



class OnnxManager
{
public: // Singleton pattern to ensure only one instance exists 
    OnnxManager(const OnnxManager&) = delete;
    OnnxManager& operator=(const OnnxManager&) = delete;

    inline static OnnxManager& Get()
    {
        static OnnxManager instance;
        return instance;
    }

public:
    OnnxManager() = default;

public: // Static & Override
  
public: // Functions
	bool Init(OnnxType type, ID3D12Device* dev, ID3D12CommandQueue* queue);
    bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H) { return PrepareIO(dev, W, H, W, H); }
    bool Run();
    void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H) { ResizeIO(dev, W, H, W, H); }
    void Shutdown();

    //===========Getter=================//
    // 새 게터(명시적): content/style 입력 버퍼와 shape
    ComPointer<ID3D12Resource> GetOutputBuffer()        const { return m_OnnxRunner->GetOutputBuffer(); }
    ComPointer<ID3D12Resource> GetInputBufferContent()  const { return m_OnnxRunner->GetInputBufferContent(); }
    ComPointer<ID3D12Resource> GetInputBufferStyle()    const { return m_OnnxRunner->GetInputBufferStyle(); }
    const std::vector<int64_t>& GetOutputShape()        const { return m_OnnxRunner->GetOutputShape(); }
    const std::vector<int64_t>& GetInputShapeContent()  const { return m_OnnxRunner->GetInputShapeContent(); }
    const std::vector<int64_t>& GetInputShapeStyle()    const { return m_OnnxRunner->GetInputShapeStyle(); }
	bool IsInitialized()                                const { return m_Initialized; }
	OnnxType GetOnnxType()                              const { return m_OnnxType; }
    //==================================//


private:
    bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);
	void InitOnnxRunner(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);

private:
    std::unique_ptr<OnnxRunnerInterface> m_OnnxRunner = nullptr;

	bool m_Initialized = false;
	OnnxType m_OnnxType = OnnxType::None;
};

