#include "OnnxRunner_Sanet.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif


bool OnnxRunner_Sanet::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
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

    m_Session = std::make_unique<Ort::Session>(m_Env, modelPath.c_str(), m_So);
    miDml_ = Ort::MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeDefault);

    // 입력/출력 이름/shape
    {
        Ort::AllocatorWithDefaultOptions alloc;

        int inputCount = m_Session->GetInputCount();
        if (inputCount == 2) {
            // ▲ SANet 파이프라인(컨텐츠, 스타일)
            m_TwoInputs = true;

            auto in0 = m_Session->GetInputNameAllocated(0, alloc);
            auto in1 = m_Session->GetInputNameAllocated(1, alloc);
            m_InNameContent = in0.get();
            m_InNameStyle = in1.get();

            m_InShapeContent = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape(); // 기대: [-1,3,-1,-1]
            m_InShapeStyle = m_Session->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape(); // 기대: [-1,3,-1,-1]
            // 고정형 모델이면 C=3, H/W 고정인지 점검
            if (m_InShapeContent.size() == 4 && m_InShapeContent[1] > 0 && m_InShapeContent[1] != 3)
                throw std::runtime_error("SANet expects C=3 for content.");
            if (m_InShapeStyle.size() == 4 && m_InShapeStyle[1] > 0 && m_InShapeStyle[1] != 3)
                throw std::runtime_error("SANet expects C=3 for style.");
        }
        else if (inputCount == 1) {
            // ▲ 병합형(C=6) 호환
            m_TwoInputs = false;

            auto in0 = m_Session->GetInputNameAllocated(0, alloc);
            m_InNameContent = in0.get(); // 이름은 content로 둠
            m_InShapeContent = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape(); // 기대: [-1,6,-1,-1]
            if (m_InShapeContent.size() == 4 && m_InShapeContent[1] > 0 && m_InShapeContent[1] != 6)
                throw std::runtime_error("This model expects C=6 (concat). Export or preproc mismatch.");
        }
        else {
            throw std::runtime_error("Unexpected number of inputs for SANet pipeline.");
        }

        int outputCount = m_Session->GetOutputCount();
        if (outputCount != 1) throw std::runtime_error("SANet pipeline expects exactly ONE output.");
        auto outName0 = m_Session->GetOutputNameAllocated(0, alloc);
        m_OutName = outName0.get();
        m_OutShape = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape(); // 기대: [-1,3,-1,-1]
    }

    m_Binding = std::make_unique<Ort::IoBinding>(*m_Session);
    return true;
}

