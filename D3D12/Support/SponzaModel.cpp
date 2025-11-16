#include "SponzaModel.h"

#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"
#include "Object/RenderingObject3D.h"
#include "Support/tiny_obj_loader.h"
#include <cmath> 
#include <unordered_map>
#include <algorithm> 

struct Vec3 { float x, y, z; };

struct Mat3 {
    float m[3][3];
    Vec3 apply(const Vec3& v) const {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
        };
    }
    static Mat3 rotateX(float rad) {
        float c = cosf(rad), s = sinf(rad);
        Mat3 M{};
        M.m[0][0] = 1;  M.m[0][1] = 0;  M.m[0][2] = 0;
        M.m[1][0] = 0;  M.m[1][1] = c;  M.m[1][2] = -s;
        M.m[2][0] = 0;  M.m[2][1] = s;  M.m[2][2] = c;
        return M;
    }
};

static inline Mat3 Identity3() {
    Mat3 M{};
    M.m[0][0] = 1; M.m[0][1] = 0; M.m[0][2] = 0;
    M.m[1][0] = 0; M.m[1][1] = 1; M.m[1][2] = 0;
    M.m[2][0] = 0; M.m[2][1] = 0; M.m[2][2] = 1;
    return M;
}

static inline Mat3 ZupToYup(bool invert = false) {
    constexpr float HALF_PI = 1.57079632679f; // +90°
    return Mat3::rotateX(invert ? -HALF_PI : +HALF_PI);
}

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
    m_Subs.clear();
    m_Heap.Release();
    m_DescInc = 0;
}


