#include "ui_util.h"

#include <algorithm>
#include <cmath>

namespace mdgeui {
namespace {

D2D1_POINT_2F P(float x, float y) {
    return D2D1::Point2F(x, y);
}

void StrokeLine(ID2D1RenderTarget* target, ID2D1Brush* brush, float thickness,
                float x1, float y1, float x2, float y2) {
    target->DrawLine(P(x1, y1), P(x2, y2), brush, thickness);
}

}  // namespace

ComPtr<IDWriteTextFormat> CreateTextFormat(IDWriteFactory* factory,
                                           const wchar_t* family, float size,
                                           DWRITE_FONT_WEIGHT weight,
                                           DWRITE_FONT_STYLE style,
                                           DWRITE_TEXT_ALIGNMENT alignment,
                                           DWRITE_WORD_WRAPPING wrapping) {
    ComPtr<IDWriteTextFormat> format;
    HRESULT result = factory->CreateTextFormat(family, nullptr, weight, style,
                                               DWRITE_FONT_STRETCH_NORMAL, size,
                                               L"zh-CN", format.GetAddressOf());
    if (FAILED(result)) {
        result = factory->CreateTextFormat(L"Segoe UI", nullptr, weight, style,
                                           DWRITE_FONT_STRETCH_NORMAL, size,
                                           L"zh-CN", format.ReleaseAndGetAddressOf());
    }
    if (SUCCEEDED(result)) {
        format->SetTextAlignment(alignment);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        format->SetWordWrapping(wrapping);
    }
    return format;
}

void DrawUiText(ID2D1RenderTarget* target, IDWriteFactory* factory,
                const std::wstring& text, IDWriteTextFormat* format,
                const RectF& bounds, const D2D1_COLOR_F& color,
                UiTextAlign align, UiTextVAlign valign, bool wrap) {
    if (!format || bounds.Empty()) {
        return;
    }
    ComPtr<IDWriteTextLayout> layout;
    const float width = std::max(1.0f, bounds.Width());
    const float height = std::max(1.0f, bounds.Height());
    if (FAILED(factory->CreateTextLayout(text.c_str(),
                                         static_cast<UINT32>(text.size()), format,
                                         width, height, layout.GetAddressOf()))) {
        return;
    }
    switch (align) {
        case UiTextAlign::Left:
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            break;
        case UiTextAlign::Center:
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            break;
        case UiTextAlign::Right:
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
    }
    switch (valign) {
        case UiTextVAlign::Top:
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            break;
        case UiTextVAlign::Middle:
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            break;
        case UiTextVAlign::Bottom:
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
            break;
    }
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                                 : DWRITE_WORD_WRAPPING_NO_WRAP);

    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (brush) {
        target->DrawTextLayout(P(bounds.left, bounds.top), layout.Get(), brush.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

float MeasureTextWidth(IDWriteFactory* factory, const std::wstring& text,
                       IDWriteTextFormat* format) {
    if (!factory || !format || text.empty()) {
        return 0.0f;
    }
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.c_str(),
                                         static_cast<UINT32>(text.size()), format,
                                         8192.0f, 8192.0f, layout.GetAddressOf()))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) {
        return 0.0f;
    }
    return metrics.width;
}

std::wstring EllipsizeMiddle(const std::wstring& text, std::size_t maxChars) {
    if (text.size() <= maxChars || maxChars < 8) {
        return text;
    }
    const std::size_t left = (maxChars - 3) / 2;
    const std::size_t right = maxChars - 3 - left;
    return text.substr(0, left) + L"..." + text.substr(text.size() - right);
}

