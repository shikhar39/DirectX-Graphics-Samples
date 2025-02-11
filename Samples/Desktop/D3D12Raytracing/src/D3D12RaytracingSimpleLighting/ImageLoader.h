# pragma once

// #include "WinInclude.h"
// #include "ComPtr.h"

#include "stdafx.h"
#include <vector>
#include <filesystem>
#include <algorithm>

#define __ImageLoader_CAR(expr) do {if(FAILED(expr)) { return false; } } while(false)

class ImageLoader {
public:
	struct ImageData {
		std::vector<char> data;
		uint32_t width;
		uint32_t height;
		uint32_t bpp;
		uint32_t cc;

		GUID wicPixelFormat;
		DXGI_FORMAT giPixelFormat;
	};

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


};