bool SponzaModel::InitFromOBJ(const std::string& objPath, const std::string& baseDir,
    ID3D12PipelineState* pso, ID3D12RootSignature* rs, const ObjImportOptions& opts /*= {}*/)
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

    // 1-1) Import 변환 준비
    const Mat3 R_up = opts.zUpToYUp ? ZupToYup(opts.invertUpRotation) : Identity3();

    auto applyImportXf = [&](Vec3& p, bool isNormal) {
        // 업축 회전
        p = R_up.apply(p);

        // RH -> LH 변환: Z 반전
        if (opts.toLeftHanded) {
            p.z = -p.z;
        }

        if (!isNormal && opts.uniformScale != 1.0f) {
            p.x *= opts.uniformScale; p.y *= opts.uniformScale; p.z *= opts.uniformScale;
        }
        };

    // 2) material -> albedo
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
                        Vec3 p{
                            attrib.vertices[3 * ix.vertex_index + 0],
                            attrib.vertices[3 * ix.vertex_index + 1],
                            attrib.vertices[3 * ix.vertex_index + 2]
                        };
                        applyImportXf(p, /*isNormal=*/false);
                        v.pos = { p.x, p.y, p.z };
                    }

                    // nrm
                    if (ix.normal_index >= 0) {
                        Vec3 n{
                            attrib.normals[3 * ix.normal_index + 0],
                            attrib.normals[3 * ix.normal_index + 1],
                            attrib.normals[3 * ix.normal_index + 2]
                        };
                        applyImportXf(n, /*isNormal=*/true);
                        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                        if (len > 1e-8f) { n.x /= len; n.y /= len; n.z /= len; }
                        v.nrm = { n.x, n.y, n.z };
                    }
                    else {
                        Vec3 n{ 0,1,0 };
                        applyImportXf(n, /*isNormal=*/true);
                        v.nrm = { n.x, n.y, n.z };
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

            // RH -> LH면 와인딩 반전
            if (opts.toLeftHanded && fv == 3) {
                auto& idx = S.indices;
                const size_t base = idx.size() - 3;
                std::swap(idx[base + 1], idx[base + 2]);
            }

            off += fv;
        }
    }

    if (subs.empty()) return false;

    // 4) 디스크립터 힙
    ID3D12Device* dev = DX_CONTEXT.GetDevice();
    const size_t usedMatCount = mat2sub.size();
    D3D12_DESCRIPTOR_HEAP_DESC h{};
    h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    h.NumDescriptors = (UINT)(usedMatCount * 2);         // t0:albedo, t1:shadow
    h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&h, IID_PPV_ARGS(&m_Heap)))) return false;

    m_DescInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto baseCPU = m_Heap->GetCPUDescriptorHandleForHeapStart();
    auto baseGPU = m_Heap->GetGPUDescriptorHandleForHeapStart();

    // 섀도우맵 SRV 템플릿
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSRV{};
    shadowSRV.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shadowSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSRV.Texture2D.MipLevels = 1;
    ID3D12Resource* shadowTex = DX_MANAGER.GetShadowMap();

    // 5) 머티리얼별 디스크립터 슬롯 할당
    struct MatSlots { UINT t0t1Base; std::shared_ptr<Image> keep; };
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

        if (albedoImg && !albedoImg->GetTexture()) {
            albedoImg->UploadTextureBuffer();
        }

        ID3D12Resource* albedoRes = (albedoImg ? albedoImg->GetTexture() : nullptr);
        if (!albedoRes) {
            OutputDebugStringA(("[Sponza] Missing or not uploaded texture: " + albedoPath + "\n").c_str());
            auto whiteImg = DX_IMAGE.GetImage("./Resources/White1x1.png");
            if (whiteImg && !whiteImg->GetTexture()) whiteImg->UploadTextureBuffer();
            if (whiteImg && whiteImg->GetTexture()) { albedoImg = whiteImg; albedoRes = whiteImg->GetTexture(); }
        }

        if (albedoRes) {
            s.keep = albedoImg;
            D3D12_SHADER_RESOURCE_VIEW_DESC albedoSRV{};
            auto td = albedoRes->GetDesc();
            albedoSRV.Format = td.Format;
            albedoSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            albedoSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            albedoSRV.Texture2D.MipLevels = td.MipLevels ? td.MipLevels : 1;

            D3D12_CPU_DESCRIPTOR_HANDLE dstAlbedo = baseCPU; dstAlbedo.ptr += SIZE_T(s.t0t1Base + 0) * m_DescInc;
            D3D12_CPU_DESCRIPTOR_HANDLE dstShadow = baseCPU; dstShadow.ptr += SIZE_T(s.t0t1Base + 1) * m_DescInc;

            dev->CreateShaderResourceView(albedoRes, &albedoSRV, dstAlbedo);
            dev->CreateShaderResourceView(shadowTex, &shadowSRV, dstShadow);
        }
        matSlots.emplace(matID, s);
        return s;
        };

    // 6) GPU 업로드 + 서브메시 생성
    m_Subs.clear(); m_Subs.reserve(subs.size());
    for (auto& C : subs) {
        SponzaSubmesh sm;
        sm.ro = std::make_unique<RenderingObject3D>();

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

        MatSlots sl = allocSlotsForMat(sm, C.mat);
        sm.keepAlive = sl.keep;
        sm.tableBase = baseGPU;
        sm.tableBase.ptr += SIZE_T(sl.t0t1Base) * m_DescInc;

        m_Subs.emplace_back(std::move(sm));
    }

    ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
    DX_CONTEXT.ExecuteCommandList();
    return true;
}


void SponzaModel::Render(ID3D12GraphicsCommandList7* cmd,
    const Camera& cam, float aspect,
    D3D12_CPU_DESCRIPTOR_HANDLE& rtv,
    D3D12_CPU_DESCRIPTOR_HANDLE& dsv,
    float angle)
{
    ID3D12DescriptorHeap* heaps[] = { m_Heap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);  

    for (auto& sm : m_Subs) {
        DX_MANAGER.SetObjSrvGPU(sm.tableBase);
        sm.ro->Rendering(cmd, cam, aspect, rtv, dsv, angle);
    }
}

void SponzaModel::UploadGPUResource(ID3D12GraphicsCommandList7* cmdList)
{
    for (auto& sm : m_Subs) {
        if (!sm.keepAlive) continue;
        sm.ro->UploadGPUResource(cmdList);
    }
}