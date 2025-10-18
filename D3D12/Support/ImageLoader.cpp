#include "ImageLoader.h"

#include <algorithm>

const std::vector<ImageLoader::GUID_to_DXGI> ImageLoader::s_lookupTable =
{
    // 32bpp BGRA (non-premultiplied / premultiplied)
    { GUID_WICPixelFormat32bppBGRA,  DXGI_FORMAT_B8G8R8A8_UNORM },
    { GUID_WICPixelFormat32bppPBGRA, DXGI_FORMAT_B8G8R8A8_UNORM }, // premultiplied도 동일 DXGI로

    // 32bpp RGBA (non-premultiplied / premultiplied)
    { GUID_WICPixelFormat32bppRGBA,  DXGI_FORMAT_R8G8B8A8_UNORM },
    { GUID_WICPixelFormat32bppPRGBA, DXGI_FORMAT_R8G8B8A8_UNORM },

    // 32bpp BGR/BGRX (알파 없음) → B8G8R8X8_UNORM
    { GUID_WICPixelFormat32bppBGR,   DXGI_FORMAT_B8G8R8X8_UNORM },
    { GUID_WICPixelFormat32bppBGR,   DXGI_FORMAT_B8G8R8X8_UNORM }, // 일부 SDK엔 BGRX 명시가 따로 없는 경우가 있어 동일 GUID 사용

    // 64bpp RGBA (16비트 채널)
    { GUID_WICPixelFormat64bppRGBA,  DXGI_FORMAT_R16G16B16A16_UNORM },
    { GUID_WICPixelFormat64bppPRGBA, DXGI_FORMAT_R16G16B16A16_UNORM },

    // 48bpp RGB (16비트 채널, 알파 없음)
    //{ GUID_WICPixelFormat48bppRGB,   DXGI_FORMAT_R16G16B16_UNORM },

    // 그레이스케일
    { GUID_WICPixelFormat8bppGray,   DXGI_FORMAT_R8_UNORM },
    { GUID_WICPixelFormat16bppGray,  DXGI_FORMAT_R16_UNORM },
};


bool ImageLoader::LoadImageFromDisk(const std::filesystem::path& imagePath, ImageData& data)
{
    // 1) WIC 팩토리
    ComPointer<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) return false;

    // 2) 파일 스트림 + 디코더 + 0번 프레임
    ComPointer<IWICStream> wicStream;
    if (FAILED(wicFactory->CreateStream(&wicStream))) return false;
    if (FAILED(wicStream->InitializeFromFilename(imagePath.wstring().c_str(), GENERIC_READ))) return false;

    ComPointer<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromStream(
        wicStream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) return false;

    ComPointer<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    // 3) 크기/원본 포맷
    if (FAILED(frame->GetSize(&data.width, &data.height))) return false;

    // 4) 어떤 포맷이든 32bpp BGRA로 변환 (가장 호환성 좋음)
    ComPointer<IWICFormatConverter> conv;
    if (FAILED(wicFactory->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(
        frame,
        GUID_WICPixelFormat32bppBGRA,       // 타겟 포맷
        WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;

    // 5) DXGI 포맷/메타 채우기
    data.wicPixelFormat = GUID_WICPixelFormat32bppBGRA;
    data.giPixelFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    data.bpp = 32;
    data.cc = 4;

    // 6) 복사 (stride = 4 * width)
    const UINT stride = 4u * data.width;
    const UINT size = stride * data.height;
    data.data.resize(size);

    WICRect rc{ 0, 0, (INT)data.width, (INT)data.height };
    if (FAILED(conv->CopyPixels(&rc, stride, size, (BYTE*)data.data.data()))) return false;

    return true;
}
