#include "OnnxRunner_AdaIN.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif


bool OnnxRunner_AdaIN::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
{
    m_Dev = dev; m_Queue = queue;

    ComPointer<IDMLDevice> dml;
    THROW_IF_FAILED(DMLCreateDevice(m_Dev, DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dml)));

    m_So = Ort::SessionOptions{};
    m_So.DisableMemPattern(); // ����
    m_So.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    m_So.SetIntraOpNumThreads(0);
    m_So.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // DML EP API ȹ�� + ���� ť ����
    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
        "DML", ORT_API_VERSION, reinterpret_cast<const void**>(&m_DmlApi)));
    Ort::ThrowOnError(m_DmlApi->SessionOptionsAppendExecutionProvider_DML1(
        m_So, dml.Get(), m_Queue));

    m_Session = std::make_unique<Ort::Session>(m_Env, modelPath.c_str(), m_So);

    // �� ��ҹ��ڴ� ORT ������ ���� "DML" / "Dml" ��� �����ϳ�, �ϰ��ǰ� "DML" ����
    miDml_ = Ort::MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeDefault);

    // IO ��Ÿ������(�̸�/shape ����)
    Ort::AllocatorWithDefaultOptions alloc;

    int inputCount = m_Session->GetInputCount();
    for (int i = 0; i < inputCount; ++i)
    {
        switch (i)
        {
        case 0:
        {
            auto inName0 = m_Session->GetInputNameAllocated(0, alloc);
            m_InNameContent = inName0.get();
            auto info0 = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
            m_InShapeContent = info0.GetShape();
        }
        break;

        case 1:
        {
            auto inName1 = m_Session->GetInputNameAllocated(1, alloc);
            m_InNameStyle = inName1.get();
            auto info1 = m_Session->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo();
            m_InShapeStyle = info1.GetShape();
        }
        break;
        }
    }
    int outputCount = m_Session->GetOutputCount();
    for (int i = 0; i < outputCount; ++i)
    {
        switch (i)
        {
        case 0:
        {
            auto outName0 = m_Session->GetOutputNameAllocated(0, alloc);
            m_OutName = outName0.get();
            auto oinfo0 = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
            m_OutShape = oinfo0.GetShape(); // ���� ���̸� ���� ���� ����
        }
        break;
        }
    }
    return true;
}

bool OnnxRunner_AdaIN::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    // 1) �Է� shape Ȯ��
    auto inShapeContent = m_InShapeContent; // [-1,3,-1,-1] ��
    auto inShapeStyle = m_InShapeStyle;
    FillDynamicNCHW(inShapeContent, 1, 3, (int)contentH, (int)contentW);
    FillDynamicNCHW(inShapeStyle, 1, 3, (int)styleH, (int)styleW);

    // 2) ����Ʈ �� (�����÷ο� ���� ���� uint64_t�� ����ϴ� BytesOf ��� ����)
    m_InBytesContent = BytesOf(inShapeContent, sizeof(float));
    m_InBytesStyle = BytesOf(inShapeStyle, sizeof(float));
    if (m_InBytesContent == 0 || m_InBytesStyle == 0) return false;

    // 3) ���� �Է� ���ҽ�/�Ҵ� ���� (GPU�� ��� ���̸� ȣ�� �� ����ȭ �ʿ�)
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    m_InputBufContent.Release();
    m_InputBufStyle.Release();

    // 4) UAV ���� ���� + DML allocation ����
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    {
        auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesContent, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
        m_InputBufContent->SetName(L"ORT_Input_Content");
        Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InAllocContent));
    }
    {
        auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesStyle, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufStyle)));
        m_InputBufStyle->SetName(L"ORT_Input_Style");
        Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufStyle.Get(), &m_InAllocStyle));
    }

    // 5) Ȯ���� �Է� shape ���� (�� ������ ��ó��/���ε�/����ġ ��� ��ġ�ؾ� ��)
    m_InShapeContent = std::move(inShapeContent);
    m_InShapeStyle = std::move(inShapeStyle);

    // 6) ����� ���� Run 1ȸ������ ��Ÿ�� shape�� �Ҵ�
    m_OutShape.clear();
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();
    m_OutBytes = 0;

    return true;
}

