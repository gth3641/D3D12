#include "OnnxManager.h"
#include "DirectXManager.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif

// 8�� ����� ����
static inline int AlignDown8(int v) { return (v / 8) * 8; }

// shape�� ����(-1)�� ���� ũ��� �޿��
static void FillDynamicNCHW(std::vector<int64_t>& s, int N, int C, int H, int W)
{
    if (s.size() < 4) s = {N, C, H, W};
    if (s[0] < 0) s[0] = N;
    if (s[1] < 0) s[1] = C;
    if (s[2] < 0) s[2] = H;
    if (s[3] < 0) s[3] = W;
}


bool OnnxManager::Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue)
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
    {
        auto inName0 = m_Session->GetInputNameAllocated(0, alloc);
        m_InNameContent = inName0.get();
        auto info0 = m_Session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        m_InShapeContent = info0.GetShape();
    }
    {
        auto inName1 = m_Session->GetInputNameAllocated(1, alloc);
        m_InNameStyle = inName1.get();
        auto info1 = m_Session->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo();
        m_InShapeStyle = info1.GetShape();
    }
    {
        auto outName0 = m_Session->GetOutputNameAllocated(0, alloc);
        m_OutName = outName0.get();
        auto oinfo0 = m_Session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
        m_OutShape = oinfo0.GetShape(); // ���� ���̸� ���� ���� ����
    }
    return true;
}

bool OnnxManager::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
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

bool OnnxManager::Run()
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

void OnnxManager::ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release();
    m_InputBufStyle.Release();
    m_OutputBuf.Release();

    PrepareIO(dev, contentW, contentH, styleW, styleH);
}


void OnnxManager::Shutdown()
{
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc);       m_OutAlloc = nullptr; }

    m_InputBufContent.Release();
    m_InputBufStyle.Release();
    m_OutputBuf.Release();

    m_Session.reset();
}

bool OnnxManager::RunCPUOnce_Debug()
{
    try {
        // 1) CPU �� �ӽ� �Է� �ټ�: 0.5�� ä���� NaN ���ɼ� ����
        std::vector<int64_t> cshape = { 1,3, (int64_t)m_InShapeContent[2], (int64_t)m_InShapeContent[3] };
        std::vector<int64_t> sshape = { 1,3, (int64_t)m_InShapeStyle[2],   (int64_t)m_InShapeStyle[3] };

        size_t ccount = (size_t)cshape[0] * cshape[1] * cshape[2] * cshape[3];
        size_t scount = (size_t)sshape[0] * sshape[1] * sshape[2] * sshape[3];
        std::vector<float> cbuf(ccount, 0.5f), sbuf(scount, 0.5f);

        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        //  DML EP ������ ���� �� CPU EP ���
        Ort::Session cpu_session(m_Env, L"adain_end2end.onnx", so);

        auto memCPU = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        Ort::Value inC = Ort::Value::CreateTensor<float>(memCPU, cbuf.data(), cbuf.size(), cshape.data(), cshape.size());
        Ort::Value inS = Ort::Value::CreateTensor<float>(memCPU, sbuf.data(), sbuf.size(), sshape.data(), sshape.size());

        const char* in0 = m_InNameContent.c_str();
        const char* in1 = m_InNameStyle.c_str();
        const char* out = m_OutName.c_str();

        std::array<const char*, 2> inNames{ in0,in1 };
        std::array<Ort::Value, 2>  inVals{ std::move(inC), std::move(inS) };
        auto outVals = cpu_session.Run(Ort::RunOptions{ nullptr }, inNames.data(), inVals.data(), 2, &out, 1);

        // min/max ����
        auto info = outVals.front().GetTensorTypeAndShapeInfo();
        auto shape = info.GetShape(); // [1,3,H,W]
        const float* y = outVals.front().GetTensorData<float>();
        size_t n = 1ull * shape[0] * shape[1] * shape[2] * shape[3];
        float mn = +1e9f, mx = -1e9f;
        for (size_t i = 0; i < n; ++i) { mn = (mn < y[i] ? mn : y[i]); mx = (mx < y[i] ? mx : y[i]); }
        char buf[256]; sprintf_s(buf, "[CPU] Y min=%f, max=%f, H=%lld W=%lld\n", mn, mx, (long long)shape[2], (long long)shape[3]);
        OutputDebugStringA(buf);
        return true;
    }
    catch (const Ort::Exception& e) {
        OutputDebugStringA((std::string("[CPU debug] ORT failed: ") + e.what() + "\n").c_str());
        return false;
    }
}

bool OnnxManager::Run_FillInputsHalf_Debug()
{
    //try {
    //    // == 0.5�� GPU �Է� ���� ä��� ==
    //    // ���ε� ���� ���� 0.5f�� ä��� CopyBufferRegion �� UAV barrier �� Fence ���
    //    auto FillBufferWith = [&](ComPointer<ID3D12Resource>& dst, float v) {
    //        UINT64 bytes = dst == m_InputBufContent ? m_InBytesContent : m_InBytesStyle;
    //        ComPointer<ID3D12Resource> up;
    //        CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
    //        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    //        THROW_IF_FAILED(m_Dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
    //            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)));
    //        void* p; up->Map(0, nullptr, &p);
    //        std::fill_n(reinterpret_cast<float*>(p), bytes / sizeof(float), v);
    //        up->Unmap(0, nullptr);

    //        // Ŀ�ǵ帮��Ʈ�� Copy �� UAV barrier
    //        ID3D12GraphicsCommandList* cl = /* �� �� Ŀ�ǵ帮��Ʈ ��� */;
    //        cl->CopyBufferRegion(dst.Get(), 0, up.Get(), 0, bytes);
    //        cl->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(dst.Get()));
    //        // ť ���� + �潺 ��� (DML�� ���� ť ���Ƿ� ���⼭ �Ϸ� ����)
    //        };
    //    FillBufferWith(m_InputBufContent, 0.5f);
    //    FillBufferWith(m_InputBufStyle, 0.5f);

    //    // == ���� Run() (�ռ� �� 2-���� ����) ==
    //    return Run();
    //}
    //catch (...) { return false; }

    return false;
}

void OnnxManager::AllocateOutputForShape(const std::vector<int64_t>& shape)
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

