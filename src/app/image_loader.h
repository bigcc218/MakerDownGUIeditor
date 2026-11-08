// MDGE Studio - WIC image decoding for the Direct2D canvas
#pragma once

#include "ui_util.h"

#include <wincodec.h>

#include <string>

namespace mdgeui {

class ImageLoader {
  public:
    ImageLoader() = default;
    ~ImageLoader() = default;

    bool Initialize(std::wstring& error);
    bool LoadBitmap(ID2D1RenderTarget* target, const std::wstring& path,
                    ComPtr<ID2D1Bitmap>& bitmap, std::wstring& error) const;

    // Converts a PNG/JPEG/BMP/GIF/ICO file to an HICON. The caller owns the
    // returned icon and must call DestroyIcon.
    HICON CreateHIcon(const std::wstring& path, int size) const;

  private:
    ComPtr<IWICImagingFactory> wic_;
};

}  // namespace mdgeui
