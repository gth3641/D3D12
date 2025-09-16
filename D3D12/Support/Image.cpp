#include "Image.h"
#include "Manager/DirectXManager.h"
#include "D3D/DXContext.h"

Image::~Image()
{
	m_Texture.Release();
}

void Image::ImageLoad(const std::filesystem::path& imagePath)
{
	ImageLoader::LoadImageFromDisk("./Resources/TEX_Noise.png", m_TextureData);
	m_TextureStride = m_TextureData.width * ((m_TextureData.bpp + 7) / 8);
	m_TextureSize = (m_TextureData.height * m_TextureStride);
}

void Image::UploadTextureBuffer()
{
	D3D12_HEAP_PROPERTIES hpDefault = DirectXManager::GetDefaultUploadProperties();
	D3D12_RESOURCE_DESC rdt = DirectXManager::GetTextureResourceDesc(GetTextureData());

	DX_CONTEXT.GetDevice()->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &rdt, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_Texture));

}

D3D12_BOX Image::GetTextureSizeAsBox()
{
	D3D12_BOX textureSizeAsBox;
	textureSizeAsBox.left = textureSizeAsBox.top = textureSizeAsBox.front = 0;
	textureSizeAsBox.right = m_TextureData.width;
	textureSizeAsBox.bottom = m_TextureData.height;
	textureSizeAsBox.back = 1;

	return textureSizeAsBox;
}
