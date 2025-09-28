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
    dev_ = dev; queue_ = queue;

    so_ = Ort::SessionOptions{};
    so_.DisableMemPattern();
    so_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    so_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    so_.SetIntraOpNumThreads(1);

    ComPointer<IDMLDevice> dml;
    THROW_IF_FAILED(DMLCreateDevice(dev_, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)));

    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
        reinterpret_cast<const void**>(&dmlApi_)));
    Ort::ThrowOnError(dmlApi_->SessionOptionsAppendExecutionProvider_DML1(so_, dml.Get(), queue_));

    // 세션 생성 (env_는 클래스 멤버로, 세션보다 오래 살아야 함)
    session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(), so_);

    // ★ DML MemoryInfo를 "대문자 DML"로 생성자 사용해 1회 생성
    miDml_ = Ort::MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, /*device_id*/0, OrtMemTypeDefault);

    // IO 메타
    Ort::AllocatorWithDefaultOptions alloc;
    auto inName = session_->GetInputNameAllocated(0, alloc);
    auto outName = session_->GetOutputNameAllocated(0, alloc);
    inName_ = inName.get();
    outName_ = outName.get();

    auto inInfo = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
    auto outInfo = session_->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
    inShape_ = inInfo.GetShape();
    outShape_ = outInfo.GetShape();

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
    fixShape(inShape_);
    fixShape(outShape_);

    auto bytesOf = [](const std::vector<int64_t>& s, size_t elemBytes) {
        uint64_t n = 1; for (auto d : s) n *= (uint64_t)d; return n * elemBytes;
        };
    inBytes_ = (UINT64)bytesOf(inShape_, sizeof(float));
    outBytes_ = (UINT64)bytesOf(outShape_, sizeof(float));

    CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT);
    auto rdIn = CD3DX12_RESOURCE_DESC::Buffer(inBytes_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto rdOut = CD3DX12_RESOURCE_DESC::Buffer(outBytes_, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    THROW_IF_FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdIn,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&inputBuf_)));
    THROW_IF_FAILED(dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdOut,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&outputBuf_)));

    inputBuf_->SetName(L"ORT_InputBuffer");
    outputBuf_->SetName(L"ORT_OutputBuffer");

    // DML GPU allocation 핸들
    Ort::ThrowOnError(dmlApi_->CreateGPUAllocationFromD3DResource(inputBuf_.Get(), &inAlloc_));
    Ort::ThrowOnError(dmlApi_->CreateGPUAllocationFromD3DResource(outputBuf_.Get(), &outAlloc_));
    return true;
}

bool OnnxManager::Run()
{
    try {
        // CreateGPUAllocationFromD3DResource 로 만든 핸들을 그대로 p_data 로 준다
        Ort::Value inTensor = Ort::Value::CreateTensor(
            miDml_,               // ← 멤버로 보관한 DML MemoryInfo
            inAlloc_,             // void* (DML EP allocation handle)
            inBytes_,             // 바이트 수
            inShape_.data(), inShape_.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::Value outTensor = Ort::Value::CreateTensor(
            miDml_,
            outAlloc_,
            outBytes_,
            outShape_.data(), outShape_.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::IoBinding binding(*session_);
        binding.BindInput(inName_.c_str(), inTensor);
        binding.BindOutput(outName_.c_str(), outTensor);

        session_->Run(Ort::RunOptions{ nullptr }, binding);
       
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
    if (inAlloc_) { dmlApi_->FreeGPUAllocation(inAlloc_);  inAlloc_ = nullptr; }
    if (outAlloc_) { dmlApi_->FreeGPUAllocation(outAlloc_); outAlloc_ = nullptr; }
    inputBuf_.Release();
    outputBuf_.Release();
    PrepareIO(dev, W, H);
}

void OnnxManager::Shutdown()
{
    if (inAlloc_) { dmlApi_->FreeGPUAllocation(inAlloc_);  inAlloc_ = nullptr; }
    if (outAlloc_) { dmlApi_->FreeGPUAllocation(outAlloc_); outAlloc_ = nullptr; }
    inputBuf_.Release();
    outputBuf_.Release();

    session_.reset();
}