void DrawIcon(ID2D1RenderTarget* target, IconKind kind, const RectF& bounds,
              ID2D1Brush* brush, float thickness) {
    const float x = bounds.left;
    const float y = bounds.top;
    const float w = bounds.Width();
    const float h = bounds.Height();
    const float s = std::min(w, h);
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float inset = s * 0.16f;

    switch (kind) {
        case IconKind::Save: {
            const RectF body = MakeRect(x + inset, y + inset, x + w - inset * 0.7f,
                                        y + h - inset);
            target->DrawRoundedRectangle(Rounded(body, 2.0f), brush, thickness);
            StrokeLine(target, brush, thickness, body.left + 2.0f, body.top + 2.0f,
                       body.right - 2.0f, body.top + 2.0f);
            target->DrawRectangle(
                D2D1::RectF(body.left + 3.0f, body.top + 2.0f, body.right - 3.0f,
                            body.top + h * 0.28f),
                brush, thickness);
            break;
        }
        case IconKind::Open: {
            const float top = y + h * 0.3f;
            const float bottom = y + h * 0.8f;
            target->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x + inset, top, x + w - inset, bottom),
                                  2.0f, 2.0f),
                brush, thickness);
            StrokeLine(target, brush, thickness, x + inset, top, x + w * 0.4f, top);
            StrokeLine(target, brush, thickness, x + w * 0.4f, top, x + w * 0.48f,
                       top - 3.0f);
            break;
        }
        case IconKind::Export: {
            const float middle = x + w * 0.5f;
            StrokeLine(target, brush, thickness, middle, y + inset, middle,
                       y + h * 0.65f);
            StrokeLine(target, brush, thickness, middle, y + inset,
                       middle - w * 0.18f, y + inset + h * 0.2f);
            StrokeLine(target, brush, thickness, middle, y + inset,
                       middle + w * 0.18f, y + inset + h * 0.2f);
            target->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(x + inset, y + h * 0.62f, x + w - inset, y + h - inset),
                    2.0f, 2.0f),
                brush, thickness);
            break;
        }
        case IconKind::Undo:
        case IconKind::Redo: {
            const bool redo = kind == IconKind::Redo;
            const float dir = redo ? 1.0f : -1.0f;
            const float centerX = cx - dir * w * 0.04f;
            const float top = y + h * 0.30f;
            const float bottom = y + h * 0.58f;
            StrokeLine(target, brush, thickness, centerX - dir * w * 0.22f, top,
                       centerX + dir * w * 0.12f, top);
            target->DrawLine(P(centerX + dir * w * 0.12f, top),
                             P(centerX + dir * w * 0.20f, bottom), brush, thickness);
            target->DrawLine(P(centerX + dir * w * 0.20f, bottom),
                             P(centerX - dir * w * 0.18f, bottom), brush, thickness);
            StrokeLine(target, brush, thickness, centerX - dir * w * 0.22f, top,
                       centerX - dir * w * 0.12f, top - h * 0.11f);
            StrokeLine(target, brush, thickness, centerX - dir * w * 0.22f, top,
                       centerX - dir * w * 0.12f, top + h * 0.11f);
            break;
        }
        case IconKind::Plus:
            StrokeLine(target, brush, thickness, cx, y + inset, cx, y + h - inset);
            StrokeLine(target, brush, thickness, x + inset, cy, x + w - inset, cy);
            break;
        case IconKind::ChevronDown:
            StrokeLine(target, brush, thickness, x + w * 0.25f, y + h * 0.4f,
                       cx, y + h * 0.62f);
            StrokeLine(target, brush, thickness, cx, y + h * 0.62f,
                       x + w * 0.75f, y + h * 0.4f);
            break;
        case IconKind::Close:
            StrokeLine(target, brush, thickness, x + inset, y + inset,
                       x + w - inset, y + h - inset);
            StrokeLine(target, brush, thickness, x + w - inset, y + inset,
                       x + inset, y + h - inset);
            break;
        case IconKind::Image: {
            const RectF frame = MakeRect(x + inset, y + inset, x + w - inset,
                                         y + h - inset);
            target->DrawRoundedRectangle(Rounded(frame, 2.0f), brush, thickness);
            target->FillEllipse(D2D1::Ellipse(P(frame.left + frame.Width() * 0.3f,
                                                frame.top + frame.Height() * 0.3f),
                                              s * 0.05f, s * 0.05f),
                                brush);
            StrokeLine(target, brush, thickness, frame.left + 1.0f,
                       frame.bottom - 2.0f, frame.left + frame.Width() * 0.42f,
                       frame.top + frame.Height() * 0.52f);
            StrokeLine(target, brush, thickness, frame.left + frame.Width() * 0.42f,
                       frame.top + frame.Height() * 0.52f, frame.right - 2.0f,
                       frame.bottom - 2.0f);
            break;
        }
        case IconKind::Divider:
            StrokeLine(target, brush, thickness, x + inset, cy, x + w - inset, cy);
            break;
        case IconKind::Link: {
            target->DrawEllipse(D2D1::Ellipse(P(cx - w * 0.16f, cy), w * 0.16f,
                                              h * 0.22f),
                                brush, thickness);
            target->DrawEllipse(D2D1::Ellipse(P(cx + w * 0.16f, cy), w * 0.16f,
                                              h * 0.22f),
                                brush, thickness);
            StrokeLine(target, brush, thickness, cx - w * 0.04f, cy,
                       cx + w * 0.04f, cy);
            break;
        }
        case IconKind::Code:
            StrokeLine(target, brush, thickness, x + w * 0.36f, y + h * 0.26f,
                       x + w * 0.18f, cy);
            StrokeLine(target, brush, thickness, x + w * 0.18f, cy,
                       x + w * 0.36f, y + h * 0.74f);
            StrokeLine(target, brush, thickness, x + w * 0.64f, y + h * 0.26f,
                       x + w * 0.82f, cy);
            StrokeLine(target, brush, thickness, x + w * 0.82f, cy,
                       x + w * 0.64f, y + h * 0.74f);
            break;
        case IconKind::Drag: {
            const float lineWidth = std::max(1.3f, thickness);
            const float half = s * 0.21f;
            for (int row = 0; row < 3; ++row) {
                const float rowY = cy + (static_cast<float>(row) - 1.0f) * s * 0.22f;
                target->DrawLine(P(cx - half, rowY), P(cx + half, rowY), brush,
                                 lineWidth);
            }
            break;
        }
        case IconKind::Markdown: {
            const RectF frame = MakeRect(x + inset, y + inset, x + w - inset,
                                         y + h - inset);
            target->DrawRoundedRectangle(Rounded(frame, 2.0f), brush, thickness);
            StrokeLine(target, brush, thickness, frame.left + 3.0f,
                       frame.bottom - 4.0f, frame.left + 3.0f, frame.top + 5.0f);
            StrokeLine(target, brush, thickness, frame.left + 3.0f, frame.top + 5.0f,
                       frame.left + frame.Width() * 0.5f, frame.bottom - 7.0f);
            StrokeLine(target, brush, thickness, frame.left + frame.Width() * 0.5f,
                       frame.bottom - 7.0f, frame.right - 3.0f, frame.top + 5.0f);
            StrokeLine(target, brush, thickness, frame.right - 3.0f, frame.top + 5.0f,
                       frame.right - 3.0f, frame.bottom - 4.0f);
            break;
        }
        case IconKind::Outline:
            for (int i = 0; i < 3; ++i) {
                const float row = y + h * 0.26f + i * h * 0.22f;
                target->FillEllipse(D2D1::Ellipse(P(x + w * 0.23f, row), 1.4f, 1.4f),
                                    brush);
                StrokeLine(target, brush, thickness, x + w * 0.35f, row,
                           x + w * (i == 1 ? 0.7f : 0.82f), row);
            }
            break;
        case IconKind::ArrowRight:
            StrokeLine(target, brush, thickness, x + inset, cy, x + w - inset, cy);
            StrokeLine(target, brush, thickness, x + w - inset, cy,
                       x + w * 0.66f, cy - h * 0.18f);
            StrokeLine(target, brush, thickness, x + w - inset, cy,
                       x + w * 0.66f, cy + h * 0.18f);
            break;
        case IconKind::Check:
            StrokeLine(target, brush, thickness, x + w * 0.22f, cy,
                       x + w * 0.42f, y + h * 0.7f);
            StrokeLine(target, brush, thickness, x + w * 0.42f, y + h * 0.7f,
                       x + w * 0.8f, y + h * 0.28f);
            break;
    }
}

void DrawLine(ID2D1RenderTarget* target, float x1, float y1, float x2, float y2,
              ID2D1Brush* brush, float thickness, ID2D1StrokeStyle* strokeStyle) {
    if (strokeStyle) {
        target->DrawLine(P(x1, y1), P(x2, y2), brush, thickness, strokeStyle);
    } else {
        target->DrawLine(P(x1, y1), P(x2, y2), brush, thickness);
    }
}

}  // namespace mdgeui
