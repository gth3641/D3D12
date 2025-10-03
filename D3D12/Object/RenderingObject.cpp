#include "RenderingObject.h"
#include "Manager/DirectXManager.h"
#include "Manager/ImageManager.h"

RenderingObject::RenderingObject()
{

}

RenderingObject::~RenderingObject()
{
	m_UploadBuffer.Release();
	m_VertexBuffer.Release();
}

bool RenderingObject::Init(const std::filesystem::path& imagePath, UINT64 index)
{
	m_TestIndex = index;
	AddTexture(imagePath);
	UploadTextureBuffer();
	CreateSRV();
	UploadCPUResource();

	return true;
}

void RenderingObject::UploadGPUResource(ID3D12GraphicsCommandList7* cmdList)
{
	if (cmdList == nullptr)
	{
		return;
	}

	if (m_Image == nullptr)
	{
		return;
	}

	cmdList->CopyBufferRegion(m_VertexBuffer, 0, m_UploadBuffer, m_Image->GetTextureSize(), 1024);

	D3D12_BOX textureSizeAsBox = DirectXManager::GetTextureSizeAsBox(m_Image->GetTextureData());
	D3D12_TEXTURE_COPY_LOCATION txtcSrc = DirectXManager::GetTextureSource(m_UploadBuffer, m_Image->GetTextureData(), m_Image->GetTextureStride());
	D3D12_TEXTURE_COPY_LOCATION txtcDst = DirectXManager::GetTextureDestination(m_Image->GetTexture());

	cmdList->CopyTextureRegion(&txtcDst, 0, 0, 0, &txtcSrc, &textureSizeAsBox);
}

Triangle* RenderingObject::GetTriagleByIndex(size_t index)
{
	if (m_Triangle.size() > index)
	{
		return &m_Triangle[index];
	}

	return nullptr;
}

int RenderingObject::GetVertexCount()
{
	size_t size = m_Triangle.size();
	if (size <= 0)
	{
		return 0;
	}

	return size * _countof(m_Triangle[0].m_Verticies);
}

void RenderingObject::AddTriangle(const Vertex* vertex, size_t size)
{
	Triangle triangle;
	for (size_t i = 0; i < size; ++i)
	{
		triangle.m_Verticies[i] = vertex[i];
	}

	m_Triangle.push_back(triangle);
}

void RenderingObject::AddTexture(const std::filesystem::path& imagePath)
{
	m_Image = DX_IMAGE.GetImage(imagePath);
}

void RenderingObject::UploadTextureBuffer()
{
	if (m_Image == nullptr)
	{
		return;
	}

	D3D12_HEAP_PROPERTIES hpUpload = DirectXManager::GetHeapUploadProperties();
	D3D12_HEAP_PROPERTIES hpDefault = DirectXManager::GetDefaultUploadProperties();

	D3D12_RESOURCE_DESC rdu = DirectXManager::GetUploadResourceDesc(m_Image->GetTextureSize());
	D3D12_RESOURCE_DESC rdv = DirectXManager::GetVertexResourceDesc();

	DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpUpload, D3D12_HEAP_FLAG_NONE, &rdu, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_UploadBuffer));
	DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &rdv, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_VertexBuffer));
	m_Image->UploadTextureBuffer();
}

void RenderingObject::CreateSRV()
{
	if (m_Image == nullptr)
	{
		return;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = m_Image->GetTextureData().giPixelFormat;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.PlaneSlice = 0;
	srv.Texture2D.ResourceMinLODClamp = 0.f;

	DX_CONTEXT.GetDevice()->CreateShaderResourceView(m_Image->GetTexture(), &srv, DX_IMAGE.GetCPUDescriptorHandle(m_TestIndex));

}

void RenderingObject::UploadCPUResource()
{
	if (m_Image == nullptr)
	{
		return;
	}

	char* uploadBufferAddress;
	D3D12_RANGE uploadRange;
	uploadRange.Begin = 0;
	uploadRange.End = 1024 + m_Image->GetTextureSize();
	m_UploadBuffer->Map(0, &uploadRange, (void**)&uploadBufferAddress);
	if (uploadBufferAddress == nullptr)
	{
		return; //수정 필요.
	}
	memcpy(&uploadBufferAddress[0], m_Image->GetTextureData().data.data(), m_Image->GetTextureSize());

	size_t size = GetTriangleIndex();
	size_t curSize = 0;
	for (size_t i = 0; i < size; ++i)
	{
		Triangle* triangle = GetTriagleByIndex(i);
		memcpy(&uploadBufferAddress[m_Image->GetTextureSize() + curSize], triangle->m_Verticies, triangle->GetVerticiesSize());
		curSize += triangle->GetVerticiesSize();
	}

	m_UploadBuffer->Unmap(0, &uploadRange);
}
