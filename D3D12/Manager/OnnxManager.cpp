#include "OnnxManager.h"
#include "DirectXManager.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif


bool OnnxManager::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    m_Dev = dev; m_Queue = queue;

    ComPointer<IDMLDevice> dml;
    THROW_IF_FAILED(DMLCreateDevice(m_Dev, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)));

    m_So = Ort::SessionOptions{};
    m_So.DisableMemPattern();
    m_So.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    m_So.SetIntraOpNumThreads(0);
    m_So.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);


    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
        reinterpret_cast<const void**>(&m_DmlApi)));
    Ort::ThrowOnError(m_DmlApi->SessionOptionsAppendExecutionProvider_DML1(m_So, dml.Get(), m_Queue));

    m_Session = std::make_unique<Ort::Session>(env_, modelPath.c_str(), m_So);
    miDml_ = Ort::MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, /*device_id*/0, OrtMemTypeDefault);

    // IO 메타
    Ort::AllocatorWithDefaultOptions alloc;
    auto inName = m_Session->GetInputNameAllocated(0, alloc);
    auto outName = m_Session->GetOutputNameAllocated(0, alloc);
    m_InName = inName.get();
    m_OutName = outName.get();

    auto inInfo = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
    auto outInfo = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
    m_InShape = inInfo.GetShape();
    m_OutShape = outInfo.GetShape();

    return true;
}

bool OnnxManager::PrepareIO(ID3D12Device* dev, UINT W, UINT H)
{
    auto fixShape = [&](std::vector<int64_t>& s) {
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] < 0) {
                if (i == 0) s[i] = 1;               // N
                else if (i == 1) s[i] = 3;          // C (모델에 맞게 조정)
                else if (i == 2) s[i] = (int64_t)H; // H
                else if (i == 3) s[i] = (int64_t)W; // W
                else s[i] = 1;
            }
        }
        };
    fixShape(m_InShape);
    fixShape(m_OutShape);

    auto bytesOf = [](const std::vector<int64_t>& s, size_t elemBytes) {
        uint64_t n = 1; for (auto d : s) n *= (uint64_t)d; return n * elemBytes;
        };
    m_InBytes = (UINT64)bytesOf(m_InShape, sizeof(float));
    m_OutBytes = (UINT64)bytesOf(m_OutShape, sizeof(float));

    CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC rdIn = CD3DX12_RESOURCE_DESC::Buffer(m_InBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    CD3DX12_RESOURCE_DESC rdOut = CD3DX12_RESOURCE_DESC::Buffer(m_OutBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    THROW_IF_FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdIn,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBuf)));
    THROW_IF_FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdOut,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputBuf)));

    m_InputBuf->SetName(L"ORT_InputBuffer");
    m_OutputBuf->SetName(L"ORT_OutputBuffer");

    // DML GPU allocation 핸들
    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBuf.Get(), &m_InAlloc));
    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_OutputBuf.Get(), &m_OutAlloc));
    return true;
}

bool OnnxManager::Run()
{
    try {
        // CreateGPUAllocationFromD3DResource 로 만든 핸들을 그대로 p_data 로 준다
        Ort::Value inTensor = Ort::Value::CreateTensor(
            miDml_,               // 멤버로 보관한 DML MemoryInfo
            m_InAlloc,             // void* (DML EP allocation handle)
            m_InBytes,             // 바이트 수
            m_InShape.data(), m_InShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::Value outTensor = Ort::Value::CreateTensor(
            miDml_,
            m_OutAlloc,
            m_OutBytes,
            m_OutShape.data(), m_OutShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::IoBinding binding(*m_Session);
        binding.BindInput(m_InName.c_str(), inTensor);
        binding.BindOutput(m_OutName.c_str(), outTensor);

        m_Session->Run(Ort::RunOptions{ nullptr }, binding);
       
        binding.ClearBoundInputs(); 
        binding.ClearBoundOutputs();
    }
    catch (const Ort::Exception& e) {
        OutputDebugStringA((std::string("ORT Run failed: ") + e.what() + "\n").c_str());
        return false;
    }
    return true;

}

void OnnxManager::ResizeIO(ID3D12Device* dev, UINT W, UINT H)
{
    if (m_InAlloc) { m_DmlApi->FreeGPUAllocation(m_InAlloc);  m_InAlloc = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_InputBuf.Release();
    m_OutputBuf.Release();
    PrepareIO(dev, W, H);
}

void OnnxManager::Shutdown()
{
    if (m_InAlloc) { m_DmlApi->FreeGPUAllocation(m_InAlloc);  m_InAlloc = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_InputBuf.Release();
    m_OutputBuf.Release();

    m_Session.reset();
}