bool OnnxRunner_AdaIN::Run()
{
    try {
        // === (0) ���� �˻�: ����Ʈ���� shape�� ��ġ�ϴ��� (���� �ܰ� assert ����)
        auto bytesOf = [](const std::vector<int64_t>& s) { return size_t(s[0]) * s[1] * s[2] * s[3] * sizeof(float); };
        if (m_InBytesContent != bytesOf(m_InShapeContent)) return false;
        if (m_InBytesStyle != bytesOf(m_InShapeStyle))   return false;

        // === (1) �Է� ���ε� (GPU �޸�)
        Ort::Value inContent = Ort::Value::CreateTensor(
            miDml_, m_InAllocContent, m_InBytesContent,
            m_InShapeContent.data(), m_InShapeContent.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        Ort::Value inStyle = Ort::Value::CreateTensor(
            miDml_, m_InAllocStyle, m_InBytesStyle,
            m_InShapeStyle.data(), m_InShapeStyle.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        Ort::IoBinding io(*m_Session);
        io.BindInput(m_InNameContent.c_str(), inContent);
        io.BindInput(m_InNameStyle.c_str(), inStyle);

        // === (2) ù ����: ��� ������ �� ORT�� GPU�� �Ҵ� (shape �ľǿ�)
        if (!m_OutAlloc) {
            io.BindOutput(m_OutName.c_str(), miDml_);  // GPU�� �ӽ� ���
            m_Session->Run(Ort::RunOptions{ nullptr }, io);

            // shape ��ȸ
            auto outs = io.GetOutputValues();
            auto info = outs[0].GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape();              // e.g., [1,3,H,W]
            AllocateOutputForShape(shape);             // �� ��� ���ҽ�/DML alloc ����

            // ����ε� �غ�
            io.ClearBoundInputs();
            io.ClearBoundOutputs();

            // �Է� �ٽ� ���ε�
            io.BindInput(m_InNameContent.c_str(), inContent);
            io.BindInput(m_InNameStyle.c_str(), inStyle);
        }

        // === (3) �� ��° ����: �� ��� ���۷� ���
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
        // ����̽� ���� ���� �α�
        char buf[512];
        HRESULT reason = m_Dev ? m_Dev->GetDeviceRemovedReason() : S_OK;
        sprintf_s(buf, "ORT Run failed: %s (GetDeviceRemovedReason=0x%08X)\n", e.what(), (unsigned)reason);
        OutputDebugStringA(buf);
        return false;
    }
}

void OnnxRunner_AdaIN::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release();
    m_InputBufStyle.Release();
    m_OutputBuf.Release();

    PrepareIO(dev, contentW, contentH, styleW, styleH);
}

void OnnxRunner_AdaIN::Shutdown()
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release();
    m_InputBufStyle.Release();
    m_OutputBuf.Release();

    m_Session.reset();
}

void OnnxRunner_AdaIN::AllocateOutputForShape(const std::vector<int64_t>& shape)
{
    // shape = [1,3,H_out,W_out]  (��Ÿ���� �� '��¥' ũ��)
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 3)
        throw std::runtime_error("Unexpected output shape");

    m_OutShape = shape;

    // ����Ʈ �� ���
    uint64_t n = 1;
    for (auto d : shape) n *= static_cast<uint64_t>(d);
    m_OutBytes = n * sizeof(float);

    // ���� ��� ���ҽ� ����
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();

    // �� ��� ���� ����
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(m_OutBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    THROW_IF_FAILED(m_Dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_OutputBuf)));
    m_OutputBuf->SetName(L"ORT_Output");

    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_OutputBuf.Get(), &m_OutAlloc));
}