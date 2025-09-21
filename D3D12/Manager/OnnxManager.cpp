#include "OnnxManager.h"
#include "DirectXManager.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif

//bool OnnxManager::Init()
//{
//    try {
//        // 0) 매 호출마다 SessionOptions를 "완전히 새로" 만든다
//        so_ = Ort::SessionOptions{};
//        so_.DisableMemPattern();
//        so_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
//        so_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
//
//        bool attachedDml = false;
//
//        // 1) DML1 경로: 내가 가진 D3D12 디바이스/큐로 붙이기 (권장)
//        if (DX_CONTEXT.GetDevice() && DX_CONTEXT.GetCommandQueue())
//        {
//            ComPointer<IDMLDevice> dml;
//            DMLCreateDevice(DX_CONTEXT.GetDevice(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml));
//
//            const OrtDmlApi* dmlApi = nullptr;
//            Ort::ThrowOnError(
//                Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
//                    reinterpret_cast<const void**>(&dmlApi))
//            );
//
//            Ort::ThrowOnError(dmlApi->SessionOptionsAppendExecutionProvider_DML1(
//                so_, dml.Get(), DX_CONTEXT.GetCommandQueue()));
//
//            attachedDml = true;
//        }
//
//        // 2) (옵션) DML1 실패 시에만 간단 DML로 폴백
//        if (!attachedDml)
//        {
//            OrtStatus* st = OrtSessionOptionsAppendExecutionProvider_DML(so_, /*deviceId*/0);
//            if (st == nullptr) {
//                attachedDml = true;
//            }
//            else {
//                // 원하면 여기서 CPU로 폴백하거나, 에러로 처리
//                Ort::GetApi().ReleaseStatus(st);
//            }
//        }
//
//        // 여기서 DML을 또 추가하면 "already registered"가 납니다. 절대 중복 호출 금지!
//
//        // 3) 세션 생성
//        session_ = std::make_unique<Ort::Session>(env_, L"./Resources/Onnx/udnie-9.onnx", so_);
//        return true;
//    }
//    catch (const Ort::Exception& e) {
//        OutputDebugStringA(("OnnxManager::Init ORT error: " + std::string(e.what()) + "\n").c_str());
//        return false;
//    }
//
//	return true;
//}
//
//
//void OnnxManager::Shutdown()
//{
//	m_Dml.Release();
//}
//
//void OnnxManager::RunTest()
//{
//    if (!session_) { OutputDebugStringA("No session\n"); return; }
//
//    Ort::AllocatorWithDefaultOptions alloc;
//
//    const size_t inCount = session_->GetInputCount();
//    const size_t outCount = session_->GetOutputCount();
//
//    std::string log;
//
//    auto typeToStr = [](ONNXTensorElementDataType t) {
//        switch (t) {
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: return "float32";
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: return "uint8";
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:  return "int8";
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:return "uint16";
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16: return "int16";
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: return "int32";
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: return "int64";
//        case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:return "float64";
//        default: return "other";
//        }
//        };
//
//    log += "=== ONNX IO ===\n";
//    log += "Inputs:\n";
//    for (size_t i = 0; i < inCount; ++i) {
//        auto name = session_->GetInputNameAllocated(i, alloc);
//        auto ti = session_->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
//        auto et = ti.GetElementType();
//        auto shp = ti.GetShape();
//        log += "  - " + std::string(name.get()) + " : " + typeToStr(et) + " [";
//        for (size_t k = 0; k < shp.size(); ++k) { log += std::to_string(shp[k]); if (k + 1 < shp.size()) log += ","; }
//        log += "]\n";
//    }
//    log += "Outputs:\n";
//    for (size_t i = 0; i < outCount; ++i) {
//        auto name = session_->GetOutputNameAllocated(i, alloc);
//        auto ti = session_->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
//        auto et = ti.GetElementType();
//        auto shp = ti.GetShape();
//        log += "  - " + std::string(name.get()) + " : " + typeToStr(et) + " [";
//        for (size_t k = 0; k < shp.size(); ++k) { log += std::to_string(shp[k]); if (k + 1 < shp.size()) log += ","; }
//        log += "]\n";
//    }
//    OutputDebugStringA(log.c_str());
//}
//
//bool OnnxManager::RunCpuSmokeTest()
//{
//    if (!session_) return false;
//
//    Ort::AllocatorWithDefaultOptions alloc;
//
//    // 입력 0 기준으로 간단 테스트 (여러 입력이면 필요만큼 반복)
//    auto inNameAlloc = session_->GetInputNameAllocated(0, alloc);
//    std::string inName = inNameAlloc.get();
//
//    auto tinfo = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
//    auto etype = tinfo.GetElementType();
//    auto shape = tinfo.GetShape();
//
//    // 동적(-1) 차원을 1로 대체
//    for (auto& d : shape) if (d < 0) d = 1;
//
//    // float32 만 먼저 지원(필요시 다른 타입 분기)
//    if (etype != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
//        OutputDebugStringA("RunCpuSmokeTest: only float32 handled in sample\n");
//        return false;
//    }
//
//    size_t elemCount = 1;
//    for (auto d : shape) elemCount *= size_t(d);
//
//    std::vector<float> hostInput(elemCount, 0.5f); // 임의 값
//    Ort::MemoryInfo mi = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
//
//    Ort::Value input = Ort::Value::CreateTensor<float>(
//        mi, hostInput.data(), hostInput.size(),
//        shape.data(), shape.size()
//    );
//
//    // 출력 이름 가져오기(1개 가정)
//    auto outNameAlloc = session_->GetOutputNameAllocated(0, alloc);
//    std::string outName = outNameAlloc.get();
//
//    const char* inNames[] = { inName.c_str() };
//    const char* outNames[] = { outName.c_str() };
//
//    auto outputs = session_->Run(Ort::RunOptions{ nullptr },
//        inNames, &input, 1,
//        outNames, 1);
//
//    if (outputs.size() != 1 || !outputs[0].IsTensor()) {
//        OutputDebugStringA("RunCpuSmokeTest: invalid output\n");
//        return false;
//    }
//
//    auto outT = outputs[0].GetTensorTypeAndShapeInfo();
//    auto outShape = outT.GetShape();
//    std::string log = "RunCpuSmokeTest: ok, output shape [";
//    for (size_t i = 0; i < outShape.size(); ++i) { log += std::to_string(outShape[i]); if (i + 1 < outShape.size()) log += ","; }
//    log += "]\n";
//    OutputDebugStringA(log.c_str());
//    return true;
//}
//
//bool OnnxManager::RunGpuSmokeTest(ID3D12Device* dev, ID3D12CommandQueue* q)
//{
//    if (!session_) return false;
//
//    try {
//        // === 1) 모델 입력 메타 ===
//        Ort::AllocatorWithDefaultOptions alloc;
//        auto inName = session_->GetInputNameAllocated(0, alloc);
//        auto outName = session_->GetOutputNameAllocated(0, alloc);
//
//        auto inInfo = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
//        auto etype = inInfo.GetElementType();
//        auto inShape = inInfo.GetShape();
//        for (auto& d : inShape) if (d < 0) d = 1;         // 동적 차원 임시 1 처리
//        if (etype != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) // 샘플은 float32만 처리
//        {
//            OutputDebugStringA("RunGpuSmokeTest: only float32 handled.\n");
//            return false;
//        }
//        size_t elemCount = 1; for (auto d : inShape) elemCount *= size_t(d);
//        if (!elemCount) { OutputDebugStringA("RunGpuSmokeTest: elemCount=0\n"); return false; }
//        const UINT64 byteSize = UINT64(elemCount * sizeof(float));
//
//        // === 2) 리소스 준비 (DEFAULT UAV + UPLOAD) ===
//        CD3DX12_HEAP_PROPERTIES hpDefault(D3D12_HEAP_TYPE_DEFAULT);
//        CD3DX12_HEAP_PROPERTIES hpUpload(D3D12_HEAP_TYPE_UPLOAD);
//
//        // DML 바인딩할 DEFAULT 버퍼는 UAV 허용
//        CD3DX12_RESOURCE_DESC rdDefaultUAV =
//            CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
//        // 업로드 버퍼는 플래그 없음
//        CD3DX12_RESOURCE_DESC rdUpload = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
//
//        ComPointer<ID3D12Resource> gpuIn, upload;
//        THROW_IF_FAILED(dev->CreateCommittedResource(
//            &hpDefault, D3D12_HEAP_FLAG_NONE, &rdDefaultUAV,
//            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&gpuIn)));
//
//        THROW_IF_FAILED(dev->CreateCommittedResource(
//            &hpUpload, D3D12_HEAP_FLAG_NONE, &rdUpload,
//            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));
//
//        // 업로드 데이터 채우기
//        {
//            void* p = nullptr;
//            CD3DX12_RANGE range(0, 0);
//            THROW_IF_FAILED(upload->Map(0, &range, &p));
//            std::fill_n(reinterpret_cast<float*>(p), elemCount, 0.25f);
//            upload->Unmap(0, nullptr);
//        }
//
//        // === 3) 복사 커맨드: allocator/list 생성 → 실행 → Fence로 완료 대기(해제 안전) ===
//        ComPointer<ID3D12CommandAllocator> ca;
//        ComPointer<ID3D12GraphicsCommandList> cl;
//        THROW_IF_FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca)));
//        THROW_IF_FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca.Get(), nullptr, IID_PPV_ARGS(&cl)));
//
//        CD3DX12_RESOURCE_BARRIER a1 = CD3DX12_RESOURCE_BARRIER::Transition(
//            gpuIn.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
//        CD3DX12_RESOURCE_BARRIER a2 = CD3DX12_RESOURCE_BARRIER::Transition(
//            gpuIn.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
//
//        cl->ResourceBarrier(1, &a1);
//        cl->CopyBufferRegion(gpuIn.Get(), 0, upload.Get(), 0, byteSize);
//        cl->ResourceBarrier(1, &a2);
//
//        THROW_IF_FAILED(cl->Close());
//        ID3D12CommandList* lists[] = { cl.Get() };
//        q->ExecuteCommandLists(1, lists);
//
//        // Fence로 GPU 완료 대기 → allocator/list 해제 시 크래시 방지
//        ComPointer<ID3D12Fence> fence;
//        THROW_IF_FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
//        UINT64 fv = 1;
//        THROW_IF_FAILED(q->Signal(fence.Get(), fv));
//        if (fence->GetCompletedValue() < fv) {
//            HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
//            THROW_IF_FAILED(fence->SetEventOnCompletion(fv, evt));
//            WaitForSingleObject(evt, INFINITE);
//            CloseHandle(evt);
//        }
//        // 여기서 ca/cl은 스코프 아웃되어도 안전
//
//        // === 4) ORT DML 확장: GPU allocation 핸들 생성 ===
//        const OrtDmlApi* dmlApi = nullptr;
//        Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
//            "DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dmlApi)));
//
//        void* inAlloc = nullptr;
//        Ort::ThrowOnError(dmlApi->CreateGPUAllocationFromD3DResource(gpuIn.Get(), &inAlloc));
//
//        Ort::MemoryInfo miDml("DML", OrtDeviceAllocator, /*deviceId*/0, OrtMemTypeDefault);
//
//        // === 5) IoBinding 실행 (출력은 ORT가 DML 메모리로 자동 할당) ===
//        {
//            // inTensor가 스코프를 벗어나며 포인터 참조 해제 → 그 다음 FreeGPUAllocation
//            Ort::Value inTensor = Ort::Value::CreateTensor(
//                miDml, inAlloc, byteSize,
//                inShape.data(), inShape.size(),
//                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
//
//            Ort::IoBinding binding(*session_);
//            binding.BindInput(inName.get(), inTensor);
//            binding.BindOutput(outName.get(), miDml);
//
//            try {
//                session_->Run(Ort::RunOptions{ nullptr }, binding);
//            }
//            catch (const Ort::Exception& e) {
//                std::string msg = "ORT Run failed: "; msg += e.what(); msg += "\n";
//                OutputDebugStringA(msg.c_str());
//                // 바인딩 정리 후 리턴
//                binding.ClearBoundInputs();
//                binding.ClearBoundOutputs();
//                // inTensor 소멸은 블록 종료 시 자동
//                dmlApi->FreeGPUAllocation(inAlloc);
//                return false;
//            }
//
//            // 바인딩 정리(참조 해제)
//            binding.ClearBoundInputs();
//            binding.ClearBoundOutputs();
//        }
//
//        // === 6) GPU allocation 핸들 해제 (한 번만) ===
//        dmlApi->FreeGPUAllocation(inAlloc);
//
//        OutputDebugStringA("RunGpuSmokeTest: OK\n");
//        return true;
//    }
//    catch (const std::exception& e) {
//        std::string msg = "RunGpuSmokeTest exception: "; msg += e.what(); msg += "\n";
//        OutputDebugStringA(msg.c_str());
//        return false;
//    }
//}

