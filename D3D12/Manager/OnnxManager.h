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
    // ONNX Runtime + DirectML EP 초기화
    bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);

    // 가변 해상도 준비(권장): content/style을 각각 지정
    bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);

    // 호환 오버로드: style = content
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H) { return PrepareIO(dev, W, H, W, H); }

    // 실행(현재 바인딩된 GPU 버퍼를 사용)
    bool Run();

    // 리사이즈(권장)
    void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH);

    // 호환 오버로드: style = content
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H) { ResizeIO(dev, W, H, W, H); }

    // 종료
    void Shutdown();

    //===========Getter=================//
    // 새 게터(명시적): content/style 입력 버퍼와 shape
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

