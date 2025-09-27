#pragma once
#include <string>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>

class OnnxRunner {
public:
    OnnxRunner();
    ~OnnxRunner();

    static void BilinearResizeCHW(const float* src, int srcC, int srcH, int srcW, float* dst, int dstC, int dstH, int dstW);

    // onnxPath: UTF-8 경로 허용 (내부에서 UTF-16 변환)
    bool Initialize(const std::string& onnxPath, int inputC = 3);

    // 입력: NCHW float32 [0..1] 또는 정규화된 값.
    // 출력: 모델의 첫 번째 출력 텐서를 outBuffer에 복사 (float32)
    bool Run(const float* nchwInput, int n, int c, int h, int w,
        std::vector<float>& outBuffer, std::vector<int64_t>& outShape);

    const std::string& GetInputName() const { return inputName; }
    const std::string& GetOutputName() const { return outputName; }

private:
    // ORT
    Ort::Env env{ nullptr };
    Ort::SessionOptions sessionOpt{ nullptr };
    std::unique_ptr<Ort::Session> session;

    std::string inputName;
    std::string outputName;

    int inputChannels = 3;

    static std::wstring Utf8ToUtf16(const std::string& s);
};
