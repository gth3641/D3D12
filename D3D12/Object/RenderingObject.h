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
public: // Functions
	bool Init(const std::filesystem::path& imagePath);
	void UploadGPUResource(ID3D12GraphicsCommandList7* cmdList);

	const std::vector<Triangle>& GetTriangleVector() const { return m_Triangle; }
	Triangle* GetTriagleByIndex(size_t index);
	size_t GetTriangleIndex() const { return m_Triangle.size(); }

	std::shared_ptr<Image> GetImage() { return m_Image; }

	ComPointer<ID3D12Resource2>& GetUploadBuffer() { return m_UploadBuffer; }
	ComPointer<ID3D12Resource2>& GetVertexBuffer() { return m_VertexBuffer; }
	ComPointer<ID3D12DescriptorHeap>& GetSrvheap() { return m_Srvheap; }

	int GetVertexCount();
	void AddTriangle(const Vertex* vertex, size_t size);

private: // Functions
	void AddTexture(const std::filesystem::path& imagePath);
	void UploadTextureBuffer();
	void CreateDescriptorHipForTexture();
	void CreateSRV();
	void UploadCPUResource();

private: // Variables
	std::vector<Triangle> m_Triangle;

	std::shared_ptr<Image> m_Image;
	
	ComPointer<ID3D12Resource2> m_UploadBuffer;
	ComPointer<ID3D12Resource2> m_VertexBuffer;
	ComPointer<ID3D12DescriptorHeap> m_Srvheap;

};

