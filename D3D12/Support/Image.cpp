#include "Image.h"
#include "Manager/DirectXManager.h"
#include "D3D/DXContext.h"

Image::~Image()
{
	m_Texture.Release();
}

void Image::ImageLoad(const std::filesystem::path& imagePath)
{
	ImageLoader::LoadImageFromDisk(imagePath, m_TextureData);
	m_TextureStride = m_TextureData.width * ((m_TextureData.bpp + 7) / 8);
	m_TextureSize = (m_TextureData.height * m_TextureStride);
}

void Image::UploadTextureBuffer()
{
    auto CalcMipCount = [](UINT w, UINT h) {
        UINT m = 1;
        while (w > 1 || h > 1) {
            w = (w > 1) ? (w >> 1) : 1;
            h = (h > 1) ? (h >> 1) : 1;
            ++m;
        }
        return m;
        };

    // 포맷이 RGBA8/BGRA8일 때만 우리가 CPU로 mip-chain을 만들어주니,
    // 그 외 포맷은 안전하게 mip 1개만 생성하도록 분기(중요!)
    const bool isRGBA8 =
        (m_TextureData.giPixelFormat == DXGI_FORMAT_R8G8B8A8_UNORM) ||
        (m_TextureData.giPixelFormat == DXGI_FORMAT_B8G8R8A8_UNORM);

    const UINT mipCount = isRGBA8
        ? CalcMipCount((UINT)m_TextureData.width, (UINT)m_TextureData.height)
        : 1;

    D3D12_HEAP_PROPERTIES hpDefault = DirectXManager::GetDefaultUploadProperties();

    D3D12_RESOURCE_DESC rdt{};
    rdt.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rdt.Width = m_TextureData.width;
    rdt.Height = m_TextureData.height;
    rdt.DepthOrArraySize = 1;
    rdt.MipLevels = (UINT16)mipCount;               
    rdt.Format = m_TextureData.giPixelFormat;
    rdt.SampleDesc = { 1, 0 };
    rdt.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rdt.Flags = D3D12_RESOURCE_FLAG_NONE;

    DX_CONTEXT.GetDevice()->CreateCommittedResource(
        &hpDefault, D3D12_HEAP_FLAG_NONE, &rdt,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_Texture));
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
