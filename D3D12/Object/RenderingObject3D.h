#pragma once
#include "RenderingObject.h"

struct Camera;

class RenderingObject3D : public RenderingObject
{
public:
	RenderingObject3D();
	~RenderingObject3D();

public: // Static & Override
	virtual bool Init(
		const std::filesystem::path& imagePath, 
		UINT64 index, 
		ID3D12PipelineState* pso, 
		ID3D12RootSignature* rs
		);

public: // Functions
	void Rendering(
		ID3D12GraphicsCommandList7* cmd,
		const Camera& cam, 
		float aspect,
		D3D12_CPU_DESCRIPTOR_HANDLE& rtv, 
		D3D12_CPU_DESCRIPTOR_HANDLE& dsv,
		float angle
	);
	void RenderingDepthOnly(
		ID3D12GraphicsCommandList7* cmd
	);

	bool InitGeometry(
		Vtx* vertexBuffer,
		UINT vbSize,
		uint16_t* indexBuffer,
		UINT ibSize,
		UINT index
	);

	bool InitGeometry(Vtx* vertices, UINT vbSize,
		const void* indices, UINT ibSize,
		UINT indexCount, DXGI_FORMAT idxFmt);

protected:
	ComPointer<ID3D12Resource2>  m_IndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW     m_VertexBufferView{};
	D3D12_INDEX_BUFFER_VIEW      m_IndexBufferView{};
	UINT                         m_IndexCount = 0;

	ComPointer<ID3D12PipelineState>     m_PSO;
	ComPointer<ID3D12RootSignature>     m_RootSig;

};