bool OnnxRunner_Sanet::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    const UINT Wc = (contentW / 4) * 4;
    const UINT Hc = (contentH / 4) * 4;

    if (m_TwoInputs) {
        // ▲ Content: [1,3,Hc,Wc]
        auto scC = m_InShapeContent;
        FillDynamicNCHW(scC, 1, /*C=*/3, (int)Hc, (int)Wc);
        m_InBytesContent = BytesOf(scC, sizeof(float)); if (!m_InBytesContent) return false;

        // ▲ Style: [1,3,Hs,Ws] (없으면 컨텐츠와 동일)
        const UINT Ws = styleW ? (styleW / 4) * 4 : Wc;
        const UINT Hs = styleH ? (styleH / 4) * 4 : Hc;
        auto scS = m_InShapeStyle.empty() ? std::vector<int64_t>{-1, 3, -1, -1} : m_InShapeStyle;
        FillDynamicNCHW(scS, 1, /*C=*/3, (int)Hs, (int)Ws);
        m_InBytesStyle = BytesOf(scS, sizeof(float)); if (!m_InBytesStyle) return false;

        // 기존 입력 자원 정리
        if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
        if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
        m_InputBufContent.Release();
        m_InputBufStyle.Release();

        // Content UAV buffer
        {
            CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
            auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesContent, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
            m_InputBufContent->SetName(L"ORT_Input_Content");
            Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InAllocContent));
            m_InShapeContent = std::move(scC);
            m_InTensorDML = Ort::Value::CreateTensor(miDml_, m_InAllocContent, m_InBytesContent,
                m_InShapeContent.data(), m_InShapeContent.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        }
        // Style UAV buffer
        {
            CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
            auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesStyle, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufStyle)));
            m_InputBufStyle->SetName(L"ORT_Input_Style");
            Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufStyle.Get(), &m_InAllocStyle));
            m_InShapeStyle = std::move(scS);
            m_InTensorDMLStyle = Ort::Value::CreateTensor(miDml_, m_InAllocStyle, m_InBytesStyle,
                m_InShapeStyle.data(), m_InShapeStyle.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        }

        // 출력 리셋
        if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
        m_OutputBuf.Release(); m_OutBytes = 0; m_OutTensorDML = Ort::Value{ nullptr };
        m_OutShape.clear(); m_OutputBound = false;

        // IoBinding 갱신
        m_Binding->ClearBoundInputs();
        m_Binding->ClearBoundOutputs();
        m_Binding->BindInput(m_InNameContent.c_str(), m_InTensorDML);
        m_Binding->BindInput(m_InNameStyle.c_str(), m_InTensorDMLStyle);
        m_Binding->BindOutput(m_OutName.c_str(), miDml_); // discovery
        return true;
    }
    else {
        // ▲ 병합형(C=6) 기존 로직 (기존 코드 거의 그대로)
        auto inShapeContent = m_InShapeContent;
        FillDynamicNCHW(inShapeContent, 1, /*C=*/6, (int)Hc, (int)Wc);
        m_InBytesContent = BytesOf(inShapeContent, sizeof(float)); if (!m_InBytesContent) return false;

        if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
        m_InputBufContent.Release();

        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesContent, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
        m_InputBufContent->SetName(L"ORT_Input_Content");
        Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InAllocContent));

        m_InShapeContent = std::move(inShapeContent);
        m_InTensorDML = Ort::Value::CreateTensor(miDml_, m_InAllocContent, m_InBytesContent,
            m_InShapeContent.data(), m_InShapeContent.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
        m_OutputBuf.Release(); m_OutBytes = 0; m_OutTensorDML = Ort::Value{ nullptr };
        m_OutShape.clear(); m_OutputBound = false;

        m_Binding->ClearBoundInputs();
        m_Binding->ClearBoundOutputs();
        m_Binding->BindInput(m_InNameContent.c_str(), m_InTensorDML);
        m_Binding->BindOutput(m_OutName.c_str(), miDml_); // discovery
        return true;
    }
}

bool OnnxRunner_Sanet::Run()
{
    try {
        auto bytesOf = [](const std::vector<int64_t>& s) {
            return size_t(s[0]) * size_t(s[1]) * size_t(s[2]) * size_t(s[3]) * sizeof(float);
            };

        if (m_TwoInputs) {
            if (m_InBytesContent != bytesOf(m_InShapeContent)) return false;
            if (m_InBytesStyle != bytesOf(m_InShapeStyle))   return false;
        }
        else {
            if (m_InBytesContent != bytesOf(m_InShapeContent)) return false;
        }

        if (!m_OutputBound) {
            m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);
            auto outs = m_Binding->GetOutputValues();
            auto info = outs[0].GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape(); // 기대: [1,3,H,W]

            AllocateOutputForShape(shape);
            m_OutTensorDML = Ort::Value::CreateTensor(miDml_, m_OutAlloc, m_OutBytes,
                m_OutShape.data(), m_OutShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

            m_Binding->ClearBoundOutputs();
            m_Binding->BindOutput(m_OutName.c_str(), m_OutTensorDML);

            m_OutputBound = true;
            m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);
            return true;
        }

        m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);
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

void OnnxRunner_Sanet::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
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
    //m_InNameStyle.clear();

    PrepareIO(dev, contentW, contentH, 0, 0);
}

void OnnxRunner_Sanet::Shutdown()
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

void OnnxRunner_Sanet::AllocateOutputForShape(const std::vector<int64_t>& shape)
{
    // shape = [1,3,H_out,W_out]  (런타임이 준 '진짜' 크기)
    if (shape.size() != 4 || shape[0] != 1 || (shape[1] != 3 && shape[1] != 6))
        throw std::runtime_error("Unexpected output shape for BlindVideo");

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