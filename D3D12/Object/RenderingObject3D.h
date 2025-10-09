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
		Camera& cam,
		float mAspect,
		D3D12_CPU_DESCRIPTOR_HANDLE& mRtvScene,
		D3D12_CPU_DESCRIPTOR_HANDLE& mDSV,
		UINT indexCount,
		float angle
	);

	bool InitGeometry(
		Vtx* vertexBuffer,
		UINT vbSize,
		uint16_t* indexBuffer,
		UINT ibSize,
		UINT index
	);

protected:
	ComPointer<ID3D12Resource2>  m_IndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW     m_VertexBufferView{};
	D3D12_INDEX_BUFFER_VIEW      m_IndexBufferView{};
	UINT                         m_IndexCount = 0;

	ComPointer<ID3D12PipelineState>     m_PSO;
	ComPointer<ID3D12RootSignature>     m_RootSig;

};

