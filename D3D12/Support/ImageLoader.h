#pragma once

#include "WinInclude.h"
#include "ComPointer.h"

#include <vector>
#include <filesystem>

#define __ImageLoader_CAR(expr) do { if(!(expr)) { return false; } } while(false)

struct ImageData
{
	std::vector<char> data;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t bpp = 0;
	uint32_t cc = 0;

	GUID			wicPixelFormat;
	DXGI_FORMAT		giPixelFormat;
};

class ImageLoader
{
public:
	static bool LoadImageFromDisk(const std::filesystem::path& imagePath, ImageData& data);

private:
	struct GUID_to_DXGI
	{
		GUID wic;
		DXGI_FORMAT gi;
	};

	static const std::vector<GUID_to_DXGI> s_lookupTable;

private:
	ImageLoader() = default;
	ImageLoader(const ImageLoader&) = default;
	ImageLoader& operator=(const ImageLoader&) = default;

};