#pragma once
#include <memory>
#include <vector>
#include <string>
#include "Support/WinInclude.h"
#include "Support/ComPointer.h"
#include "Object/RenderingObject3D.h"
#include <d3d12.h>

struct Camera;

struct SponzaSubmesh {
    std::unique_ptr<RenderingObject3D> ro;   // 지오메트리/드로우
    D3D12_GPU_DESCRIPTOR_HANDLE tableBase{}; // 이 서브가 참조할 (t0,t1) 디스크립터 테이블 베이스
    std::shared_ptr<Image> keepAlive;        // 알베도 텍스처 생존 보장
};

class SponzaModel {
public:
    bool InitFromOBJ(const std::string& objPath,
        const std::string& baseDir,
        ID3D12PipelineState* pso,
        ID3D12RootSignature* rs);

    void Render(ID3D12GraphicsCommandList7* cmd,
        const Camera& cam, float aspect,
        D3D12_CPU_DESCRIPTOR_HANDLE& rtv,
        D3D12_CPU_DESCRIPTOR_HANDLE& dsv,
        float angle = 0.f);

    void Reset(); // 힙/서브메쉬 정리

    void UploadGPUResource(ID3D12GraphicsCommandList7* cmdList);

private:
    std::vector<SponzaSubmesh> mSubs;
    ComPointer<ID3D12DescriptorHeap> mHeap;  // SRV 힙 (서브메쉬*2 슬롯)
    UINT mDescInc = 0;                       // 디스크립터 증가치 캐시
};