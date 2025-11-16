#include "RenderingObject3D.h"
#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"

extern DirectX::XMMATRIX MakeVP_Dir(const Camera& cam, float aspect);

RenderingObject3D::RenderingObject3D()
{

}

RenderingObject3D::~RenderingObject3D()
{
}

bool RenderingObject3D::Init(
	const std::filesystem::path& imagePath, 
	UINT64 index, 
	ID3D12PipelineState* pso, 
	ID3D12RootSignature* rs
)
{
	bool rtValue = RenderingObject::Init(imagePath, index);
	if (pso == nullptr || rs == nullptr)
	{
		return false;
	}

	m_PSO = pso;
	m_RootSig = rs;

    if (m_RootSig == nullptr)
    {
        return false;
    }

	return rtValue;
}

void RenderingObject3D::Rendering(
	ID3D12GraphicsCommandList7* cmd,
	const Camera& cam, float aspect,
	D3D12_CPU_DESCRIPTOR_HANDLE& rtv, D3D12_CPU_DESCRIPTOR_HANDLE& dsv,
	float angle)
{
	using namespace DirectX;

    if (m_RootSig.Get() == nullptr)
    {
        return;
    }

	cmd->SetGraphicsRootSignature(m_RootSig);
	cmd->SetPipelineState(m_PSO);
	cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	cmd->IASetIndexBuffer(&m_IndexBufferView);

	// World 행렬 (회전 등)
	XMMATRIX W = (angle == 0.f) ? XMMatrixIdentity() : XMMatrixRotationY(angle);

	// MVP, LightVP (transposed)
	XMFLOAT4X4 mvpT, lightVPT;
	DX_MANAGER.BuildMVPs(W, cam, aspect, DX_MANAGER.GetLightViewProj(), mvpT, lightVPT);

	// World 행렬도 transposed로 준비 (VS/PS에서 mul(vector, matrix) 사용)
	XMFLOAT4X4 worldT;
	XMStoreFloat4x4(&worldT, XMMatrixTranspose(W));

	DirectX::XMFLOAT3 ld = DX_MANAGER.GetLightDirWS();
	float b0[52] = { 0 };
	memcpy(&b0[0], &mvpT, sizeof(mvpT));     // 16
	memcpy(&b0[16], &lightVPT, sizeof(lightVPT)); // 16  (이제 '순수 lightVP')
	memcpy(&b0[32], &worldT, sizeof(worldT));   // 16
	b0[48] = ld.x; b0[49] = ld.y; b0[50] = ld.z; b0[51] = 0.0f;
	cmd->SetGraphicsRoot32BitConstants(0, 52, b0, 0);

	// PS: t0=albedo, t1=shadow (RenderOffscreen에서 힙/베이스 설정됨)
	cmd->SetGraphicsRootDescriptorTable(1, DX_MANAGER.GetObjSrvGPU());

	cmd->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);
}

void RenderingObject3D::RenderingDepthOnly(ID3D12GraphicsCommandList7* cmd)
{
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	cmd->IASetIndexBuffer(&m_IndexBufferView);
	cmd->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);
}


