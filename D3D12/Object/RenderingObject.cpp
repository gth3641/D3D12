#include "RenderingObject.h"
#include "Manager/DirectXManager.h"

RenderingObject::RenderingObject()
{

}

RenderingObject::~RenderingObject()
{
	m_UploadBuffer.Release();
	m_VertexBuffer.Release();
	m_Srvheap.Release();
}

bool RenderingObject::Init(const std::filesystem::path& imagePath)
{
	AddTexture(imagePath);
	UploadTextureBuffer();
	CreateDescriptorHipForTexture();
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

	if (GetImage() == nullptr)
	{
		return;
	}

	cmdList->CopyBufferRegion(GetVertexBuffer(), 0, GetUploadBuffer(), GetImage()->GetTextureSize(), 1024);

	D3D12_BOX textureSizeAsBox = DirectXManager::GetTextureSizeAsBox(GetImage()->GetTextureData());
	D3D12_TEXTURE_COPY_LOCATION txtcSrc = DirectXManager::GetTextureSource(GetUploadBuffer(), GetImage()->GetTextureData(), GetImage()->GetTextureStride());
	D3D12_TEXTURE_COPY_LOCATION txtcDst = DirectXManager::GetTextureDestination(GetImage()->GetTexture());

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
	if (m_Image == nullptr)
	{
		m_Image = std::make_shared<Image>();
	}

	m_Image->ImageLoad(imagePath);
}

void RenderingObject::UploadTextureBuffer()
{
	if (GetImage() == nullptr)
	{
		return;
	}

	D3D12_HEAP_PROPERTIES hpUpload = DirectXManager::GetHeapUploadProperties();
	D3D12_HEAP_PROPERTIES hpDefault = DirectXManager::GetDefaultUploadProperties();

	D3D12_RESOURCE_DESC rdu = DirectXManager::GetUploadResourceDesc(GetImage()->GetTextureSize()); //< TODO: 이거 어느 상황에 필요한건지 체크
	D3D12_RESOURCE_DESC rdv = DirectXManager::GetVertexResourceDesc();

	DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpUpload, D3D12_HEAP_FLAG_NONE, &rdu, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&GetUploadBuffer()));
	DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &rdv, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&GetVertexBuffer()));
	GetImage()->UploadTextureBuffer();
}

void RenderingObject::CreateDescriptorHipForTexture()
{
	D3D12_DESCRIPTOR_HEAP_DESC dhd{};
	dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	dhd.NumDescriptors = 8;
	dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dhd.NodeMask = 0;

	DX_CONTEXT.GetDevice()->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&GetSrvheap()));
}

void RenderingObject::CreateSRV()
{
	if (GetImage() == nullptr)
	{
		return;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = GetImage()->GetTextureData().giPixelFormat;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;
	srv.Texture2D.PlaneSlice = 0;
	srv.Texture2D.ResourceMinLODClamp = 0.f;

	DX_CONTEXT.GetDevice()->CreateShaderResourceView(GetImage()->GetTexture(), &srv, GetSrvheap()->GetCPUDescriptorHandleForHeapStart());

}

void RenderingObject::UploadCPUResource()
{
	if (GetImage() == nullptr)
	{
		return;
	}

	char* uploadBufferAddress;
	D3D12_RANGE uploadRange;
	uploadRange.Begin = 0;
	uploadRange.End = 1024 + GetImage()->GetTextureSize();
	GetUploadBuffer()->Map(0, &uploadRange, (void**)&uploadBufferAddress);
	memcpy(&uploadBufferAddress[0], GetImage()->GetTextureData().data.data(), GetImage()->GetTextureSize());

	size_t size = GetTriangleIndex();
	size_t curSize = 0;
	for (size_t i = 0; i < size; ++i)
	{
		Triangle* triangle = GetTriagleByIndex(i);
		memcpy(&uploadBufferAddress[GetImage()->GetTextureSize() + curSize], triangle->m_Verticies, triangle->GetVerticiesSize());
		curSize += triangle->GetVerticiesSize();
	}

	GetUploadBuffer()->Unmap(0, &uploadRange);
}
