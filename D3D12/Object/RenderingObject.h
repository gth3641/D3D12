#pragma once
#include "Object.h"
#include "Util/Util.h"
#include "Support/Image.h"

#include <vector>
#include <memory>

class RenderingObject : public Object
{
public:
	RenderingObject();
	~RenderingObject();
public: // Static & Override
	virtual bool Init(const std::filesystem::path& imagePath, UINT64 index);

public: // Functions
	void UploadGPUResource(ID3D12GraphicsCommandList7* cmdList);

	const std::vector<Triangle>& GetTriangleVector() const { return m_Triangle; }
	Triangle* GetTriagleByIndex(size_t index);
	size_t GetTriangleIndex() const { return m_Triangle.size(); }
	UINT64 GetTestIndex() const { return m_TestIndex; }

	std::shared_ptr<Image> GetImage() { return m_Image; }

	ComPointer<ID3D12Resource2>& GetUploadBuffer() { return m_UploadBuffer; }
	ComPointer<ID3D12Resource2>& GetVertexBuffer() { return m_VertexBuffer; }

	int GetVertexCount();
	void AddTriangle(const Vertex* vertex, size_t size);
	std::vector<Triangle>& GetTriangle() { return m_Triangle; }
	void UploadCPUResource(bool textureUpdate = true, bool vertexUpdate = true);


	void SetVbDirty(bool dirty) { mVbDirty = dirty; }
	void SetTexDirty(bool dirty) { mTexDirty = dirty; }

protected: // Functions
	void AddTexture(const std::filesystem::path& imagePath);
	void UploadTextureBuffer();
	void CreateSRV();

	void UpateTexture(BYTE* dst);
	void UpdateVertexBuffer(BYTE* dst);

protected: // Variables
	std::vector<Triangle> m_Triangle;

	std::shared_ptr<Image> m_Image;
	
	ComPointer<ID3D12Resource2> m_UploadBuffer;
	ComPointer<ID3D12Resource2> m_VertexBuffer;

	INT64 m_TestIndex = 0;

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_TexFootprint{};
	UINT   m_TexNumRows = 0;
	UINT64 m_TexRowSizeInBytes = 0;  // = width * BPP (�е� ����)
	UINT64 m_TexUploadSize = 0;      // ��ü �ؽ�ó ���ε� ũ��(�е� ����)
	UINT64 m_GeomOffsetInUpload = 0; // ���ε� ���� ������ VB�� ���۵Ǵ� ������
	UINT64 m_VBSize = 0;             // ��ü ���ؽ� ������ ����Ʈ ��

	// ���� ����
	D3D12_RESOURCE_STATES mVBState = D3D12_RESOURCE_STATE_COPY_DEST;
	D3D12_RESOURCE_STATES mTexState = D3D12_RESOURCE_STATE_COPY_DEST;

	// ��Ƽ �÷���(���ε尡 �ʿ��� ���� true)
	bool mVbDirty = true;
	bool mTexDirty = true;

};

