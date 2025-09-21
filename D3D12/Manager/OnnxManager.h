#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Util/Util.h"

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
    bool Init();
    void Shutdown();


    //===========Getter=================//

    //==================================//

private: // Functions

private: // Variables

    ComPointer<IDMLDevice> m_Dml;


};

