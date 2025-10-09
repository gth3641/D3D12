#include "RenderingObject3D.h"
#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"

static inline DirectX::XMMATRIX MakeVP_Dir(const Camera& cam, float aspect)
{
	XMVECTOR eye = XMLoadFloat3(&cam.pos);
	XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&cam.dir));
	XMVECTOR up = XMVector3Normalize(XMLoadFloat3(&cam.up));

	XMMATRIX V = XMMatrixLookToLH(eye, dir, up);
	XMMATRIX P = XMMatrixPerspectiveFovLH(cam.fovY, aspect, cam.nearZ, cam.farZ);
	return XMMatrixMultiply(V, P);
}

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

	return rtValue;
}

void RenderingObject3D::Rendering(
	ID3D12GraphicsCommandList7* cmd, 
	Camera& cam, 
	float mAspect,
	D3D12_CPU_DESCRIPTOR_HANDLE& mRtvScene,
	D3D12_CPU_DESCRIPTOR_HANDLE& mDSV,
	UINT indexCount,
	float angle
)
{
	cmd->OMSetRenderTargets(1, &mRtvScene, FALSE, &mDSV);

	cmd->SetPipelineState(m_PSO);
	cmd->SetGraphicsRootSignature(m_RootSig);

	// 텍스처 힙
	ID3D12DescriptorHeap* srvHeap = DX_IMAGE.GetSrvheap();
	cmd->SetDescriptorHeaps(1, &srvHeap);
	cmd->SetGraphicsRootDescriptorTable(1, DX_IMAGE.GetGPUDescriptorHandle(m_TestIndex));

	// IA
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	cmd->IASetIndexBuffer(&m_IndexBufferView);

	// MVP (World=I)
	XMMATRIX Wg = angle == 0.f ? XMMatrixIdentity() : (XMMatrixRotationY(angle) * XMMatrixRotationX(angle));
	XMMATRIX VPg = MakeVP_Dir(cam, mAspect);
	XMMATRIX MVPg = XMMatrixTranspose(Wg * VPg);
	cmd->SetGraphicsRoot32BitConstants(0, 16, &MVPg, 0);

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