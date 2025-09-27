#include "OnnxRunner.h"
#include <stdexcept>
#include <codecvt>
#include <locale>
#include <string>
#include <iostream>
#include <algorithm>

#include <dml_provider_factory.h>
#include "Manager/OnnxManager.h"

OnnxRunner::OnnxRunner() : env(ORT_LOGGING_LEVEL_WARNING, "onnx"),
sessionOpt{} {
    sessionOpt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // DirectML EP�� ���� �ʹٸ� �Ʒ� �� ���� ���� (�̼�ġ ȯ�濡���� CPU�� �ڵ� �����)
    // device_id=0
    OrtSessionOptionsAppendExecutionProvider_DML(sessionOpt, 0);
    // CPU ��ο� ȣȯ�� ���� �޸� ���� ��Ȱ��ȭ + ���� ����
    sessionOpt.DisableMemPattern();
    sessionOpt.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
}

OnnxRunner::~OnnxRunner()
{

}

void OnnxRunner::BilinearResizeCHW(const float* src, int srcC, int srcH, int srcW, float* dst, int dstC, int dstH, int dstW)
{
    for (int c = 0; c < srcC; ++c) {
        const float* s = src + c * srcH * srcW;
        float* d = dst + c * dstH * dstW;
        for (int y = 0; y < dstH; ++y) {
            float gy = (srcH > 1) ? (y * (srcH - 1.0f) / (dstH - 1.0f)) : 0.f;
            int y0 = (int)gy; int y1 = (y0 + 1 < srcH - 1 ? y0 + 1 : srcH - 1);
            float wy = gy - y0;
            for (int x = 0; x < dstW; ++x) {
                float gx = (srcW > 1) ? (x * (srcW - 1.0f) / (dstW - 1.0f)) : 0.f;
                int x0 = (int)gx; int x1 = (x0 + 1 < srcW - 1 ? x0 + 1 : srcW - 1);
                float wx = gx - x0;

                float v00 = s[y0 * srcW + x0];
                float v01 = s[y0 * srcW + x1];
                float v10 = s[y1 * srcW + x0];
                float v11 = s[y1 * srcW + x1];

                float v0 = v00 * (1 - wx) + v01 * wx;
                float v1 = v10 * (1 - wx) + v11 * wx;
                d[y * dstW + x] = v0 * (1 - wy) + v1 * wy;
            }
        }
    }
}

std::wstring OnnxRunner::Utf8ToUtf16(const std::string& s) {
    if (s.empty()) {
        return std::wstring();
    }

    // ��ȯ�� �ʿ��� ���� ũ�⸦ ����մϴ�.
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);

    // ���۸� �Ҵ��մϴ�.
    std::wstring wstr(size_needed, 0);

    // ��ȯ�� �����մϴ�.
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &wstr[0], size_needed);

    return wstr;
}

struct IoMeta {
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::vector<ONNXTensorElementDataType> inputTypes;
    std::vector<std::vector<int64_t>> inputShapes;   // -1�� ����
};
IoMeta meta;

bool OnnxRunner::Initialize(const std::string& onnxPath, int inputC) {
    inputChannels = inputC;
    auto w = Utf8ToUtf16(onnxPath);
    session = std::make_unique<Ort::Session>(env, w.c_str(), sessionOpt);

    Ort::AllocatorWithDefaultOptions alloc;
    // ù ��° �Է�/��� �̸��� ��� (���� I/O �� ����)
    char* inName = session->GetInputNameAllocated(0, alloc).get();
    inputName = inName ? std::string(inName) : std::string();

    char* outName = session->GetOutputNameAllocated(0, alloc).get();
    outputName = outName ? std::string(outName) : std::string();


    // �̸���
    size_t inCount = session->GetInputCount();
    size_t outCount = session->GetOutputCount();
    meta.inputNames.resize(inCount);
    meta.outputNames.resize(outCount);
    meta.inputTypes.resize(inCount);
    meta.inputShapes.resize(inCount);

    for (size_t i = 0; i < inCount; ++i) {
        Ort::AllocatedStringPtr nm = session->GetInputNameAllocated(i, alloc);
        meta.inputNames[i] = nm.get();

        auto info = session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
        meta.inputTypes[i] = info.GetElementType();
        meta.inputShapes[i] = info.GetShape(); // ��: {1,3,224,224} �Ǵ� {-1,3,-1,-1}
    }

    std::vector<std::vector<int64_t>> outputShapes(outCount);
    for (size_t i = 0; i < outCount; ++i) {
        Ort::AllocatedStringPtr nm = session->GetOutputNameAllocated(i, alloc);
        meta.outputNames[i] = nm.get();
    }

    inputName = meta.inputNames[0];   // ���� �Է� ����
    outputName = meta.outputNames[0];  // ���� ��� ����


    return true;
}

