#include "OnnxRunner_Sanet.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif

static inline size_t BytesOfNCHW(const std::vector<int64_t>&s) {
    if (s.size() != 4) return 0;
    return size_t(s[0]) * size_t(s[1]) * size_t(s[2]) * size_t(s[3]) * sizeof(float);
}

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

    // 입력/출력 메타
    {
        Ort::AllocatorWithDefaultOptions alloc;
        const int inputCount = m_Session->GetInputCount();
        if (inputCount < 1 || inputCount > 2) throw std::runtime_error("SANet expects 1 or 2 inputs");

        // 이름/shape 수집
        for (int i = 0; i < inputCount; ++i) {
            auto n = m_Session->GetInputNameAllocated(i, alloc);
            auto sh = m_Session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
            std::string name = n.get();
            const bool isStyle = (name.find("style") != std::string::npos) || (name.find("sty") != std::string::npos);

            if (inputCount == 2) {
                if (isStyle) { m_InNameStyle = name; m_InShapeStyle = sh; }
                else { m_InNameContent = name; m_InShapeContent = sh; }
            }
            else { // 단일 입력(C=6)
                m_InNameContent = name; m_InShapeContent = sh; // [1,6,H,W] 권장
            }
        }
        // 2입력인데 스타일 못찾았으면 인덱스 기준으로 보정
        if (m_InNameStyle.empty() && m_Session->GetInputCount() == 2) {
            auto n1 = m_Session->GetInputNameAllocated(1, alloc);
            m_InNameStyle = n1.get();
            m_InShapeStyle = m_Session->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
        }

        const int outputCount = m_Session->GetOutputCount();
        if (outputCount != 1) throw std::runtime_error("SANet expects exactly ONE output");
        auto outName0 = m_Session->GetOutputNameAllocated(0, alloc);
        m_OutName = outName0.get();
        m_OutShape = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    }

    m_Binding = std::make_unique<Ort::IoBinding>(*m_Session);
    return true;
}

bool OnnxRunner_Sanet::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    const bool twoInputs = (m_Session->GetInputCount() == 2);

    {
        const UINT W = (contentW / 4) * 4, H = (contentH / 4) * 4;
        auto inShape = m_InShapeContent;                  // [-1,3/6,-1,-1]
        const int Cfixed = twoInputs ? 3 : 6;
        FillDynamicNCHW(inShape, 1, Cfixed, (int)H, (int)W);
        m_InBytesContent = BytesOf(inShape, sizeof(float));
        if (!m_InBytesContent) return false;

        if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
        m_InputBufContent.Release();

        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesContent, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
        m_InputBufContent->SetName(L"ORT_Input_Content");
        Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InAllocContent));

        m_InShapeContent = std::move(inShape);
        m_InTensorContentDML = Ort::Value::CreateTensor(miDml_, m_InAllocContent, m_InBytesContent,
            m_InShapeContent.data(), m_InShapeContent.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    }

    if (twoInputs) {
        const UINT W = (styleW ? styleW : contentW), H = (styleH ? styleH : contentH);
        auto sh = m_InShapeStyle;                        // [-1,3,-1,-1]
        FillDynamicNCHW(sh, 1, 3, (int)((H / 4) * 4), (int)((W / 4) * 4));
        m_InBytesStyle = BytesOf(sh, sizeof(float));

        if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle); m_InAllocStyle = nullptr; }
        m_InputBufStyle.Release();

        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
        auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesStyle, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufStyle)));
        m_InputBufStyle->SetName(L"ORT_Input_Style");
        Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufStyle.Get(), &m_InAllocStyle));

        m_InShapeStyle = std::move(sh);
        m_InTensorStyleDML = Ort::Value::CreateTensor(miDml_, m_InAllocStyle, m_InBytesStyle,
            m_InShapeStyle.data(), m_InShapeStyle.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    }

    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release(); m_OutBytes = 0; m_OutTensorDML = Ort::Value{ nullptr };
    m_OutShape.clear(); m_OutputBound = false;

    m_Binding->ClearBoundInputs();
    m_Binding->ClearBoundOutputs();

    if (twoInputs) {
        m_Binding->BindInput(m_InNameContent.c_str(), m_InTensorContentDML);
        m_Binding->BindInput(m_InNameStyle.c_str(), m_InTensorStyleDML);
    }
    else {
        m_Binding->BindInput(m_InNameContent.c_str(), m_InTensorContentDML);
    }
    m_Binding->BindOutput(m_OutName.c_str(), miDml_); // shape discovery

    return true;
}

bool OnnxRunner_Sanet::Run()
{
    try {
        // first run: discover output, then pin explicit output binding
        if (!m_OutputBound) {
            m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);

            auto outs = m_Binding->GetOutputValues();
            auto info = outs[0].GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape();               // [1,3,H,W] 기대

            AllocateOutputForShape(shape);

            m_OutTensorDML = Ort::Value::CreateTensor(
                miDml_, m_OutAlloc, m_OutBytes,
                m_OutShape.data(), m_OutShape.size(),
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

            m_Binding->ClearBoundOutputs();
            m_Binding->BindOutput(m_OutName.c_str(), m_OutTensorDML);
            m_OutputBound = true;

            // 실제 출력도 즉시 얻음
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
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release(); m_InputBufStyle.Release(); m_OutputBuf.Release();
    m_InBytesStyle = 0; m_InShapeStyle.clear();

    PrepareIO(dev, contentW, contentH, styleW, styleH);
}

void OnnxRunner_Sanet::Shutdown()
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release(); m_InputBufStyle.Release(); m_OutputBuf.Release();
    m_InBytesStyle = 0; m_InShapeStyle.clear(); m_InNameStyle.clear();
    m_Session.reset();
}

void OnnxRunner_Sanet::AllocateOutputForShape(const std::vector<int64_t>& shape)
{
    if (shape.size() != 4 || shape[0] != 1 || (shape[1] != 3 && shape[1] != 6))
        throw std::runtime_error("Unexpected output shape for SANet");

    m_OutShape = shape;
    uint64_t n = 1; for (auto d : shape) n *= (uint64_t)d;
    m_OutBytes = n * sizeof(float);

    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();

    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_OutBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    THROW_IF_FAILED(m_Dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputBuf)));
    m_OutputBuf->SetName(L"ORT_Output");
    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_OutputBuf.Get(), &m_OutAlloc));
}