bool OnnxManager::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    dev_ = dev; queue_ = queue;

    so_ = Ort::SessionOptions{};
    so_.DisableMemPattern();
    so_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    so_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // DML1: 내 DX12 디바이스/큐를 사용
    ComPointer<IDMLDevice> dml;
    THROW_IF_FAILED(DMLCreateDevice(dev, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)));

    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
        reinterpret_cast<const void**>(&dmlApi_)));
    Ort::ThrowOnError(dmlApi_->SessionOptionsAppendExecutionProvider_DML1(so_, dml.Get(), queue));

    // 세션 생성
    session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(), so_);

    // IO 메타 읽기
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
        // NCHW 가정: -1 이면 H/W 로 치환(필요시 너 모델에 맞게 조정)
        for (size_t i = 0; i < s.size(); ++i) if (s[i] < 0) {
            // 대충: 채널/배치는 1, 공간은 W/H
            if (i == 2) s[i] = (int64_t)H;
            else if (i == 3) s[i] = (int64_t)W;
            else s[i] = 1;
        }
        };
    fixShape(inShape_); fixShape(outShape_);

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

    // DML GPU allocation 핸들 생성
    Ort::ThrowOnError(dmlApi_->CreateGPUAllocationFromD3DResource(inputBuf_.Get(), &inAlloc_));
    Ort::ThrowOnError(dmlApi_->CreateGPUAllocationFromD3DResource(outputBuf_.Get(), &outAlloc_));
    return true;
}

bool OnnxManager::Run()
{
    // GPU in/out 텐서 바인딩 → 실행
    Ort::MemoryInfo miDml("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault);

    {
        Ort::Value inTensor = Ort::Value::CreateTensor(
            miDml, inAlloc_, inBytes_, inShape_.data(), inShape_.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::Value outTensor = Ort::Value::CreateTensor(
            miDml, outAlloc_, outBytes_, outShape_.data(), outShape_.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::IoBinding binding(*session_);
        binding.BindInput(inName_.c_str(), inTensor);
        binding.BindOutput(outName_.c_str(), outTensor);

        try {
            session_->Run(Ort::RunOptions{ nullptr }, binding);
        }
        catch (const Ort::Exception& e) {
            OutputDebugStringA((std::string("ORT Run failed: ") + e.what() + "\n").c_str());
            binding.ClearBoundInputs(); binding.ClearBoundOutputs();
            return false;
        }
        binding.ClearBoundInputs(); binding.ClearBoundOutputs();
    }
    return true;
}

void OnnxManager::ResizeIO(ID3D12Device* dev, UINT W, UINT H)
{
    // 기존 핸들/버퍼 해제 후 PrepareIO 재호출
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
