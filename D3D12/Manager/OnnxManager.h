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
    bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue);
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H); 
    bool Run();                                       
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H);
    void Shutdown();

    //===========Getter=================//
    ComPointer<ID3D12Resource> GetInputBuffer()  const { return m_InputBuf; }
    ComPointer<ID3D12Resource> GetOutputBuffer() const { return m_OutputBuf; }
    const std::vector<int64_t>& GetOutputShape() const { return m_OutShape; }
    const std::vector<int64_t>& GetInputShape() const { return m_InShape; }
    //==================================//

private: // Functions

private: // Variables

    Ort::MemoryInfo miDml_{ nullptr };

    Ort::Env env_{ ORT_LOGGING_LEVEL_WARNING, "app" };
    Ort::SessionOptions m_So;
    std::unique_ptr<Ort::Session> m_Session;
    const OrtDmlApi* m_DmlApi = nullptr;

    // ¸ðµ¨ IO
    std::string m_InName, m_OutName;
    std::vector<int64_t> m_InShape, m_OutShape;

    // GPU IO
    ComPointer<ID3D12Device> m_Dev;
    ComPointer<ID3D12CommandQueue> m_Queue;
    ComPointer<ID3D12Resource> m_InputBuf;
    ComPointer<ID3D12Resource> m_OutputBuf;
    void* m_InAlloc = nullptr;  // DML GPU allocation ÇÚµé
    void* m_OutAlloc = nullptr;
    UINT64 m_InBytes = 0, m_OutBytes = 0;

};

