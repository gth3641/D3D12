#include "ImageLoader.h"

#include <algorithm>

const std::vector<ImageLoader::GUID_to_DXGI> ImageLoader::s_lookupTable =
{
    {GUID_WICPixelFormat32bppBGRA, DXGI_FORMAT_B8G8R8A8_UNORM},
    {GUID_WICPixelFormat32bppBGRA, DXGI_FORMAT_B8G8R8A8_UNORM}
};


bool ImageLoader::LoadImageFromDisk(const std::filesystem::path& imagePath, ImageData& data)
{
    // Factory
    ComPointer<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));

    // Load the image
    ComPointer<IWICStream> wicFileStream;

    hr = wicFactory->CreateStream(&wicFileStream);
    if (FAILED(hr)) { return false; }
    hr = wicFileStream->InitializeFromFilename(imagePath.wstring().c_str(), GENERIC_READ);
    if (FAILED(hr)) { return false; }

    ComPointer<IWICBitmapDecoder> wicDecoder;
    hr = wicFactory->CreateDecoderFromStream(wicFileStream, nullptr, WICDecodeMetadataCacheOnDemand, &wicDecoder);
    if (FAILED(hr)) { return false; }

    ComPointer<IWICBitmapFrameDecode> wicFrameDecoder;
    hr = wicDecoder->GetFrame(0, &wicFrameDecoder);
    if (FAILED(hr)) { return false; }

    // Trivial metadata
    hr = wicFrameDecoder->GetSize(&data.width, &data.height);
    if (FAILED(hr)) { return false; }
    hr = wicFrameDecoder->GetPixelFormat(&data.wicPixelFormat);
    if (FAILED(hr)) { return false; }

    ComPointer<IWICComponentInfo> wicComponentInfo;
    hr = wicFactory->CreateComponentInfo(data.wicPixelFormat, &wicComponentInfo);
    if (FAILED(hr)) { return false; }

    ComPointer<IWICPixelFormatInfo> wicPixelFormatInfo;
    hr = wicComponentInfo->QueryInterface(&wicPixelFormatInfo);
    if (FAILED(hr)) { return false; }
    hr = wicPixelFormatInfo->GetBitsPerPixel(&data.bpp);
    if (FAILED(hr)) { return false; }
    hr = wicPixelFormatInfo->GetChannelCount(&data.cc);
    if (FAILED(hr)) { return false; }

    // DXGI Pixel format
    auto findIt = std::find_if(s_lookupTable.begin(), s_lookupTable.end(),
        [&](const GUID_to_DXGI& entry)
        {
            return memcmp(&entry.wic, &data.wicPixelFormat, sizeof(GUID)) == 0;
        });

    if (findIt == s_lookupTable.end())
    {
        return false;
    }
    data.giPixelFormat = findIt->gi;

    // Image Loading
    uint32_t stride = ((data.bpp + 7) / 8) * data.width;
    uint32_t size = stride * data.height;
    data.data.resize(size);

    WICRect copyRect;
    copyRect.X = copyRect.Y = 0;
    copyRect.Width = data.width;
    copyRect.Height = data.height;

    hr = wicFrameDecoder->CopyPixels(&copyRect, stride, size, (BYTE*)data.data.data());
    if (FAILED(hr)) { return false; }

    return true;
}
