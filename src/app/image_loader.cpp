#include "image_loader.h"

#include "core/path_utils.h"

#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace mdgeui {
namespace {

std::wstring HResultText(HRESULT result) {
    wchar_t buffer[32]{};
    swprintf(buffer, 32, L"0x%08lX", static_cast<unsigned long>(result));
    return buffer;
}

bool IsIcoPath(const std::wstring& path) {
    const std::wstring extension = mdge::FileExtensionLower(path);
    return extension == L".ico";
}

HICON IconFromWicBitmap(IWICImagingFactory* factory, IWICBitmapSource* source,
                        int size) {
    if (!factory || !source || size <= 0) {
        return nullptr;
    }
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf()))) {
        return nullptr;
    }
    if (FAILED(scaler->Initialize(source, static_cast<UINT>(size),
                                  static_cast<UINT>(size),
                                  WICBitmapInterpolationModeFant))) {
        return nullptr;
    }

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* dibBits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP colorBitmap = CreateDIBSection(
        screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &dibBits,
        nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!colorBitmap || !dibBits) {
        if (colorBitmap) {
            DeleteObject(colorBitmap);
        }
        return nullptr;
    }

    const UINT stride = static_cast<UINT>(size) * 4;
    const UINT bufferSize = stride * static_cast<UINT>(size);
    HRESULT result = scaler->CopyPixels(nullptr, stride, bufferSize,
                                        static_cast<BYTE*>(dibBits));
    if (FAILED(result)) {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, nullptr);
    if (!maskBitmap) {
        DeleteObject(colorBitmap);
        return nullptr;
    }
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = colorBitmap;
    info.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);
    return icon;
}

}  // namespace

bool ImageLoader::Initialize(std::wstring& error) {
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(wic_.GetAddressOf()));
    if (FAILED(result)) {
        error = L"无法初始化 Windows Imaging Component（" + HResultText(result) + L"）。";
        return false;
    }
    return true;
}

bool ImageLoader::LoadBitmap(ID2D1RenderTarget* target, const std::wstring& path,
                             ComPtr<ID2D1Bitmap>& bitmap,
                             std::wstring& error) const {
    bitmap.Reset();
    if (!wic_ || !target || path.empty()) {
        error = L"图像路径为空。";
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT result = wic_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
        decoder.GetAddressOf());
    if (FAILED(result)) {
        error = L"无法读取图像文件。";
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(result)) {
        error = L"图像不包含可读取的画面。";
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    result = wic_->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(result)) {
        error = L"无法创建图像格式转换器。";
        return false;
    }
    result = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        error = L"无法转换图像像素格式。";
        return false;
    }

    result = target->CreateBitmapFromWicBitmap(converter.Get(),
                                               bitmap.GetAddressOf());
    if (FAILED(result)) {
        error = L"Direct2D 无法创建图像位图（" + HResultText(result) + L"）。";
        bitmap.Reset();
        return false;
    }
    return true;
}

HICON ImageLoader::CreateHIcon(const std::wstring& path, int size) const {
    if (path.empty() || size <= 0) {
        return nullptr;
    }
    if (IsIcoPath(path)) {
        HICON icon = reinterpret_cast<HICON>(LoadImageW(
            nullptr, path.c_str(), IMAGE_ICON, size, size,
            LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (icon) {
            return icon;
        }
    }
    if (!wic_) {
        return nullptr;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                               WICDecodeMetadataCacheOnDemand,
                                               decoder.GetAddressOf()))) {
        return nullptr;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
        return nullptr;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic_->CreateFormatConverter(converter.GetAddressOf()))) {
        return nullptr;
    }
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }
    return IconFromWicBitmap(wic_.Get(), converter.Get(), size);
}

}  // namespace mdgeui