bool RenderingObject3D::InitGeometry(
	Vtx* vertexBuffer,
	UINT vbSize,
	uint16_t* indexBuffer,
	UINT ibSize,
	UINT index
)
{
	m_IndexCount = index;

	auto dev = DX_CONTEXT.GetDevice();
	CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT), hpUp(D3D12_HEAP_TYPE_UPLOAD);

	// VB
	ComPointer<ID3D12Resource> upVB;
	auto rdVB = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
	dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdVB,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_VertexBuffer));
	dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &rdVB,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upVB));

	// IB
	ComPointer<ID3D12Resource> upIB;
	auto rdIB = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
	dev->CreateCommittedResource(&hpDef, D3D12_HEAP_FLAG_NONE, &rdIB,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_IndexBuffer));
	dev->CreateCommittedResource(&hpUp, D3D12_HEAP_FLAG_NONE, &rdIB,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upIB));

	// 업로드 데이터 채우기
	void* p = nullptr;
	upVB->Map(0, nullptr, &p);  memcpy(p, vertexBuffer, vbSize);  upVB->Unmap(0, nullptr);
	upIB->Map(0, nullptr, &p);  memcpy(p, indexBuffer, ibSize);  upIB->Unmap(0, nullptr);

	// 복사 + 전이
	ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
	cmd->CopyResource(m_VertexBuffer, upVB);
	cmd->CopyResource(m_IndexBuffer, upIB);
	auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(m_VertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(m_IndexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
	cmd->ResourceBarrier(1, &b1);
	cmd->ResourceBarrier(1, &b2);
	DX_CONTEXT.ExecuteCommandList();

	// VBV/IBV
	m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
	m_VertexBufferView.StrideInBytes = sizeof(Vtx);
	m_VertexBufferView.SizeInBytes = vbSize;

	m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
	m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
	m_IndexBufferView.SizeInBytes = ibSize;

	return true;
}

bool RenderingObject3D::InitGeometry(
	Vtx* vertices, 
	UINT vbSize, 
	const void* indices, 
	UINT ibSize, 
	UINT indexCount, 
	DXGI_FORMAT idxFmt)
{
    // 1) 기본 유효성 검사
    if (!vertices || !indices) {
        OutputDebugStringA("[InitGeometry] vertices/indices is null\n");
        return false;
    }
    if (vbSize == 0 || ibSize == 0 || indexCount == 0) {
        char buf[256];
        sprintf_s(buf, "[InitGeometry] size zero (vbSize=%u, ibSize=%u, indexCount=%u)\n",
            vbSize, ibSize, indexCount);
        OutputDebugStringA(buf);
        return false;
    }
    if (idxFmt != DXGI_FORMAT_R16_UINT && idxFmt != DXGI_FORMAT_R32_UINT) {
        OutputDebugStringA("[InitGeometry] idxFmt must be R16_UINT or R32_UINT\n");
        return false;
    }

    ID3D12Device* dev = DX_CONTEXT.GetDevice();
    if (!dev) {
        OutputDebugStringA("[InitGeometry] device is null\n");
        return false;
    }

    m_IndexCount = indexCount;

    // 2) 리소스 생성
    CD3DX12_HEAP_PROPERTIES hpDef(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES hpUp(D3D12_HEAP_TYPE_UPLOAD);

    HRESULT hr = S_OK;

    // VB
    ComPointer<ID3D12Resource> upVB;
    auto rdVB = CD3DX12_RESOURCE_DESC::Buffer((UINT64)vbSize);

    hr = dev->CreateCommittedResource(
        &hpDef, D3D12_HEAP_FLAG_NONE, &rdVB,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_VertexBuffer));
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "[InitGeometry] CreateCommittedResource(VB default) failed: 0x%08X (vbSize=%u)\n", (unsigned)hr, vbSize);
        OutputDebugStringA(buf);
        return false;
    }

    hr = dev->CreateCommittedResource(
        &hpUp, D3D12_HEAP_FLAG_NONE, &rdVB,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upVB));
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "[InitGeometry] CreateCommittedResource(VB upload) failed: 0x%08X (vbSize=%u)\n", (unsigned)hr, vbSize);
        OutputDebugStringA(buf);
        return false;
    }

    // IB
    ComPointer<ID3D12Resource> upIB;
    auto rdIB = CD3DX12_RESOURCE_DESC::Buffer((UINT64)ibSize);

    hr = dev->CreateCommittedResource(
        &hpDef, D3D12_HEAP_FLAG_NONE, &rdIB,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_IndexBuffer));
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "[InitGeometry] CreateCommittedResource(IB default) failed: 0x%08X (ibSize=%u)\n", (unsigned)hr, ibSize);
        OutputDebugStringA(buf);
        return false;
    }

    hr = dev->CreateCommittedResource(
        &hpUp, D3D12_HEAP_FLAG_NONE, &rdIB,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upIB));
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "[InitGeometry] CreateCommittedResource(IB upload) failed: 0x%08X (ibSize=%u)\n", (unsigned)hr, ibSize);
        OutputDebugStringA(buf);
        return false;
    }

    // 3) 업로드(맵 결과 체크 필수)
    void* p = nullptr;

    hr = upVB->Map(0, nullptr, &p);
    if (FAILED(hr) || !p) {
        OutputDebugStringA("[InitGeometry] upVB->Map failed\n");
        return false;
    }
    memcpy(p, vertices, vbSize);
    upVB->Unmap(0, nullptr);

    hr = upIB->Map(0, nullptr, &p);
    if (FAILED(hr) || !p) {
        OutputDebugStringA("[InitGeometry] upIB->Map failed\n");
        return false;
    }
    memcpy(p, indices, ibSize);
    upIB->Unmap(0, nullptr);

    // 4) 복사 + 전이
    ID3D12GraphicsCommandList7* cmd = DX_CONTEXT.InitCommandList();
    cmd->CopyResource(m_VertexBuffer, upVB);
    cmd->CopyResource(m_IndexBuffer, upIB);

    auto b1 = CD3DX12_RESOURCE_BARRIER::Transition(
        m_VertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    auto b2 = CD3DX12_RESOURCE_BARRIER::Transition(
        m_IndexBuffer, D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    cmd->ResourceBarrier(1, &b1);
    cmd->ResourceBarrier(1, &b2);

    DX_CONTEXT.ExecuteCommandList();

    // 5) VBV/IBV
    m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
    m_VertexBufferView.StrideInBytes = sizeof(Vtx);
    m_VertexBufferView.SizeInBytes = vbSize;

    m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
    m_IndexBufferView.Format = idxFmt; // DXGI_FORMAT_R32_UINT or R16_UINT
    m_IndexBufferView.SizeInBytes = ibSize;

    return true;
}
