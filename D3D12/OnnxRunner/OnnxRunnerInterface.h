#pragma once

#include "D3D/DXContext.h"
#include "Support/ComPointer.h"
#include "Util/Util.h"

#include <string>
#include <vector>
#include <wrl.h>
#include <DirectML.h>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

class OnnxRunnerInterface
{

protected:
    // 8의 배수로 내림
    inline int AlignDown8(int v) { return (v / 8) * 8; }

    // shape의 음수(-1)를 실제 크기로 메우기
    void FillDynamicNCHW(std::vector<int64_t>& s, int N, int C, int H, int W)
    {
        if (s.size() < 4) s = { N, C, H, W };
        if (s[0] < 0) s[0] = N;
        if (s[1] < 0) s[1] = C;
        if (s[2] < 0) s[2] = H;
        if (s[3] < 0) s[3] = W;
    }


public: // Functions
    // ONNX Runtime + DirectML EP 초기화
    virtual bool Init(const std::wstring& modelPath, ID3D12Device* dev, ID3D12CommandQueue* queue) { return false; }

    // 가변 해상도 준비(권장): content/style을 각각 지정
    virtual bool PrepareIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) { return false; }

    // 호환 오버로드: style = content
    bool PrepareIO(ID3D12Device* dev, UINT W, UINT H) { return PrepareIO(dev, W, H, W, H); }

    // 실행(현재 바인딩된 GPU 버퍼를 사용)
    virtual bool Run() { return false; }

    // 리사이즈(권장)
    virtual void ResizeIO(ID3D12Device* dev, UINT contentW, UINT contentH, UINT styleW, UINT styleH) {}

    // 호환 오버로드: style = content
    void ResizeIO(ID3D12Device* dev, UINT W, UINT H) { ResizeIO(dev, W, H, W, H); }

    // 종료
    virtual void Shutdown() {}

    //===========Getter=================//
    ComPointer<ID3D12Resource> GetOutputBuffer()       const { return m_OutputBuf; }
    ComPointer<ID3D12Resource> GetInputBufferContent() const { return m_InputBufContent; }
    ComPointer<ID3D12Resource> GetInputBufferStyle()   const { return m_InputBufStyle; }
    const std::vector<int64_t>& GetOutputShape()       const { return m_OutShape; }
    const std::vector<int64_t>& GetInputShapeContent() const { return m_InShapeContent; }
    const std::vector<int64_t>& GetInputShapeStyle()   const { return m_InShapeStyle; }
    //==================================//

    virtual void AllocateOutputForShape(const std::vector<int64_t>& shape) {}
protected: // Functions
    inline uint64_t BytesOf(const std::vector<int64_t>& shape, size_t elemBytes)
    {
        uint64_t n = 1;
        for (auto d : shape)
            n *= static_cast<uint64_t>(d > 0 ? d : 1); // 동적(-1) 차원은 임시로 1로 취급
        return n * static_cast<uint64_t>(elemBytes);
    }
protected: // Variables

    // ORT/DML
    Ort::Env           m_Env{ ORT_LOGGING_LEVEL_WARNING, "app" };
    Ort::SessionOptions m_So;
    std::unique_ptr<Ort::Session> m_Session;
    const OrtDmlApi* m_DmlApi = nullptr;

    // DML 메모리 정보 (GPU)
    Ort::MemoryInfo miDml_{ nullptr }; // Init에서 "DML"로 채움

    // 디바이스/큐(약한 참조 래핑)
    ComPointer<ID3D12Device>        m_Dev;
    ComPointer<ID3D12CommandQueue>  m_Queue;

protected:
    // 모델 IO 이름
    std::string m_InNameContent; // input[0] : "content"
    std::string m_InNameStyle;   // input[1] : "style"
    std::string m_OutName;       // output[0]: "stylized"

    // 모델 IO shape (동적일 수 있음; PrepareIO 후 확정치로 갱신)
    std::vector<int64_t> m_InShapeContent; // 보통 [-1,3,-1,-1] → [1,3,Hc,Wc]
    std::vector<int64_t> m_InShapeStyle;   // 보통 [-1,3,-1,-1] → [1,3,Hs,Ws]
    std::vector<int64_t> m_OutShape;       // 보통 [-1,3,-1,-1] → [1,3,Ho,Wo] (Ho/Wo는 8의 배수)

    // GPU 버퍼 + DML allocation 핸들
    ComPointer<ID3D12Resource> m_InputBufContent; // content NCHW FP32
    ComPointer<ID3D12Resource> m_InputBufStyle;   // style   NCHW FP32
    ComPointer<ID3D12Resource> m_OutputBuf;       // output  NCHW FP32
    void* m_InAllocContent = nullptr; // DML GPU allocation handle
    void* m_InAllocStyle = nullptr;
    void* m_OutAlloc = nullptr;

    // 바이트 크기
    UINT64 m_InBytesContent = 0;
    UINT64 m_InBytesStyle = 0;
    UINT64 m_OutBytes = 0;


};

