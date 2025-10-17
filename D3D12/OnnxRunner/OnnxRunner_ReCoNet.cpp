#include "OnnxRunner_ReCoNet.h"
#include "D3D/DXContext.h"
#include "Manager/DirectXManager.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif

static inline std::wstring ToW(const std::string & s) 
{
    return std::wstring(s.begin(), s.end());
}

bool OnnxRunner_ReCoNet::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    m_Dev = dev; m_Queue = queue;

    m_So = Ort::SessionOptions{};
    m_So.DisableMemPattern();                                
    m_So.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);    
    m_So.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    ComPointer<IDMLDevice> dml;
    THROW_IF_FAILED(DMLCreateDevice(m_Dev, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)));
    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
        reinterpret_cast<const void**>(&m_DmlApi)));
    Ort::ThrowOnError(m_DmlApi->SessionOptionsAppendExecutionProvider_DML1(m_So, dml.Get(), m_Queue));

    m_Session = std::make_unique<Ort::Session>(m_Env, modelPath.c_str(), m_So);

    // IO 이름/shape
    {
        Ort::AllocatorWithDefaultOptions a;
        m_InputName = m_Session->GetInputNameAllocated(0, a).get();
        m_OutputName = m_Session->GetOutputNameAllocated(0, a).get();
        m_ModelInShape = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        m_ModelOutShape = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    }

    m_Binding = std::make_unique<Ort::IoBinding>(*m_Session);
    return true;
}

bool OnnxRunner_ReCoNet::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    // 8의 배수 정렬 권장
    m_W = AlignDown8((int)contentW);
    m_H = AlignDown8((int)contentH);

    m_InShapeContent = m_ModelInShape;  FillDynamicNCHW(m_InShapeContent, 1, 3, (int)m_H, (int)m_W);
    m_OutShape = m_ModelOutShape; FillDynamicNCHW(m_OutShape, 1, 3, (int)m_H, (int)m_W);

    m_InputBytes = BytesOf(m_InShapeContent, sizeof(float));
    m_OutputBytes = BytesOf(m_OutShape, sizeof(float));

    // D3D12 GPU 버퍼(UAV)
    {
        auto hpDef = HeapDefault();
        auto dIn = MakeBufDesc(m_InputBytes);
        auto dOut = MakeBufDesc(m_OutputBytes);
        THROW_IF_FAILED(m_Dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &dIn,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
        THROW_IF_FAILED(m_Dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &dOut,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputBuf)));
    }

    if (m_InDmlAlloc) { m_DmlApi->FreeGPUAllocation(m_InDmlAlloc);  m_InDmlAlloc = nullptr; }
    if (m_OutDmlAlloc) { m_DmlApi->FreeGPUAllocation(m_OutDmlAlloc); m_OutDmlAlloc = nullptr; }

    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InDmlAlloc));
    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_OutputBuf.Get(), &m_OutDmlAlloc));

    Ort::MemoryInfo miDml("DML", OrtDeviceAllocator, /*device_id*/0, OrtMemTypeDefault);

    std::array<int64_t, 4> inShape{ m_InShapeContent[0],  m_InShapeContent[1],  m_InShapeContent[2],  m_InShapeContent[3] };
    std::array<int64_t, 4> outShape{ m_OutShape[0],        m_OutShape[1],        m_OutShape[2],        m_OutShape[3] };

    m_InTensorDML = Ort::Value::CreateTensor(miDml, m_InDmlAlloc, m_InputBytes,
        inShape.data(), inShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    m_OutTensorDML = Ort::Value::CreateTensor(miDml, m_OutDmlAlloc, m_OutputBytes,
        outShape.data(), outShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

    // IoBinding: 제로-카피 바인딩
    m_Binding->ClearBoundInputs();
    m_Binding->ClearBoundOutputs();
    m_Binding->BindInput(m_InputName.c_str(), m_InTensorDML);
    m_Binding->BindOutput(m_OutputName.c_str(), m_OutTensorDML);

    return true;

}

bool OnnxRunner_ReCoNet::Run()
{
    try {
        // 전처리 디스패치가 같은 큐에 제출되어 있으면 순서 보장됨
        m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);  // 제로-카피
        return true;
    }
    catch (const Ort::Exception& e) {
        OutputDebugStringA(("ORT Exception: " + std::string(e.what()) + "\n").c_str());
        return false;
    }
}

void OnnxRunner_ReCoNet::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    // 기존 리소스 해제 후 재준비
    m_InputBufContent.Release();
    m_InputBufStyle.Release();
    m_OutputBuf.Release();

    PrepareIO(dev, contentW, contentH, styleW, styleH);
}

void OnnxRunner_ReCoNet::Shutdown()
{
    m_Session.reset();
    if (m_InDmlAlloc) { m_DmlApi->FreeGPUAllocation(m_InDmlAlloc);  m_InDmlAlloc = nullptr; }
    if (m_OutDmlAlloc) { m_DmlApi->FreeGPUAllocation(m_OutDmlAlloc); m_OutDmlAlloc = nullptr; }

    m_Session.reset();
    m_InputBufContent.Release();
    m_InputBufStyle.Release();
    m_OutputBuf.Release();
    m_InAllocContent = m_InAllocStyle = m_OutAlloc = nullptr;
}

void OnnxRunner_ReCoNet::AllocateOutputForShape(const std::vector<int64_t>& shape)
{
    // shape 기반으로 출력만 재할당하고 싶을 때 사용
    m_OutShape = shape;
    m_OutputBytes = BytesOf(m_OutShape, sizeof(float));

    m_OutputBuf.Release();

    auto hpDef = HeapDefault();
    auto hpUP = HeapUpload();

    auto dOut = MakeBufDesc(m_OutputBytes);
    THROW_IF_FAILED(m_Dev->CreateCommittedResource(
        &hpDef, D3D12_HEAP_FLAG_NONE, &dOut,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputBuf)));

    auto dUp = MakeBufDesc(m_OutputBytes, D3D12_RESOURCE_FLAG_NONE);
}

D3D12_RESOURCE_DESC OnnxRunner_ReCoNet::MakeBufDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags) const
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Alignment = 0;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc = { 1,0 };
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    return d;
}

D3D12_HEAP_PROPERTIES OnnxRunner_ReCoNet::HeapDefault() const
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    return hp;
}

D3D12_HEAP_PROPERTIES OnnxRunner_ReCoNet::HeapUpload() const
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    return hp;
}

D3D12_HEAP_PROPERTIES OnnxRunner_ReCoNet::HeapReadback() const
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    return hp;
}

void OnnxRunner_ReCoNet::CopyDefaultToReadback(ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, ID3D12Resource* dst, size_t bytes)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = src;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);

    cmd->CopyBufferRegion(dst, 0, src, 0, bytes);

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cmd->ResourceBarrier(1, &b);
}

void OnnxRunner_ReCoNet::CopyUploadToDefault(ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, ID3D12Resource* dst, size_t bytes)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = dst;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);

    cmd->CopyBufferRegion(dst, 0, src, 0, bytes);

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cmd->ResourceBarrier(1, &b);
}
