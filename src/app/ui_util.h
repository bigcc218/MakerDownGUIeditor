// MDGE Studio - tiny COM and Direct2D helpers
#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <cstdint>
#include <string>
#include <utility>

namespace mdgeui {

template <typename T>
class ComPtr {
  public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : pointer_(other.pointer_) {
        other.pointer_ = nullptr;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            pointer_ = other.pointer_;
            other.pointer_ = nullptr;
        }
        return *this;
    }

    ~ComPtr() { Reset(); }

    T* Get() const { return pointer_; }
    T* operator->() const { return pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }

    T** GetAddressOf() {
        Reset();
        return &pointer_;
    }
    T** ReleaseAndGetAddressOf() {
        Reset();
        return &pointer_;
    }
    T* const* GetAddressOf() const { return &pointer_; }

    void Attach(T* pointer) {
        if (pointer_ != pointer) {
            Reset();
            pointer_ = pointer;
        }
    }

    T* Detach() {
        T* pointer = pointer_;
        pointer_ = nullptr;
        return pointer;
    }

    void Reset() {
        if (pointer_) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

    HRESULT CopyTo(T** output) const {
        if (!pointer_) {
            *output = nullptr;
            return S_FALSE;
        }
        pointer_->AddRef();
        *output = pointer_;
        return S_OK;
    }

  private:
    T* pointer_ = nullptr;
};

struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
    float CenterX() const { return (left + right) * 0.5f; }
    float CenterY() const { return (top + bottom) * 0.5f; }
    bool Empty() const { return right <= left || bottom <= top; }
    bool Contains(float x, float y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
    RectF Inflated(float amount) const {
        return RectF{left - amount, top - amount, right + amount, bottom + amount};
    }
    RectF Inset(float amount) const {
        return RectF{left + amount, top + amount, right - amount, bottom - amount};
    }
    RectF Offset(float dx, float dy) const {
        return RectF{left + dx, top + dy, right + dx, bottom + dy};
    }
};

inline RectF MakeRect(float left, float top, float right, float bottom) {
    return RectF{left, top, right, bottom};
}

inline RectF MakeRectXYWH(float x, float y, float width, float height) {
    return RectF{x, y, x + width, y + height};
}

inline D2D1_RECT_F ToD2D(const RectF& rect) {
    return D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
}

inline D2D1_COLOR_F Color(std::uint32_t rgb, float alpha = 1.0f) {
    return D2D1::ColorF(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(rgb & 0xFF) / 255.0f, alpha);
}

namespace palette {
constexpr std::uint32_t kInk = 0x1D2621;
constexpr std::uint32_t kInkSoft = 0x2A352F;
constexpr std::uint32_t kInkMuted = 0x6D7A72;
constexpr std::uint32_t kSidebar = 0x18231E;
constexpr std::uint32_t kSidebarSoft = 0x22312A;
constexpr std::uint32_t kSidebarText = 0xE7EDE8;
constexpr std::uint32_t kSidebarMuted = 0x8FA096;
constexpr std::uint32_t kPaper = 0xF3EFE6;
constexpr std::uint32_t kPage = 0xFFFDF8;
constexpr std::uint32_t kCard = 0xFAF7F0;
constexpr std::uint32_t kLine = 0xE2DCCE;
constexpr std::uint32_t kLineStrong = 0xCFC6B5;
constexpr std::uint32_t kAccent = 0x1E7A61;
constexpr std::uint32_t kAccentHover = 0x186852;
constexpr std::uint32_t kAccentSoft = 0xE1EFE8;
constexpr std::uint32_t kAccentPale = 0xF0F7F3;
constexpr std::uint32_t kGold = 0xB9853C;
constexpr std::uint32_t kGoldSoft = 0xF6EEDF;
constexpr std::uint32_t kDanger = 0xB8544B;
constexpr std::uint32_t kText = 0x2C342F;
constexpr std::uint32_t kTextMuted = 0x7D857F;
constexpr std::uint32_t kSelection = 0xBFDED2;
constexpr std::uint32_t kCodeBg = 0x202B25;
constexpr std::uint32_t kCodeText = 0xD8E5DC;
}  // namespace palette

inline float ClampF(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

inline D2D1_ROUNDED_RECT Rounded(const RectF& rect, float radius) {
    return D2D1::RoundedRect(ToD2D(rect), radius, radius);
}

enum class UiTextAlign { Left, Center, Right };
enum class UiTextVAlign { Top, Middle, Bottom };

ComPtr<IDWriteTextFormat> CreateTextFormat(IDWriteFactory* factory,
                                           const wchar_t* family, float size,
                                           DWRITE_FONT_WEIGHT weight,
                                           DWRITE_FONT_STYLE style,
                                           DWRITE_TEXT_ALIGNMENT alignment,
                                           DWRITE_WORD_WRAPPING wrapping);

void DrawUiText(ID2D1RenderTarget* target, IDWriteFactory* factory,
                const std::wstring& text, IDWriteTextFormat* format,
                const RectF& bounds, const D2D1_COLOR_F& color,
                UiTextAlign align = UiTextAlign::Left,
                UiTextVAlign valign = UiTextVAlign::Middle,
                bool wrap = false);

float MeasureTextWidth(IDWriteFactory* factory, const std::wstring& text,
                       IDWriteTextFormat* format);

std::wstring EllipsizeMiddle(const std::wstring& text, std::size_t maxChars);

enum class IconKind {
    Save,
    Open,
    Export,
    Undo,
    Redo,
    Plus,
    ChevronDown,
    Close,
    Image,
    Divider,
    Link,
    Code,
    Drag,
    Markdown,
    Outline,
    ArrowRight,
    Check,
};

void DrawIcon(ID2D1RenderTarget* target, IconKind kind, const RectF& bounds,
              ID2D1Brush* brush, float thickness = 1.6f);

struct StrokeStyleDeleter {
    void operator()(ID2D1StrokeStyle* style) const {
        if (style) {
            style->Release();
        }
    }
};

void DrawLine(ID2D1RenderTarget* target, float x1, float y1, float x2, float y2,
              ID2D1Brush* brush, float thickness, ID2D1StrokeStyle* strokeStyle);

}  // namespace mdgeui
