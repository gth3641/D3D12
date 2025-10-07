#include "OnnxRunner_FastNeuralStyle.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif


bool OnnxRunner_FastNeuralStyle::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    m_Dev = dev; m_Queue = queue;

    ComPointer<IDMLDevice> dml;
    THROW_IF_FAILED(DMLCreateDevice(m_Dev, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)));

    m_So = Ort::SessionOptions{};
    m_So.DisableMemPattern();
    m_So.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    m_So.SetIntraOpNumThreads(0);
    m_So.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // DML EP attach
    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
        reinterpret_cast<const void**>(&m_DmlApi)));
    Ort::ThrowOnError(m_DmlApi->SessionOptionsAppendExecutionProvider_DML1(m_So, dml.Get(), m_Queue));

    m_Session = std::make_unique<Ort::Session>(m_Env, modelPath.c_str(), m_So);
    miDml_ = Ort::MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeDefault);

    Ort::AllocatorWithDefaultOptions alloc;

    // === 입력은 정확히 1개여야 함
    int inputCount = m_Session->GetInputCount();
    if (inputCount != 1) {
        throw std::runtime_error("FastNeuralStyle runner expects exactly ONE input tensor");
    }
    {
        auto inName0 = m_Session->GetInputNameAllocated(0, alloc);
        m_InNameContent = inName0.get();
        auto info0 = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        m_InShapeContent = info0.GetShape(); // 보통 [-1,3,-1,-1]
    }

    // === 출력도 1개
    int outputCount = m_Session->GetOutputCount();
    if (outputCount != 1) {
        throw std::runtime_error("FastNeuralStyle runner expects exactly ONE output tensor");
    }
    {
        auto outName0 = m_Session->GetOutputNameAllocated(0, alloc);
        m_OutName = outName0.get();
        auto oinfo0 = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
        m_OutShape = oinfo0.GetShape(); // 동적일 수 있음
    }
    return true;
}

bool OnnxRunner_FastNeuralStyle::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    // 1) 입력 shape 확정
    auto inShapeContent = m_InShapeContent; // 예: [-1,3,-1,-1]
    FillDynamicNCHW(inShapeContent, 1, 3, (int)contentH, (int)contentW);

    // 2) 바이트 수
    m_InBytesContent = BytesOf(inShapeContent, sizeof(float));
    if (m_InBytesContent == 0) return false;

    // 3) 기존 입력 리소스/할당 해제
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    m_InputBufContent.Release();

    // (1입력 전용) 스타일 관련은 모두 정리하고 비움
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle); m_InAllocStyle = nullptr; }
    m_InputBufStyle.Release();
    m_InBytesStyle = 0;
    m_InShapeStyle.clear();
    m_InNameStyle.clear();

    // 4) 콘텐츠 입력 버퍼 생성
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesContent, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
    m_InputBufContent->SetName(L"ORT_Input_Content");
    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InAllocContent));

    // 5) 확정된 입력 shape 저장
    m_InShapeContent = std::move(inShapeContent);

    // 6) 출력은 다음 Run 1회차에서 할당
    m_OutShape.clear();
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();
    m_OutBytes = 0;

    return true;
}

bool OnnxRunner_FastNeuralStyle::Run()
{
    try {
        auto bytesOf = [](const std::vector<int64_t>& s) {
            return size_t(s[0]) * size_t(s[1]) * size_t(s[2]) * size_t(s[3]) * sizeof(float);
            };
        if (m_InBytesContent != bytesOf(m_InShapeContent)) return false;

        // 입력 텐서 생성 (GPU)
        Ort::Value inContent = Ort::Value::CreateTensor(
            miDml_, m_InAllocContent, m_InBytesContent,
            m_InShapeContent.data(), m_InShapeContent.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::IoBinding io(*m_Session);
        io.BindInput(m_InNameContent.c_str(), inContent);

        // 1회차: 출력 미지정 → ORT가 shape 파악용 임시 출력 할당
        if (!m_OutAlloc) {
            io.BindOutput(m_OutName.c_str(), miDml_);
            m_Session->Run(Ort::RunOptions{ nullptr }, io);

            // shape 조회 후 내 출력 버퍼 생성
            auto outs = io.GetOutputValues();
            auto info = outs[0].GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape();   // [1,3,H,W]
            AllocateOutputForShape(shape);

            // 재바인딩
            io.ClearBoundInputs();
            io.ClearBoundOutputs();
            io.BindInput(m_InNameContent.c_str(), inContent);
        }

        // 2회차: 내 출력 버퍼로 기록
        Ort::Value outTensor = Ort::Value::CreateTensor(
            miDml_, m_OutAlloc, m_OutBytes,
            m_OutShape.data(), m_OutShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        io.BindOutput(m_OutName.c_str(), outTensor);

        m_Session->Run(Ort::RunOptions{ nullptr }, io);

        io.ClearBoundInputs();
        io.ClearBoundOutputs();
        return true;
    }
    catch (const Ort::Exception& e) {
        char buf[512];
        HRESULT reason = m_Dev ? m_Dev->GetDeviceRemovedReason() : S_OK;
        sprintf_s(buf, "ORT Run failed: %s (GetDeviceRemovedReason=0x%08X)\n", e.what(), (unsigned)reason);
        OutputDebugStringA(buf);
        return false;
    }
}

void OnnxRunner_FastNeuralStyle::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release();
    m_OutputBuf.Release();

    // (안전) 스타일 자원 정리
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    m_InputBufStyle.Release();
    m_InBytesStyle = 0;
    m_InShapeStyle.clear();
    m_InNameStyle.clear();

    PrepareIO(dev, contentW, contentH, 0, 0);
}

void OnnxRunner_FastNeuralStyle::Shutdown()
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release();
    m_OutputBuf.Release();

    // (안전) 스타일 자원 및 메타 초기화
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    m_InputBufStyle.Release();
    m_InBytesStyle = 0;
    m_InShapeStyle.clear();
    m_InNameStyle.clear();

    m_Session.reset();
}

void OnnxRunner_FastNeuralStyle::AllocateOutputForShape(const std::vector<int64_t>& shape)
{
    // shape = [1,3,H_out,W_out]  (런타임이 준 '진짜' 크기)
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 3)
        throw std::runtime_error("Unexpected output shape");

    m_OutShape = shape;

    // 바이트 수 계산
    uint64_t n = 1;
    for (auto d : shape) n *= static_cast<uint64_t>(d);
    m_OutBytes = n * sizeof(float);

    // 기존 출력 리소스 해제
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();

    // 새 출력 버퍼 생성
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(m_OutBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    THROW_IF_FAILED(m_Dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputBuf)));
    m_OutputBuf->SetName(L"ORT_Output");

    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_OutputBuf.Get(), &m_OutAlloc));
}