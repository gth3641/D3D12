#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Util/Util.h"

#include <string>
#include <vector>
#include <wrl.h>
#include <DirectML.h>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

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
    //bool Init();
    //void Shutdown();
    //void RunTest();
    //bool RunCpuSmokeTest();
    //bool RunGpuSmokeTest(ID3D12Device* dev, ID3D12CommandQueue* q);

    bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H); // 입력/출력 버퍼 + DML 핸들 준비
    bool Run();                                        // IoBinding 실행 (GPU in/out)
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H);
    void Shutdown();

    //===========Getter=================//
    ComPointer<ID3D12Resource> GetInputBuffer()  const { return inputBuf_; }
    ComPointer<ID3D12Resource> GetOutputBuffer() const { return outputBuf_; }
    const std::vector<int64_t>& GetOutputShape() const { return outShape_; }
    //==================================//

private: // Functions

private: // Variables

    ComPointer<IDMLDevice> m_Dml;

    //Ort::Env env_{ ORT_LOGGING_LEVEL_WARNING, "app" };
    //Ort::SessionOptions so_;
    //std::unique_ptr<Ort::Session> session_;
    //std::vector<std::string> inputNames_, outputNames_;


    Ort::Env env_{ ORT_LOGGING_LEVEL_WARNING, "app" };
    Ort::SessionOptions so_;
    std::unique_ptr<Ort::Session> session_;
    const OrtDmlApi* dmlApi_ = nullptr;

    // 모델 IO
    std::string inName_, outName_;
    std::vector<int64_t> inShape_, outShape_;

    // GPU IO
    ComPointer<ID3D12Device> dev_;
    ComPointer<ID3D12CommandQueue> queue_;
    ComPointer<ID3D12Resource> inputBuf_;
    ComPointer<ID3D12Resource> outputBuf_;
    void* inAlloc_ = nullptr;  // DML GPU allocation 핸들
    void* outAlloc_ = nullptr;
    UINT64 inBytes_ = 0, outBytes_ = 0;

};

