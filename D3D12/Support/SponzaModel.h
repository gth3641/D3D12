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
    std::unique_ptr<RenderingObject3D> ro;  
    D3D12_GPU_DESCRIPTOR_HANDLE tableBase{};
    std::shared_ptr<Image> keepAlive;       
};

struct ObjImportOptions {
    bool zUpToYUp = true;      
    bool toLeftHanded = true;  
    float uniformScale = 1.0f;
    bool invertUpRotation = false;
};

class SponzaModel {
public:
    bool InitFromOBJ(const std::string& objPath,
        const std::string& baseDir,
        ID3D12PipelineState* pso,
        ID3D12RootSignature* rs,
        const ObjImportOptions& opts = {});

    void Render(ID3D12GraphicsCommandList7* cmd,
        const Camera& cam, float aspect,
        D3D12_CPU_DESCRIPTOR_HANDLE& rtv,
        D3D12_CPU_DESCRIPTOR_HANDLE& dsv,
        float angle = 0.f);

    void Reset(); // 힙/서브메쉬 정리

    void UploadGPUResource(ID3D12GraphicsCommandList7* cmdList);

private:
    std::vector<SponzaSubmesh> m_Subs;
    ComPointer<ID3D12DescriptorHeap> m_Heap;  
    UINT m_DescInc = 0;                
};