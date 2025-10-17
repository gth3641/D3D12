#include "OnnxRunner_BlindVideo.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif


bool OnnxRunner_BlindVideo::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    m_Dev = dev; 
    m_Queue = queue;

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

    // 입력/출력 이름/shape 쿼리
    {
        int inputCount = m_Session->GetInputCount();
        if (inputCount != 1) throw std::runtime_error("BlindVideo expects exactly ONE input");

        Ort::AllocatorWithDefaultOptions alloc;
        auto inName0 = m_Session->GetInputNameAllocated(0, alloc);
        m_InNameContent = inName0.get();
        m_InShapeContent = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape(); // [-1,6,-1,-1] 권장
        if (m_InShapeContent.size() == 4 && m_InShapeContent[1] > 0 && m_InShapeContent[1] != 6)
            throw std::runtime_error("BlindVideo ONNX expects C=6 (It+Pt). Re-export the model or fix preproc.");

        int outputCount = m_Session->GetOutputCount();
        if (outputCount != 1) throw std::runtime_error("BlindVideo expects exactly ONE output");
        auto outName0 = m_Session->GetOutputNameAllocated(0, alloc);
        m_OutName = outName0.get();
        m_OutShape = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape(); // [-1,3,-1,-1] 권장
    }

    // 멤버 IoBinding
    m_Binding = std::make_unique<Ort::IoBinding>(*m_Session);
    return true;
}

bool OnnxRunner_BlindVideo::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    const UINT W = (contentW / 4) * 4;
    const UINT H = (contentH / 4) * 4;

    auto inShapeContent = m_InShapeContent;
    FillDynamicNCHW(inShapeContent, 1, /*C=*/6, (int)H, (int)W); 
    m_InBytesContent = BytesOf(inShapeContent, sizeof(float));
    if (m_InBytesContent == 0) return false;

    if (m_InAllocContent) 
    {
        m_DmlApi->FreeGPUAllocation(m_InAllocContent); 
        m_InAllocContent = nullptr; 
    }

    m_InputBufContent.Release();

    // 입력 버퍼 생성 (DEFAULT + UAV)
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesContent, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
    m_InputBufContent->SetName(L"ORT_Input_Content");
    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InAllocContent));

    // 입력 텐서(DML)
    m_InShapeContent = std::move(inShapeContent);
    m_InTensorDML = Ort::Value::CreateTensor(
        miDml_, m_InAllocContent, m_InBytesContent,
        m_InShapeContent.data(), m_InShapeContent.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

    // 출력 자원/상태 리셋
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();
    m_OutBytes = 0;
    m_OutTensorDML = Ort::Value{ nullptr };
    m_OutShape.clear();
    m_OutputBound = false;

    // 멤버 IoBinding 재세팅
    m_Binding->ClearBoundInputs();
    m_Binding->ClearBoundOutputs();
    m_Binding->BindInput(m_InNameContent.c_str(), m_InTensorDML);
    m_Binding->BindOutput(m_OutName.c_str(), miDml_); // shape discovery용

    return true;
}

bool OnnxRunner_BlindVideo::Run()
{
    try {
        auto bytesOf = [](const std::vector<int64_t>& s) {
            return size_t(s[0]) * size_t(s[1]) * size_t(s[2]) * size_t(s[3]) * sizeof(float);
            };
        if (m_InBytesContent != bytesOf(m_InShapeContent)) return false;

        // 1) 아직 출력이 바인딩되지 않았다면: 1회 shape discovery
        if (!m_OutputBound) 
        {
            m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);

            // shape 확인
            auto outs = m_Binding->GetOutputValues();
            auto info = outs[0].GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape(); 

            // 내 출력 버퍼/텐서 준비
            AllocateOutputForShape(shape);
            m_OutTensorDML = Ort::Value::CreateTensor(
                miDml_, m_OutAlloc, m_OutBytes,
                m_OutShape.data(), m_OutShape.size(),
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

            // 멤버 바인딩을 고정 바인딩으로 전환
            m_Binding->ClearBoundOutputs(); 
            m_Binding->BindOutput(m_OutName.c_str(), m_OutTensorDML);

            m_OutputBound = true;
            m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);
            return true;
        }

        // 2) 출력까지 고정 바인딩이 끝났다면, 매 프레임은 그냥 Run만!
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

void OnnxRunner_BlindVideo::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (m_InAllocContent) 
    {
        m_DmlApi->FreeGPUAllocation(m_InAllocContent);
        m_InAllocContent = nullptr; 
    }

    if (m_OutAlloc) 
    { 
        m_DmlApi->FreeGPUAllocation(m_OutAlloc);
        m_OutAlloc = nullptr; 
    }

    m_InputBufContent.Release();
    m_OutputBuf.Release();

    // 스타일 자원 정리
    if (m_InAllocStyle) 
    {
        m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   
        m_InAllocStyle = nullptr;
    }

    m_InputBufStyle.Release();
    m_InBytesStyle = 0;
    m_InShapeStyle.clear();
    m_InNameStyle.clear();

    PrepareIO(dev, contentW, contentH, 0, 0);
}

void OnnxRunner_BlindVideo::Shutdown()
{
    if (m_InAllocContent) 
    {
        m_DmlApi->FreeGPUAllocation(m_InAllocContent); 
        m_InAllocContent = nullptr; 
    }

    if (m_OutAlloc) 
    {
        m_DmlApi->FreeGPUAllocation(m_OutAlloc);       
        m_OutAlloc = nullptr; 
    }

    m_InputBufContent.Release();
    m_OutputBuf.Release();

    if (m_InAllocStyle) 
    {
        m_DmlApi->FreeGPUAllocation(m_InAllocStyle); 
        m_InAllocStyle = nullptr; 
    }

    m_InputBufStyle.Release();
    m_InBytesStyle = 0;
    m_InShapeStyle.clear();
    m_InNameStyle.clear();

    m_Session.reset();
}

void OnnxRunner_BlindVideo::AllocateOutputForShape(const std::vector<int64_t>& shape)
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
    if (m_OutAlloc) 
    { 
        m_DmlApi->FreeGPUAllocation(m_OutAlloc); 
        m_OutAlloc = nullptr; 
    }

    m_OutputBuf.Release();

    // 새 출력 버퍼 생성
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(m_OutBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    THROW_IF_FAILED(m_Dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputBuf)));
    m_OutputBuf->SetName(L"ORT_Output");

    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_OutputBuf.Get(), &m_OutAlloc));
}