bool OnnxRunner::Run(const float* nchwInput, int n, int c, int h, int w,
    std::vector<float>& outBuffer, std::vector<int64_t>& outShape) {
    if (!session) return false;

    // 1) �̸��� ī��Ʈ ��ġ ���� (���� �Է�/��� ����)
    if (meta.inputNames.empty() || meta.outputNames.empty()) return false;

    // 2) dtype Ȯ�� (float32�� ó��)
    if (meta.inputTypes[0] != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        // �ʿ�� ���⿡ FP16/UINT8 ��ȯ �ڵ带 �߰�
        return false;
    }

    // 3) shape Ȯ��
    auto& ms = meta.inputShapes[0]; // ����: {N, C, H, W} with -1 for dynamic
    int64_t mN = ms.size() > 0 ? ms[0] : -1;
    int64_t mC = ms.size() > 1 ? ms[1] : -1;
    int64_t mH = ms.size() > 2 ? ms[2] : -1;
    int64_t mW = ms.size() > 3 ? ms[3] : -1;

    if ((mN > 0 && n != mN) || (mC > 0 && c != mC)) {
        return false; // ��ġ/ä�� ����ġ�� ��������� �ذ� �Ұ�
    }

    const bool needResize =
        (mH > 0 && h != mH) || (mW > 0 && w != mW);

    std::vector<float> resized;   // �ʿ� �� ���� ���
    const float* feed = nchwInput;
    int feedH = h, feedW = w;

    if (needResize) {
        int dstH = (int)(mH > 0 ? mH : h);
        int dstW = (int)(mW > 0 ? mW : w);
        resized.resize((size_t)n * c * dstH * dstW);
        BilinearResizeCHW(nchwInput, c, h, w, resized.data(), c, dstH, dstW);
        feed = resized.data();
        feedH = dstH; feedW = dstW;
    }

    // 4) �ټ� ����
    std::array<int64_t, 4> inShape{ n, c, feedH, feedW };
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value inTensor = Ort::Value::CreateTensor<float>(
        memInfo,
        const_cast<float*>(feed),
        (size_t)n * c * feedH * feedW,
        inShape.data(), inShape.size()
    );

    // 5) �̸� �迭 ���� (�𵨿��� �о�� �̸� ���)
    std::vector<const char*> inNames{ meta.inputNames[0].c_str() };
    std::vector<const char*> outNames{ meta.outputNames[0].c_str() };

    // 6) ���� (���� �޽��� �α� ����)
    std::vector<Ort::Value> outputs;
    try {
        outputs = session->Run(Ort::RunOptions{ nullptr },
            inNames.data(), &inTensor, 1,
            outNames.data(), (size_t)outNames.size());
    }
    catch (const Ort::Exception& e) {
        OutputDebugStringA((std::string("[ONNX][Run] ") + e.what() + "\n").c_str());
        return false;
    }

    if (outputs.empty() || !outputs[0].IsTensor()) return false;

    auto info = outputs[0].GetTensorTypeAndShapeInfo();
    outShape = info.GetShape();
    size_t outCount = info.GetElementCount();

    outBuffer.resize(outCount);
    const float* src = outputs[0].GetTensorData<float>();
    std::memcpy(outBuffer.data(), src, outCount * sizeof(float));
    return true;
}

