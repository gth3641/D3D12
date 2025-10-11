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

    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
        reinterpret_cast<const void**>(&m_DmlApi)));
    Ort::ThrowOnError(m_DmlApi->SessionOptionsAppendExecutionProvider_DML1(m_So, dml.Get(), m_Queue));

    m_Session = std::make_unique<Ort::Session>(m_Env, modelPath.c_str(), m_So);
    miDml_ = Ort::MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeDefault);

    Ort::AllocatorWithDefaultOptions alloc;

    // �Է�/��� �̸�/shape ����
    {
        int inputCount = m_Session->GetInputCount();
        if (inputCount != 1) throw std::runtime_error("FastNeuralStyle expects one input");
        auto inName0 = m_Session->GetInputNameAllocated(0, alloc);
        m_InNameContent = inName0.get();
        m_InShapeContent = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();

        int outputCount = m_Session->GetOutputCount();
        if (outputCount != 1) throw std::runtime_error("FastNeuralStyle expects one output");
        auto outName0 = m_Session->GetOutputNameAllocated(0, alloc);
        m_OutName = outName0.get();
        m_OutShape = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    }

    // �� ��� IoBinding ����
    m_Binding = std::make_unique<Ort::IoBinding>(*m_Session);

    return true;
}

bool OnnxRunner_FastNeuralStyle::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    // (����) 4�� ��� ����
    const UINT W = (contentW / 4) * 4;
    const UINT H = (contentH / 4) * 4;

    // �Է� shape Ȯ��
    auto inShapeContent = m_InShapeContent; // ���� [-1,3,-1,-1]
    FillDynamicNCHW(inShapeContent, 1, 3, (int)H, (int)W);
    m_InBytesContent = BytesOf(inShapeContent, sizeof(float));
    if (m_InBytesContent == 0) return false;

    // ���� �Է� �ڿ� ����
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    m_InputBufContent.Release();

    // �Է� ���� ���� (DEFAULT + UAV)
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);
    auto rd = CD3DX12_RESOURCE_DESC::Buffer(m_InBytesContent, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    THROW_IF_FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_InputBufContent)));
    m_InputBufContent->SetName(L"ORT_Input_Content");
    Ort::ThrowOnError(m_DmlApi->CreateGPUAllocationFromD3DResource(m_InputBufContent.Get(), &m_InAllocContent));

    // �Է� �ټ�(DML) ��� ����
    m_InShapeContent = std::move(inShapeContent);
    m_InTensorDML = Ort::Value::CreateTensor(
        miDml_, m_InAllocContent, m_InBytesContent,
        m_InShapeContent.data(), m_InShapeContent.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

    // ��� �ڿ�/���� ����
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();
    m_OutBytes = 0;
    m_OutTensorDML = Ort::Value{ nullptr };
    m_OutShape.clear();
    m_OutputBound = false;

    // �� ��� IoBinding �缼��: �Է� ���ε�, ����� "�߰� ���"
    m_Binding->ClearBoundInputs();
    m_Binding->ClearBoundOutputs();
    m_Binding->BindInput(m_InNameContent.c_str(), m_InTensorDML);
    m_Binding->BindOutput(m_OutName.c_str(), miDml_); // shape discovery��

    return true;
}

bool OnnxRunner_FastNeuralStyle::Run()
{
    try {
        auto bytesOf = [](const std::vector<int64_t>& s) {
            return size_t(s[0]) * size_t(s[1]) * size_t(s[2]) * size_t(s[3]) * sizeof(float);
            };
        if (m_InBytesContent != bytesOf(m_InShapeContent)) return false;

        // 1) ���� ����� ���ε����� �ʾҴٸ�: 1ȸ shape discovery
        if (!m_OutputBound) {
            m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding); // �ӽ� ��¿� ����

            // shape Ȯ��
            auto outs = m_Binding->GetOutputValues();
            auto info = outs[0].GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape(); // [1,3,H,W] ���

            // �� ��� ����/�ټ� �غ�
            AllocateOutputForShape(shape);
            m_OutTensorDML = Ort::Value::CreateTensor(
                miDml_, m_OutAlloc, m_OutBytes,
                m_OutShape.data(), m_OutShape.size(),
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

            // ��� ���ε��� "���� ���ε�"���� ��ȯ
            m_Binding->ClearBoundOutputs(); // �Է��� �״�� ����
            m_Binding->BindOutput(m_OutName.c_str(), m_OutTensorDML);

            m_OutputBound = true;

            // ����: ���� �����ӿ� �� �� �� ������ ���� ��±��� ���
            // (�ʿ� ������ �� ȣ���� �����ص� ��)
            m_Session->Run(Ort::RunOptions{ nullptr }, *m_Binding);
            return true;
        }

        // 2) ��±��� ���� ���ε��� �����ٸ�, �� �������� �׳� Run��!
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

void OnnxRunner_FastNeuralStyle::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release();
    m_OutputBuf.Release();

    // (����) ��Ÿ�� �ڿ� ����
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

    // (����) ��Ÿ�� �ڿ� �� ��Ÿ �ʱ�ȭ
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    m_InputBufStyle.Release();
    m_InBytesStyle = 0;
    m_InShapeStyle.clear();
    m_InNameStyle.clear();

    m_Session.reset();
}

void OnnxRunner_FastNeuralStyle::AllocateOutputForShape(const std::vector<int64_t>& shape)
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