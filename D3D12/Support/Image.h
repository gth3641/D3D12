#pragma once

#include "WinInclude.h"
#include "ComPointer.h"
#include "ImageLoader.h"

class Image 
{
public:
    ~Image();
public:
    void ImageLoad(const std::filesystem::path& imagePath);

    void UploadTextureBuffer();

    D3D12_BOX GetTextureSizeAsBox();
    ImageData& GetTextureData() { return m_TextureData; }
    uint32_t GetTextureStride() { return m_TextureStride; }
    uint32_t GetTextureSize() { return m_TextureSize; }
    ComPointer<ID3D12Resource2>& GetTexture() { return m_Texture; }

private:
    ImageData m_TextureData;
    uint32_t m_TextureStride = 0;
    uint32_t m_TextureSize = 0;

    ComPointer<ID3D12Resource2> m_Texture;
};