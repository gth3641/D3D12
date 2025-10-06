#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Util/Util.h"
#include "OnnxRunner/OnnxRunnerInterface.h"

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
    // ONNX Runtime + DirectML EP �ʱ�ȭ
    bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);

    // ���� �ػ� �غ�(����): content/style�� ���� ����
    bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);

    // ȣȯ �����ε�: style = content
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H) { return PrepareIO(dev, W, H, W, H); }

    // ����(���� ���ε��� GPU ���۸� ���)
    bool Run();

    // ��������(����)
    void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);

    // ȣȯ �����ε�: style = content
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H) { ResizeIO(dev, W, H, W, H); }

    // ����
    void Shutdown();

    //===========Getter=================//
    // �� ����(�����): content/style �Է� ���ۿ� shape
    ComPointer<ID3D12Resource> GetOutputBuffer()       const { return OnnxRunner->GetOutputBuffer(); }
    ComPointer<ID3D12Resource> GetInputBufferContent() const { return OnnxRunner->GetInputBufferContent(); }
    ComPointer<ID3D12Resource> GetInputBufferStyle()   const { return OnnxRunner->GetInputBufferStyle(); }
    const std::vector<int64_t>& GetOutputShape()       const { return OnnxRunner->GetOutputShape(); }
    const std::vector<int64_t>& GetInputShapeContent() const { return OnnxRunner->GetInputShapeContent(); }
    const std::vector<int64_t>& GetInputShapeStyle()   const { return OnnxRunner->GetInputShapeStyle(); }
    //==================================//


private:
    std::unique_ptr<OnnxRunnerInterface> OnnxRunner = nullptr;

};

