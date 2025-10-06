#include "OnnxManager.h"
#include "DirectXManager.h"
#include "D3D/DXContext.h"

#include "d3dx12.h"
#include "d3d12.h"
#include <memory>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(hrcall) do { HRESULT _hr=(hrcall); if(FAILED(_hr)) { throw std::runtime_error("HRESULT failed"); } } while(0)
#endif

// 8의 배수로 내림
static inline int AlignDown8(int v) { return (v / 8) * 8; }

// shape의 음수(-1)를 실제 크기로 메우기
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
    m_So.DisableMemPattern(); // 유지
    m_So.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    m_So.SetIntraOpNumThreads(0);
    m_So.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // DML EP API 획득 + 동일 큐 연결
    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
        "DML", ORT_API_VERSION, reinterpret_cast<const void**>(&m_DmlApi)));
    Ort::ThrowOnError(m_DmlApi->SessionOptionsAppendExecutionProvider_DML1(
        m_So, dml.Get(), m_Queue));

    m_Session = std::make_unique<Ort::Session>(m_Env, modelPath.c_str(), m_So);

    // ★ 대소문자는 ORT 버전에 따라 "DML" / "Dml" 모두 동작하나, 일관되게 "DML" 권장
    miDml_ = Ort::MemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeDefault);

    // IO 메타데이터(이름/shape 복사)
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
        m_OutShape = oinfo0.GetShape(); // 동적 모델이면 보통 음수 포함
    }
    return true;
}

bool OnnxManager::PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH)
{
    // 1) 입력 shape 확정
    auto inShapeContent = m_InShapeContent; // [-1,3,-1,-1] 등
    auto inShapeStyle = m_InShapeStyle;
    FillDynamicNCHW(inShapeContent, 1, 3, (int)contentH, (int)contentW);
    FillDynamicNCHW(inShapeStyle, 1, 3, (int)styleH, (int)styleW);

    // 2) 바이트 수 (오버플로우 방지 위해 uint64_t로 계산하는 BytesOf 사용 권장)
    m_InBytesContent = BytesOf(inShapeContent, sizeof(float));
    m_InBytesStyle = BytesOf(inShapeStyle, sizeof(float));
    if (m_InBytesContent == 0 || m_InBytesStyle == 0) return false;

    // 3) 기존 입력 리소스/할당 해제 (GPU가 사용 중이면 호출 전 동기화 필요)
    if (m_InAllocContent) { m_DmlApi->FreeGPUAllocation(m_InAllocContent); m_InAllocContent = nullptr; }
    if (m_InAllocStyle) { m_DmlApi->FreeGPUAllocation(m_InAllocStyle);   m_InAllocStyle = nullptr; }
    m_InputBufContent.Release();
    m_InputBufStyle.Release();

    // 4) UAV 버퍼 생성 + DML allocation 생성
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

    // 5) 확정된 입력 shape 저장 (이 값으로 전처리/바인딩/디스패치 모두 일치해야 함)
    m_InShapeContent = std::move(inShapeContent);
    m_InShapeStyle = std::move(inShapeStyle);

    // 6) 출력은 다음 Run 1회차에서 런타임 shape로 할당
    m_OutShape.clear();
    if (m_OutAlloc) { m_DmlApi->FreeGPUAllocation(m_OutAlloc); m_OutAlloc = nullptr; }
    m_OutputBuf.Release();
    m_OutBytes = 0;

    return true;
}

bool OnnxManager::Run()
{
    try {
        // === (0) 안전 검사: 바이트수와 shape가 일치하는지 (개발 단계 assert 권장)
        auto bytesOf = [](const std::vector<int64_t>& s) { return size_t(s[0]) * s[1] * s[2] * s[3] * sizeof(float); };
        if (m_InBytesContent != bytesOf(m_InShapeContent)) return false;
        if (m_InBytesStyle != bytesOf(m_InShapeStyle))   return false;

        // === (1) 입력 바인딩 (GPU 메모리)
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

        // === (2) 첫 실행: 출력 미지정 → ORT가 GPU에 할당 (shape 파악용)
        if (!m_OutAlloc) {
            io.BindOutput(m_OutName.c_str(), miDml_);  // GPU에 임시 출력
            m_Session->Run(Ort::RunOptions{ nullptr }, io);

            // shape 조회
            auto outs = io.GetOutputValues();
            auto info = outs[0].GetTensorTypeAndShapeInfo();
            auto shape = info.GetShape();              // e.g., [1,3,H,W]
            AllocateOutputForShape(shape);             // 내 출력 리소스/DML alloc 생성

            // 재바인딩 준비
            io.ClearBoundInputs();
            io.ClearBoundOutputs();

            // 입력 다시 바인딩
            io.BindInput(m_InNameContent.c_str(), inContent);
            io.BindInput(m_InNameStyle.c_str(), inStyle);
        }

        // === (3) 두 번째 실행: 내 출력 버퍼로 기록
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
        // 디바이스 제거 원인 로깅
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
        // 1) CPU 쪽 임시 입력 텐서: 0.5로 채워서 NaN 가능성 제거
        std::vector<int64_t> cshape = { 1,3, (int64_t)m_InShapeContent[2], (int64_t)m_InShapeContent[3] };
        std::vector<int64_t> sshape = { 1,3, (int64_t)m_InShapeStyle[2],   (int64_t)m_InShapeStyle[3] };

        size_t ccount = (size_t)cshape[0] * cshape[1] * cshape[2] * cshape[3];
        size_t scount = (size_t)sshape[0] * sshape[1] * sshape[2] * sshape[3];
        std::vector<float> cbuf(ccount, 0.5f), sbuf(scount, 0.5f);

        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        //  DML EP 붙이지 않음 → CPU EP 사용
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

        // min/max 찍어보기
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
    //    // == 0.5로 GPU 입력 버퍼 채우기 ==
    //    // 업로드 버퍼 만들어서 0.5f로 채우고 CopyBufferRegion → UAV barrier → Fence 대기
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

    //        // 커맨드리스트로 Copy → UAV barrier
    //        ID3D12GraphicsCommandList* cl = /* 네 쪽 커맨드리스트 얻기 */;
    //        cl->CopyBufferRegion(dst.Get(), 0, up.Get(), 0, bytes);
    //        cl->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(dst.Get()));
    //        // 큐 제출 + 펜스 대기 (DML이 같은 큐 쓰므로 여기서 완료 보장)
    //        };
    //    FillBufferWith(m_InputBufContent, 0.5f);
    //    FillBufferWith(m_InputBufStyle, 0.5f);

    //    // == 이후 Run() (앞서 준 2-스텝 구현) ==
    //    return Run();
    //}
    //catch (...) { return false; }

    return false;
}

void OnnxManager::AllocateOutputForShape(const std::vector<int64_t>& shape)
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

