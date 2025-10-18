#include "SponzaModel.h"

#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"
#include "Object/RenderingObject3D.h"
#include "Support/tiny_obj_loader.h"
#include <unordered_map>

static inline std::string NormalizeSlashes(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

static inline std::string JoinPath(const std::string& base, const std::string& rel) {
    std::string b = NormalizeSlashes(base);
    std::string r = NormalizeSlashes(rel);
    if (b.empty()) return r;
    if (b.back() == '/') return b + r;
    return b + "/" + r;
}

void SponzaModel::Reset() {
    mSubs.clear();
    mHeap.Release();
    mDescInc = 0;
}

bool SponzaModel::InitFromOBJ(const std::string& objPath, const std::string& baseDir,
    ID3D12PipelineState* pso, ID3D12RootSignature* rs)
{
    Reset();

    // 1) OBJ 로드
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    const bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
        objPath.c_str(), baseDir.c_str(), /*triangulate*/ true);
    if (!ok) {
        OutputDebugStringA(("[Sponza] LoadObj failed: " + warn + err + "\n").c_str());
        return false;
    }

    // 2) material -> albedo 경로
    std::vector<std::string> matAlbedo(materials.size());
    for (size_t m = 0; m < materials.size(); ++m) {
        const auto& kd = materials[m].diffuse_texname; // map_Kd
        matAlbedo[m] = kd.empty() ? "" : JoinPath(baseDir, kd);
    }

    // 3) 머티리얼 단위로 서브메시 누적 + 정점 dedup
    struct CPUSub {
        std::vector<Vtx>         verts;
        std::vector<uint32_t>    indices;
        int                      mat = -1;
        struct Key { int vi, ti, ni; };
        struct KeyHash {
            size_t operator()(const Key& k) const noexcept {
                return (size_t)k.vi * 73856093u ^ (size_t)k.ti * 19349663u ^ (size_t)k.ni * 83492791u;
            }
        };
        struct KeyEq {
            bool operator()(const Key& a, const Key& b) const noexcept {
                return a.vi == b.vi && a.ti == b.ti && a.ni == b.ni;
            }
        };
        std::unordered_map<Key, uint32_t, KeyHash, KeyEq> dedup;
    };

    std::vector<CPUSub> subs; subs.reserve(materials.size() + 1);
    std::unordered_map<int, size_t> mat2sub; mat2sub.reserve(materials.size() + 1);

    auto getSubIndex = [&](int matID)->size_t {
        auto it = mat2sub.find(matID);
        if (it != mat2sub.end()) return it->second;
        CPUSub s; s.mat = matID;
        subs.emplace_back(std::move(s));
        size_t idx = subs.size() - 1;
        mat2sub[matID] = idx;
        return idx;
        };

    for (const auto& sh : shapes) {
        size_t off = 0;
        for (size_t f = 0; f < sh.mesh.num_face_vertices.size(); ++f) {
            const int fv = sh.mesh.num_face_vertices[f]; // 3 (triangulate=true)
            const int mat = sh.mesh.material_ids.empty() ? -1 : sh.mesh.material_ids[f];
            size_t sidx = getSubIndex(mat);
            auto& S = subs[sidx];

            for (int k = 0; k < fv; ++k) {
                const auto ix = sh.mesh.indices[off + k];
                CPUSub::Key key{ ix.vertex_index, ix.texcoord_index, ix.normal_index };

                auto it = S.dedup.find(key);
                uint32_t dstIndex;
                if (it != S.dedup.end()) {
                    dstIndex = it->second;
                }
                else {
                    Vtx v{};
                    // pos
                    if (ix.vertex_index >= 0) {
                        v.pos.x = attrib.vertices[3 * ix.vertex_index + 0];
                        v.pos.y = attrib.vertices[3 * ix.vertex_index + 1];
                        v.pos.z = attrib.vertices[3 * ix.vertex_index + 2];
                    }
                    // nrm
                    if (ix.normal_index >= 0) {
                        v.nrm.x = attrib.normals[3 * ix.normal_index + 0];
                        v.nrm.y = attrib.normals[3 * ix.normal_index + 1];
                        v.nrm.z = attrib.normals[3 * ix.normal_index + 2];
                    }
                    else {
                        v.nrm = { 0,1,0 };
                    }
                    // uv
                    if (ix.texcoord_index >= 0) {
                        v.uv.x = attrib.texcoords[2 * ix.texcoord_index + 0];
                        v.uv.y = 1.0f - attrib.texcoords[2 * ix.texcoord_index + 1]; // V flip
                    }
                    else {
                        v.uv = { 0,0 };
                    }

                    dstIndex = (uint32_t)S.verts.size();
                    S.verts.push_back(v);
                    S.dedup.emplace(key, dstIndex);
                }
                S.indices.push_back(dstIndex);
            }
            off += fv;
        }
    }

    if (subs.empty()) return false;

    // 4) 디스크립터 힙: "사용된 머티리얼 개수 × 2"
    ID3D12Device* dev = DX_CONTEXT.GetDevice();
    const size_t usedMatCount = mat2sub.size(); // (-1 포함 가능)
    D3D12_DESCRIPTOR_HEAP_DESC h{};
    h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    h.NumDescriptors = (UINT)(usedMatCount * 2);         // t0:albedo, t1:shadow
    h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&h, IID_PPV_ARGS(&mHeap)))) return false;

    mDescInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto baseCPU = mHeap->GetCPUDescriptorHandleForHeapStart();
    auto baseGPU = mHeap->GetGPUDescriptorHandleForHeapStart();

    // 섀도우맵 SRV 템플릿
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSRV{};
    shadowSRV.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shadowSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSRV.Texture2D.MipLevels = 1;
    ID3D12Resource* shadowTex = DX_MANAGER.GetShadowMap(); // DirectXManager에 getter 있어야 함

    // 5) 머티리얼별 디스크립터 슬롯 할당(한 번만)
    struct MatSlots { UINT t0t1Base; std::shared_ptr<Image> keep; }; // keep: 텍스처 생존 보장
    std::unordered_map<int, MatSlots> matSlots; matSlots.reserve(usedMatCount);

    UINT nextPair = 0;
    auto allocSlotsForMat = [&](SponzaSubmesh& sm, int matID)->MatSlots {
        auto it = matSlots.find(matID);
        if (it != matSlots.end()) return it->second;

        MatSlots s{ nextPair * 2, nullptr };
        ++nextPair;

        std::string albedoPath;
        if (matID >= 0 && matID < (int)matAlbedo.size() && !matAlbedo[matID].empty())
            albedoPath = matAlbedo[matID];
        else
            albedoPath = "./Resources/White1x1.png";

        sm.ro->Init(albedoPath, 0, pso, rs);

        // 1) 로드
        auto albedoImg = sm.ro->GetImage();

        // 2) GPU 업로드 보장 (중요!)
        if (albedoImg && !albedoImg->GetTexture()) {
            albedoImg->UploadTextureBuffer();
        }

        ID3D12Resource* albedoRes = (albedoImg ? albedoImg->GetTexture() : nullptr);
        if (!albedoRes) {
            OutputDebugStringA(("[Sponza] Missing or not uploaded texture: " + albedoPath + "\n").c_str());
            // 최후 폴백: 1x1 white라도 제대로 올려서 SRV 만들기
            auto whiteImg = DX_IMAGE.GetImage("./Resources/White1x1.png");
            if (whiteImg && !whiteImg->GetTexture()) whiteImg->UploadTextureBuffer();
            if (whiteImg && whiteImg->GetTexture()) { albedoImg = whiteImg; albedoRes = whiteImg->GetTexture(); }
        }

        if (albedoRes) {
            s.keep = albedoImg; // lifetime 유지
            D3D12_SHADER_RESOURCE_VIEW_DESC albedoSRV{};
            auto td = albedoRes->GetDesc();
            albedoSRV.Format = td.Format;
            albedoSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            albedoSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            albedoSRV.Texture2D.MipLevels = td.MipLevels ? td.MipLevels : 1;

            D3D12_CPU_DESCRIPTOR_HANDLE dstAlbedo = baseCPU; dstAlbedo.ptr += SIZE_T(s.t0t1Base + 0) * mDescInc;
            D3D12_CPU_DESCRIPTOR_HANDLE dstShadow = baseCPU; dstShadow.ptr += SIZE_T(s.t0t1Base + 1) * mDescInc;

            dev->CreateShaderResourceView(albedoRes, &albedoSRV, dstAlbedo);
            dev->CreateShaderResourceView(shadowTex, &shadowSRV, dstShadow);
        }
        else
        {
            return s;
        }
        // albedoRes가 결국에도 없다면 이 머티리얼은 샘플=0(검정)일 수밖에. 로그로 추적.

        matSlots.emplace(matID, s);
        return s;
        };

    // 6) GPU 업로드 + 서브메시 생성
    mSubs.clear(); mSubs.reserve(subs.size());
    for (auto& C : subs) {
        SponzaSubmesh sm;
        sm.ro = std::make_unique<RenderingObject3D>();
        // 내부에서 실제로 쓰는 SRV는 우리가 바인딩하니, Init의 텍스처 인자는 폴백만 넣어도 OK
        //if (!sm.ro->Init("./Resources/White1x1.png", /*dummy*/ 0, pso, rs)) 
        //    return false;

        // 인덱스 형식 자동 선택
        if (C.verts.size() < 65536) {
            std::vector<uint16_t> i16(C.indices.begin(), C.indices.end());
            if (!sm.ro->InitGeometry(C.verts.data(),
                (UINT)(C.verts.size() * sizeof(Vtx)),
                i16.data(),
                (UINT)(i16.size() * sizeof(uint16_t)),
                (UINT)i16.size(),
                DXGI_FORMAT_R16_UINT)) return false;
        }
        else {
            if (!sm.ro->InitGeometry(C.verts.data(),
                (UINT)(C.verts.size() * sizeof(Vtx)),
                C.indices.data(),
                (UINT)(C.indices.size() * sizeof(uint32_t)),
                (UINT)C.indices.size(),
                DXGI_FORMAT_R32_UINT)) return false;
        }

        // 머티리얼 슬롯 연결(그리고 텍스처 keepAlive)
        MatSlots sl = allocSlotsForMat(sm, C.mat);
        sm.keepAlive = sl.keep;
        sm.tableBase = baseGPU;
        int asd = SIZE_T(sl.t0t1Base);

        sm.tableBase.ptr += SIZE_T(sl.t0t1Base) * mDescInc;

        mSubs.emplace_back(std::move(sm));
    }

    return true;
}

void SponzaModel::Render(ID3D12GraphicsCommandList7* cmd,
    const Camera& cam, float aspect,
    D3D12_CPU_DESCRIPTOR_HANDLE& rtv,
    D3D12_CPU_DESCRIPTOR_HANDLE& dsv,
    float angle)
{
    ID3D12DescriptorHeap* heaps[] = { mHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);   // 꼭 여기서 바인딩

    for (auto& sm : mSubs) {
        DX_MANAGER.mObjSrvGPU = sm.tableBase; 
        sm.ro->Rendering(cmd, cam, aspect, rtv, dsv, angle);
    }
}

void SponzaModel::UploadGPUResource(ID3D12GraphicsCommandList7* cmdList)
{
    // 1) 스폰자 각 서브메시의 keepAlive(Image)를 대상으로
    for (auto& sm : mSubs) {
        if (!sm.keepAlive) continue;
        sm.ro->UploadGPUResource(cmdList);
    }
}