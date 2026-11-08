#include "app.h"

#include "core/markdown.h"
#include "core/path_utils.h"

#include <d2d1helper.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <vector>

namespace mdgeui {
namespace {

constexpr float kHeaderHeight = 72.0f;
constexpr float kSidebarWidth = 252.0f;

std::wstring TopLabel(ActionId action) {
    switch (action) {
        case ActionId::NewProject: return L"新建";
        case ActionId::OpenProject: return L"打开";
        case ActionId::Undo: return L"撤销";
        case ActionId::Redo: return L"重做";
        case ActionId::TogglePreview: return L"源码";
        case ActionId::ExportMarkdown: return L"导出 MD";
        case ActionId::SaveProject: return L"保存项目";
        default: return {};
    }
}

std::wstring FormatLabel(ActionId action) {
    switch (action) {
        case ActionId::StyleHeading1: return L"H1";
        case ActionId::StyleHeading2: return L"H2";
        case ActionId::StyleHeading3: return L"H3";
        case ActionId::StyleParagraph: return L"正文";
        case ActionId::InlineBold: return L"B";
        case ActionId::InlineItalic: return L"I";
        case ActionId::InlineCode: return L"代码";
        case ActionId::InlineLink: return L"链接";
        case ActionId::DeleteBlock: return L"删除块";
        default: return {};
    }
}

std::vector<DWRITE_HIT_TEST_METRICS> RangeMetrics(IDWriteTextLayout* layout,
                                                  std::size_t start,
                                                  std::size_t length) {
    std::vector<DWRITE_HIT_TEST_METRICS> result;
    if (!layout || length == 0) {
        return result;
    }
    // DirectWrite returns at most one hit-test rectangle per wrapped line for
    // a range, so the character count plus a small margin is a generous and
    // safe buffer. Query with a real buffer directly; some DirectWrite
    // versions do not support a null-buffer preflight for this method.
    const UINT32 capacity =
        static_cast<UINT32>(std::min<std::size_t>(length + 8, 100000));
    result.resize(capacity);
    UINT32 actual = 0;
    const HRESULT hr = layout->HitTestTextRange(
        static_cast<UINT32>(start), static_cast<UINT32>(length), 0.0f, 0.0f,
        result.data(), capacity, &actual);
    if (FAILED(hr) || actual == 0) {
        result.clear();
        return result;
    }
    result.resize(std::min<UINT32>(actual, capacity));
    return result;
}

RectF SnapCaretRect(const RectF& rect, float dpi) {
    const float scale = dpi > 0.0f ? dpi / 96.0f : 1.0f;
    // Keep the insertion bar at a stable device-pixel width and snap its
    // edges to the pixel grid. Fractional DirectWrite coordinates combined
    // with a rounded antialiased rectangle make the caret look soft.
    const float widthPixels = std::max(1.0f, std::round(2.0f * scale));
    const float heightPixels =
        std::max(1.0f, std::round(std::max(1.0f, rect.Height()) * scale));
    const float leftPixels = std::round(rect.left * scale);
    const float topPixels = std::round(rect.top * scale);
    return MakeRect(leftPixels / scale, topPixels / scale,
                    (leftPixels + widthPixels) / scale,
                    (topPixels + heightPixels) / scale);
}

void DrawCrispCaret(ID2D1RenderTarget* target, const RectF& rect,
                    ID2D1SolidColorBrush* brush, float dpi) {
    if (!target || !brush || rect.Width() <= 0.0f || rect.Height() <= 0.0f) {
        return;
    }
    const RectF snapped = SnapCaretRect(rect, dpi);
    const D2D1_ANTIALIAS_MODE previous = target->GetAntialiasMode();
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    target->FillRectangle(ToD2D(snapped), brush);
    target->SetAntialiasMode(previous);
}

D2D1_COLOR_F CodeThemeNormal() {
    return Color(0xE0EBE3);
}

D2D1_COLOR_F CodeThemeLink() {
    return Color(0x92D7B8);
}

D2D1_COLOR_F CodeThemeInlineCode() {
    return Color(0xF1CA83);
}

D2D1_COLOR_F LightThemeNormal(mdge::BlockType type) {
    switch (type) {
        case mdge::BlockType::Quote:
            return Color(0x59655D);
        case mdge::BlockType::Heading1:
        case mdge::BlockType::Heading2:
        case mdge::BlockType::Heading3:
            return Color(0x1D2621);
        case mdge::BlockType::TaskChecked:
            return Color(0x8A938D);
        default:
            return Color(0x303A34);
    }
}

D2D1_COLOR_F LightThemeLink() {
    return Color(0x1B7158);
}

D2D1_COLOR_F LightThemeInlineCode() {
    return Color(0x8A5B21);
}

class InlineTextRenderer final : public IDWriteTextRenderer {
  public:
    InlineTextRenderer(ID2D1RenderTarget* target, const mdge::Block* block,
                       const D2D1_COLOR_F& normal, const D2D1_COLOR_F& link,
                       const D2D1_COLOR_F& inlineCode, float dpiScale)
        : target_(target), block_(block), dpiScale_(dpiScale) {
        target_->CreateSolidColorBrush(normal, normal_.GetAddressOf());
        target_->CreateSolidColorBrush(link, link_.GetAddressOf());
        target_->CreateSolidColorBrush(inlineCode, code_.GetAddressOf());
    }

    IFACEMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteTextRenderer)) {
            *object = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return 1; }
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }

    IFACEMETHODIMP IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        if (!disabled) {
            return E_POINTER;
        }
        *disabled = FALSE;
        return S_OK;
    }

    IFACEMETHODIMP GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        if (!transform) {
            return E_POINTER;
        }
        D2D1_MATRIX_3X2_F current{};
        target_->GetTransform(&current);
        transform->m11 = current._11;
        transform->m12 = current._12;
        transform->m21 = current._21;
        transform->m22 = current._22;
        transform->dx = current._31;
        transform->dy = current._32;
        return S_OK;
    }

    IFACEMETHODIMP GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override {
        if (!pixelsPerDip) {
            return E_POINTER;
        }
        *pixelsPerDip = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
        return S_OK;
    }

    IFACEMETHODIMP DrawGlyphRun(void*, FLOAT baselineX, FLOAT baselineY,
                                DWRITE_MEASURING_MODE measuringMode,
                                const DWRITE_GLYPH_RUN* glyphRun,
                                const DWRITE_GLYPH_RUN_DESCRIPTION* description,
                                IUnknown*) override {
        if (!glyphRun) {
            return E_INVALIDARG;
        }
        ID2D1Brush* brush = normal_.Get();
        if (description && block_ &&
            description->textPosition < block_->styles.size()) {
            const std::uint8_t style = block_->styles[description->textPosition];
            if ((style & mdge::StyleLink) != 0 && link_) {
                brush = link_.Get();
            } else if ((style & mdge::StyleCode) != 0 && code_) {
                brush = code_.Get();
            }
        }
        target_->DrawGlyphRun(D2D1::Point2F(baselineX, baselineY), glyphRun, brush,
                              measuringMode);
        return S_OK;
    }

    IFACEMETHODIMP DrawUnderline(void*, FLOAT baselineX, FLOAT baselineY,
                                 const DWRITE_UNDERLINE* underline,
                                 IUnknown*) override {
        if (!underline || !link_) {
            return S_OK;
        }
        const float top = baselineY + underline->offset;
        const D2D1_RECT_F rect =
            D2D1::RectF(baselineX, top, baselineX + underline->width,
                        top + std::max(1.0f, underline->thickness));
        target_->FillRectangle(rect, link_.Get());
        return S_OK;
    }

    IFACEMETHODIMP DrawStrikethrough(void*, FLOAT, FLOAT,
                                     const DWRITE_STRIKETHROUGH*,
                                     IUnknown*) override {
        return S_OK;
    }

    IFACEMETHODIMP DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*,
                                    BOOL, BOOL, IUnknown*) override {
        return S_OK;
    }

  private:
    ID2D1RenderTarget* target_ = nullptr;
    const mdge::Block* block_ = nullptr;
    ComPtr<ID2D1SolidColorBrush> normal_;
    ComPtr<ID2D1SolidColorBrush> link_;
    ComPtr<ID2D1SolidColorBrush> code_;
    float dpiScale_ = 1.0f;
};

void DrawLayoutWithRenderer(ID2D1RenderTarget* target, ID2D1HwndRenderTarget* /*unused*/,
                            IDWriteTextLayout* layout, const mdge::Block* block,
                            float originX, float originY,
                            const D2D1_COLOR_F& normal,
                            const D2D1_COLOR_F& link,
                            const D2D1_COLOR_F& inlineCode, float dpiScale) {
    if (!target || !layout) {
        return;
    }
    D2D1_MATRIX_3X2_F saved{};
    target->GetTransform(&saved);
    target->SetTransform(D2D1::Matrix3x2F::Translation(originX, originY));
    InlineTextRenderer renderer(target, block, normal, link, inlineCode, dpiScale);
    layout->Draw(nullptr, &renderer, 0.0f, 0.0f);
    target->SetTransform(saved);
}

std::wstring PlaceholderText(mdge::BlockType type) {
    switch (type) {
        case mdge::BlockType::Heading1: return L"输入一级标题";
        case mdge::BlockType::Heading2: return L"输入二级标题";
        case mdge::BlockType::Heading3: return L"输入三级标题";
        case mdge::BlockType::Bullet: return L"列表项";
        case mdge::BlockType::Numbered: return L"编号步骤";
        case mdge::BlockType::TaskUnchecked:
        case mdge::BlockType::TaskChecked: return L"输入任务，Enter 添加下一项";
        case mdge::BlockType::Quote: return L"引用内容";
        case mdge::BlockType::Code: return L"输入代码";
        default: return L"开始输入正文，或从左侧拖入内容块";
    }
}

}  // namespace

void App::DrawHeader() {
    ID2D1HwndRenderTarget* target = renderTarget_.Get();
    if (!target) {
        return;
    }
    FillRect(ui_.header, palette::kPaper);
    FillRect(ui_.brand, palette::kSidebar);
    StrokeLine(0.0f, kHeaderHeight - 0.5f, clientWidth_, kHeaderHeight - 0.5f,
               palette::kLine, 1.0f);
    StrokeLine(kSidebarWidth - 0.5f, 0.0f, kSidebarWidth - 0.5f, kHeaderHeight,
               0x2D3C34, 1.0f);

    const RectF logoRect = MakeRect(24.0f, 18.0f, 58.0f, 52.0f);
    FillRounded(logoRect, 10.0f, palette::kAccent);
    const RectF pageRect = MakeRect(logoRect.left + 9.0f, logoRect.top + 7.0f,
                                    logoRect.right - 9.0f, logoRect.bottom - 8.0f);
    FillRounded(pageRect, 2.5f, 0xFDFBF5);
    StrokeLine(pageRect.left + 4.0f, pageRect.top + 6.0f,
               pageRect.right - 4.0f, pageRect.top + 6.0f, palette::kAccent, 1.6f);
    StrokeLine(pageRect.left + 4.0f, pageRect.top + 10.0f,
               pageRect.right - 7.0f, pageRect.top + 10.0f, 0x8DBBAA, 1.4f);
    StrokeLine(pageRect.left + 4.0f, pageRect.top + 14.0f,
               pageRect.right - 5.0f, pageRect.top + 14.0f, 0x8DBBAA, 1.4f);
    FillRounded(MakeRect(pageRect.left + 4.0f, pageRect.bottom - 7.0f,
                         pageRect.left + 13.0f, pageRect.bottom - 4.0f),
                1.5f, palette::kGold);

    DrawUiLabel(L"MDGE", uiBrand_.Get(),
                MakeRect(68.0f, 13.0f, 190.0f, 36.0f), palette::kSidebarText,
                UiTextAlign::Left, UiTextVAlign::Middle);
    DrawUiLabel(L"VISUAL MARKDOWN STUDIO", uiTiny_.Get(),
                MakeRect(69.0f, 37.0f, 235.0f, 52.0f), palette::kSidebarMuted,
                UiTextAlign::Left, UiTextVAlign::Middle);

    DrawUiLabel(document_.title.empty() ? L"未命名文档" : document_.title,
                uiDocTitle_.Get(), ui_.documentTitleRect, palette::kInk,
                UiTextAlign::Left, UiTextVAlign::Middle);
    std::wstring subtitle;
    if (!projectPath_.empty()) {
        subtitle = mdge::FileName(projectPath_);
        subtitle += dirty_ ? L" / 有未保存的修改" : L" / 已保存";
    } else if (!markdownPath_.empty()) {
        subtitle = mdge::FileName(markdownPath_);
        subtitle += L" / 导入的 Markdown";
        if (dirty_) {
            subtitle += L" / 有未保存的修改";
        }
    } else {
        subtitle = L"未保存的 MDGE 项目";
    }
    if (!document_.author.empty()) {
        subtitle = document_.author + L"  /  " + subtitle;
    }
    DrawUiLabel(subtitle, uiSmall_.Get(), ui_.documentSubtitleRect,
                dirty_ ? palette::kGold : palette::kTextMuted, UiTextAlign::Left,
                UiTextVAlign::Middle);

    for (const ButtonSpec& button : ui_.headerButtons) {
        const std::wstring label = TopLabel(button.action);
        if (label.empty()) {
            continue;
        }
        const bool primary = button.action == ActionId::SaveProject;
        const bool active = ActionActive(button.action);
        DrawTopButton(button, label, primary, active);
    }
    StrokeLine(ui_.headerContent.left, kHeaderHeight - 0.5f,
               ui_.headerContent.right, kHeaderHeight - 0.5f, palette::kLine, 1.0f);
}

void App::DrawSidebar() {
    ID2D1HwndRenderTarget* target = renderTarget_.Get();
    if (!target) {
        return;
    }
    FillRect(ui_.sidebar, palette::kSidebar);

    // Document card.
    const bool cardHovered = hoveredDocCard_;
    FillRounded(ui_.docCard, 12.0f, cardHovered ? 0x28382F : 0x22312A);
    StrokeRounded(ui_.docCard, 12.0f, 0x2D4036, 1.0f, 0.9f);
    FillRounded(ui_.docIconRect, 10.0f, 0x31453A);

    bool iconDrawn = false;
    if (!document_.iconPath.empty()) {
        const mdge::ResolvedAssetPath resolved =
            mdge::ResolveAssetPath(document_.iconPath, BaseDocumentPath());
        if (resolved.exists && renderTarget_) {
            ImageAssetPtr asset = GetImageAsset(resolved.resolvedPath);
            if (asset && asset->loaded && asset->bitmap) {
                const float availableWidth = ui_.docIconRect.Width() - 8.0f;
                const float availableHeight = ui_.docIconRect.Height() - 8.0f;
                float drawWidth = availableWidth;
                float drawHeight = availableHeight;
                if (asset->height > 0) {
                    const float aspect = static_cast<float>(asset->width) /
                                         static_cast<float>(asset->height);
                    drawHeight = availableWidth / std::max(0.1f, aspect);
                    if (drawHeight > availableHeight) {
                        drawHeight = availableHeight;
                        drawWidth = availableHeight * aspect;
                    }
                }
                const RectF dest =
                    MakeRect(ui_.docIconRect.CenterX() - drawWidth * 0.5f,
                             ui_.docIconRect.CenterY() - drawHeight * 0.5f,
                             ui_.docIconRect.CenterX() + drawWidth * 0.5f,
                             ui_.docIconRect.CenterY() + drawHeight * 0.5f);
                renderTarget_->DrawBitmap(asset->bitmap.Get(), ToD2D(dest), 1.0f,
                                          D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                iconDrawn = true;
            }
        }
    }
    if (!iconDrawn) {
        DrawSidebarIcon(ui_.docIconRect);
    }

    DrawUiLabel(document_.title.empty() ? L"未命名文档" : document_.title,
                uiButtonBold_.Get(), ui_.docTitleRect, palette::kSidebarText,
                UiTextAlign::Left, UiTextVAlign::Top, true);
    std::wstring meta = document_.author.empty() ? L"未设置作者" : document_.author;
    meta += L"  /  ";
    if (!projectPath_.empty()) {
        meta += dirty_ ? L"编辑中" : L"已保存";
    } else if (!markdownPath_.empty()) {
        meta += dirty_ ? L"编辑中" : L"已导入";
    } else {
        meta += dirty_ ? L"编辑中" : L"未保存";
    }
    DrawUiLabel(meta, uiTiny_.Get(), ui_.docMetaRect, palette::kSidebarMuted,
                UiTextAlign::Left, UiTextVAlign::Middle);
    DrawUiLabel(L"编辑文档资料", uiSmall_.Get(),
                MakeRect(ui_.docHintRect.left, ui_.docHintRect.top,
                         ui_.docHintRect.right - 18.0f, ui_.docHintRect.bottom),
                palette::kAccentSoft, UiTextAlign::Left, UiTextVAlign::Middle);
    DrawIcon(renderTarget_.Get(), IconKind::ArrowRight,
             MakeRect(ui_.docHintRect.right - 14.0f,
                      ui_.docHintRect.CenterY() - 7.0f,
                      ui_.docHintRect.right, ui_.docHintRect.CenterY() + 7.0f),
             Brush(palette::kAccentSoft, 0.8f), 1.4f);

    // Palette heading.
    DrawUiLabel(L"内容块", uiButtonBold_.Get(), ui_.paletteLabelRect,
                palette::kSidebarText, UiTextAlign::Left, UiTextVAlign::Middle);
    DrawUiLabel(L"拖动或点击", uiTiny_.Get(), ui_.paletteHintRect,
                palette::kSidebarMuted, UiTextAlign::Right, UiTextVAlign::Middle);

    const auto badgeColors = [](mdge::BlockType type, std::uint32_t& background,
                                std::uint32_t& text) {
        switch (type) {
            case mdge::BlockType::Heading2:
                background = 0x2F4B3E;
                text = 0x9CD7C1;
                break;
            case mdge::BlockType::Bullet:
            case mdge::BlockType::Numbered:
                background = 0x3A3325;
                text = 0xE2C28A;
                break;
            case mdge::BlockType::TaskUnchecked:
            case mdge::BlockType::TaskChecked:
                background = 0x2E4236;
                text = 0x9CD7B2;
                break;
            case mdge::BlockType::Quote:
                background = 0x293A3C;
                text = 0xA9C9CD;
                break;
            case mdge::BlockType::Code:
                background = 0x25342B;
                text = 0xB9D6C4;
                break;
            case mdge::BlockType::Image:
                background = 0x293A41;
                text = 0x9EC8D8;
                break;
            case mdge::BlockType::Divider:
                background = 0x343A34;
                text = 0xD1CABD;
                break;
            default:
                background = 0x2B3730;
                text = 0xD8E2DB;
                break;
        }
    };

    for (std::size_t i = 0; i < ui_.paletteItems.size(); ++i) {
        const PaletteSpec& item = ui_.paletteItems[i];
        const bool hovered = hoveredPalette_ == static_cast<int>(i);
        if (hovered) {
            FillRounded(item.rect, 8.0f, palette::kSidebarSoft);
            FillRounded(MakeRect(item.rect.left, item.rect.top + 8.0f,
                                 item.rect.left + 2.0f, item.rect.bottom - 8.0f),
                        1.0f, palette::kAccent);
        }
        std::uint32_t badgeBackground = 0x2B3730;
        std::uint32_t badgeText = 0xD8E2DB;
        badgeColors(item.type, badgeBackground, badgeText);
        const float badgeHeight = std::min(22.0f, item.rect.Height() - 10.0f);
        const RectF badge =
            MakeRect(item.rect.left + 9.0f, item.rect.CenterY() - badgeHeight / 2.0f,
                     item.rect.left + 42.0f,
                     item.rect.CenterY() + badgeHeight / 2.0f);
        FillRounded(badge, 5.0f, badgeBackground);
        DrawUiLabel(mdge::BlockTypeShortName(item.type), uiBadge_.Get(), badge,
                    badgeText, UiTextAlign::Center, UiTextVAlign::Middle);
        DrawUiLabel(mdge::BlockTypeName(item.type), uiButton_.Get(),
                    MakeRect(item.rect.left + 52.0f, item.rect.top,
                             item.rect.right - 28.0f, item.rect.bottom),
                    palette::kSidebarText, UiTextAlign::Left,
                    UiTextVAlign::Middle);
        DrawIcon(renderTarget_.Get(), IconKind::Plus,
                 MakeRect(item.rect.right - 23.0f, item.rect.CenterY() - 6.0f,
                          item.rect.right - 11.0f, item.rect.CenterY() + 6.0f),
                 Brush(palette::kSidebarMuted, hovered ? 1.0f : 0.55f), 1.3f);
    }

    if (ui_.sidebarTipRect.Width() > 0.0f) {
        FillRounded(ui_.sidebarTipRect, 10.0f, 0x1F2C25);
        StrokeRounded(ui_.sidebarTipRect, 10.0f, 0x2E3E34, 1.0f, 0.8f);
        DrawUiLabel(L"拖动提示", uiBadge_.Get(),
                    MakeRect(ui_.sidebarTipRect.left + 13.0f,
                             ui_.sidebarTipRect.top + 10.0f,
                             ui_.sidebarTipRect.right - 13.0f,
                             ui_.sidebarTipRect.top + 26.0f),
                    palette::kAccentSoft, UiTextAlign::Left, UiTextVAlign::Middle);
        DrawUiLabel(L"拖动行首把手调整顺序。\n图片文件可直接拖入画布。",
                    uiTiny_.Get(),
                    MakeRect(ui_.sidebarTipRect.left + 13.0f,
                             ui_.sidebarTipRect.top + 29.0f,
                             ui_.sidebarTipRect.right - 13.0f,
                             ui_.sidebarTipRect.bottom - 9.0f),
                    palette::kSidebarMuted, UiTextAlign::Left, UiTextVAlign::Top,
                    true);
    }

    if (ui_.sidebarFooterRect.Width() > 0.0f) {
        DrawUiLabel(L"MDGE 1.0", uiTiny_.Get(), ui_.sidebarFooterRect,
                    palette::kSidebarMuted, UiTextAlign::Left,
                    UiTextVAlign::Middle);
        DrawUiLabel(L"TEXT 8 MS / MDGE V1", uiTiny_.Get(), ui_.sidebarFooterRect,
                    palette::kSidebarMuted, UiTextAlign::Right,
                    UiTextVAlign::Middle);
    }
}

void App::DrawSidebarIcon(const RectF& bounds) {
    if (!renderTarget_) {
        return;
    }
    FillRounded(bounds, 8.0f, 0x3B5C4C);
    const RectF page =
        MakeRect(bounds.left + 11.0f, bounds.top + 8.0f, bounds.right - 11.0f,
                 bounds.bottom - 8.0f);
    FillRounded(page, 2.0f, 0xF9F5E9);
    StrokeLine(page.left + 4.0f, page.top + 7.0f, page.right - 5.0f,
               page.top + 7.0f, palette::kAccent, 1.6f);
    StrokeLine(page.left + 4.0f, page.top + 12.0f, page.right - 9.0f,
               page.top + 12.0f, 0x79A38F, 1.3f);
    StrokeLine(page.left + 4.0f, page.top + 17.0f, page.right - 7.0f,
               page.top + 17.0f, 0x79A38F, 1.3f);
    FillRounded(MakeRect(page.left + 4.0f, page.bottom - 7.0f, page.right - 9.0f,
                         page.bottom - 4.0f),
                1.5f, palette::kGold);
}

void App::DrawFormatBar() {
    if (!renderTarget_) {
        return;
    }
    FillRect(ui_.formatBar, 0xFAF7F0);
    StrokeLine(ui_.formatBar.left, ui_.formatBar.bottom - 0.5f,
               ui_.formatBar.right, ui_.formatBar.bottom - 0.5f, palette::kLine,
               1.0f);

    for (const ButtonSpec& button : ui_.formatButtons) {
        const std::wstring label = FormatLabel(button.action);
        if (label.empty()) {
            continue;
        }
        const bool active = ActionActive(button.action);
        const bool bold = button.action == ActionId::StyleHeading1 ||
                          button.action == ActionId::StyleHeading2 ||
                          button.action == ActionId::StyleHeading3 ||
                          button.action == ActionId::InlineBold;
        DrawFormatButton(button, label, active, bold);
    }

    const auto findButton = [&](ActionId action) -> const ButtonSpec* {
        for (const ButtonSpec& button : ui_.formatButtons) {
            if (button.action == action) {
                return &button;
            }
        }
        return nullptr;
    };
    const ButtonSpec* paragraph = findButton(ActionId::StyleParagraph);
    const ButtonSpec* link = findButton(ActionId::InlineLink);
    if (paragraph) {
        const float x = paragraph->rect.right + 7.0f;
        StrokeLine(x, ui_.formatBar.top + 15.0f, x,
                   ui_.formatBar.bottom - 15.0f, palette::kLineStrong, 1.0f, 0.55f);
    }
    if (link) {
        const float x = link->rect.right + 7.0f;
        StrokeLine(x, ui_.formatBar.top + 15.0f, x,
                   ui_.formatBar.bottom - 15.0f, palette::kLineStrong, 1.0f, 0.55f);
    }

    std::wstring hint;
    const mdge::Block* block = ActiveBlock();
    if (sourceFocused_) {
        hint = L"源码编辑中 / 直接输入 Markdown，点击画布返回可视化编辑";
    } else if (!block) {
        hint = L"从左侧拖入内容块开始写作";
    } else if (mdge::IsMedia(block->type)) {
        hint = L"双击图片可更换 / 拖动左侧把手排序";
    } else if (block->type == mdge::BlockType::Code) {
        hint = L"Tab 插入四个空格 / 拖动左侧把手排序";
    } else if (mdge::IsTask(block->type)) {
        hint = L"点击左侧方框勾选 / Ctrl+Enter 切换完成状态";
    } else {
        hint = L"拖动行首把手排序 / 选中文字后使用行内样式";
    }
    const float hintLeft = link ? link->rect.right + 28.0f : ui_.formatBar.left + 24.0f;
    const float hintRight = ui_.formatBar.right - 22.0f;
    if (hintRight - hintLeft > 90.0f) {
        DrawUiLabel(hint, uiSmall_.Get(),
                    MakeRect(hintLeft, ui_.formatBar.top, hintRight,
                             ui_.formatBar.bottom),
                    palette::kTextMuted, UiTextAlign::Right, UiTextVAlign::Middle,
                    false, 0.9f);
    }
}

void App::DrawCanvas() {
    if (!renderTarget_) {
        return;
    }
    FillRect(ui_.canvas, palette::kPaper);
    renderTarget_->PushAxisAlignedClip(ToD2D(ui_.canvas),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1SolidColorBrush* dot = Brush(0xDDD5C5, 0.7f);
    if (dot) {
        const float startX = std::floor(ui_.canvas.left / 24.0f) * 24.0f;
        const float startY = std::floor(ui_.canvas.top / 24.0f) * 24.0f;
        for (float y = startY; y < ui_.canvas.bottom; y += 24.0f) {
            for (float x = startX; x < ui_.canvas.right; x += 24.0f) {
                renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 0.9f, 0.9f),
                                           dot);
            }
        }
    }
    renderTarget_->PopAxisAlignedClip();
}

void App::DrawPage() {
    if (!renderTarget_) {
        return;
    }
    EnsurePageLayout();
    if (!page_.valid) {
        return;
    }
    const float pageTop = PageTop();
    const RectF pageRect = MakeRect(page_.pageLeft, pageTop,
                                    page_.pageLeft + page_.pageWidth,
                                    pageTop + page_.pageHeight);
    renderTarget_->PushAxisAlignedClip(ToD2D(ui_.canvas),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    FillRounded(pageRect.Offset(0.0f, 7.0f).Inflated(3.0f), 9.0f, 0xA39B8B, 0.08f);
    FillRounded(pageRect.Offset(0.0f, 3.0f).Inflated(1.0f), 7.0f, 0xA39B8B, 0.10f);
    FillRounded(pageRect, 6.0f, palette::kPage);
    StrokeRounded(pageRect, 6.0f, 0xE8E1D3, 1.0f, 0.9f);

    const float pageFooterY = pageTop + page_.pageHeight - 28.0f;
    if (pageFooterY > ui_.canvas.top + 10.0f) {
        DrawUiLabel(L"MDGE / VISUAL MARKDOWN", uiTiny_.Get(),
                    MakeRect(page_.pageLeft + page_.contentLeft, pageFooterY,
                             page_.pageLeft + page_.contentLeft + 220.0f,
                             pageFooterY + 16.0f),
                    palette::kLineStrong, UiTextAlign::Left,
                    UiTextVAlign::Middle, false, 0.9f);
        DrawUiLabel(L"01", uiTiny_.Get(),
                    MakeRect(page_.pageLeft + page_.pageWidth - 80.0f, pageFooterY,
                             page_.pageLeft + page_.pageWidth - page_.contentLeft,
                             pageFooterY + 16.0f),
                    palette::kLineStrong, UiTextAlign::Right,
                    UiTextVAlign::Middle, false, 0.9f);
    }

    for (const BlockVisual& visual : page_.blocks) {
        if (visual.index >= document_.blocks.size()) {
            continue;
        }
        const mdge::Block& block = document_.blocks[visual.index];
        const bool active = selection_.block == static_cast<int>(visual.index);
        const bool hovered = hoveredBlock_ == static_cast<int>(visual.index) ||
                             hoveredGrip_ == static_cast<int>(visual.index);
        DrawBlock(visual, block, active, hovered, pageTop);
    }

    renderTarget_->PopAxisAlignedClip();

    // Custom vertical scrollbar for the canvas.
    const float range = EditorScrollRange();
    if (range > 0.0f) {
        const RectF& track = ui_.canvasScrollTrack;
        const float total = page_.pageHeight + 48.0f;
        const float thumbHeight =
            std::max(30.0f, track.Height() * ui_.canvas.Height() / total);
        const float travel = track.Height() - thumbHeight;
        const float fraction = range > 0.0f ? editorScroll_ / range : 0.0f;
        const float thumbY = track.top + travel * fraction;
        FillRounded(MakeRect(track.CenterX() - 2.5f, track.top,
                             track.CenterX() + 2.5f, track.bottom),
                    3.0f, palette::kInkMuted, 0.14f);
        FillRounded(MakeRect(track.CenterX() - 3.0f, thumbY,
                             track.CenterX() + 3.0f, thumbY + thumbHeight),
                    3.0f, palette::kInkMuted, 0.48f);
    }
}

void App::DrawBlock(const BlockVisual& visual, const mdge::Block& block,
                    bool active, bool hovered, float pageTop) {
    const float pageTopScreen = pageTop;
    const RectF card =
        visual.card.Offset(page_.pageLeft, pageTopScreen);
    if (active) {
        FillRounded(card, 8.0f, 0xFBF8F0, 0.94f);
        StrokeRounded(card, 8.0f, palette::kAccent, 1.0f, 0.2f);
    } else if (hovered) {
        FillRounded(card, 8.0f, palette::kCard, 0.55f);
    }

    if ((hovered || active) && drag_.kind == DragState::Kind::None) {
        const RectF grip =
            visual.gripRect.Offset(page_.pageLeft, pageTopScreen);
        DrawIcon(renderTarget_.Get(), IconKind::Drag,
                 MakeRect(grip.left + 3.0f, grip.top + 1.0f, grip.right - 3.0f,
                          grip.bottom - 1.0f),
                 Brush(active ? palette::kAccent : palette::kLineStrong, 0.9f),
                 1.4f);
    }

    switch (block.type) {
        case mdge::BlockType::Code:
            DrawCodeBlock(visual, block, active, pageTopScreen);
            break;
        case mdge::BlockType::Image:
            DrawImageBlock(visual, block, active, pageTopScreen);
            break;
        case mdge::BlockType::Divider: {
            const RectF cardScreen =
                visual.card.Offset(page_.pageLeft, pageTopScreen);
            const float y = cardScreen.CenterY();
            const float x1 = page_.pageLeft + page_.contentLeft;
            const float x2 = x1 + page_.contentWidth;
            StrokeLine(x1, y, x2, y, palette::kLineStrong, 1.0f, 0.9f);
            renderTarget_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F((x1 + x2) * 0.5f, y), 2.2f, 2.2f),
                Brush(palette::kAccent, 0.75f));
            break;
        }
        default:
            DrawTextBlock(visual, block, active, pageTopScreen);
            break;
    }
}

RectF App::TaskCheckboxRect(const BlockVisual& visual) const {
    if (!visual.textLayout) {
        return {};
    }
    FLOAT firstX = 0.0f;
    FLOAT firstY = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(visual.textLayout->HitTestTextPosition(0, FALSE, &firstX, &firstY,
                                                      &metrics))) {
        return {};
    }
    const float size = 16.0f;
    const float lineHeight = metrics.height > 0.0f ? metrics.height : 22.0f;
    const float left = visual.textRect.left - 30.0f;
    const float top = visual.textRect.top + firstY + (lineHeight - size) * 0.5f;
    return MakeRect(left, top, left + size, top + size);
}

void App::DrawTextBlock(const BlockVisual& visual, const mdge::Block& block,
                        bool active, float pageTop) {
    if (!renderTarget_ || !visual.textLayout) {
        return;
    }
    const float pageLeft = page_.pageLeft;
    const float textX = pageLeft + visual.textRect.left;
    const float textY = pageTop + visual.textRect.top;
    const float textWidth = visual.textRect.Width();
    const float textHeight = visual.textRect.Height();

    if (block.type == mdge::BlockType::Quote) {
        const RectF card = visual.card.Offset(pageLeft, pageTop);
        FillRounded(MakeRect(textX - 17.0f, card.top + 11.0f, textX - 13.0f,
                             card.bottom - 11.0f),
                    2.0f, palette::kAccent, 0.8f);
    }

    FLOAT firstX = 0.0f;
    FLOAT firstY = 0.0f;
    DWRITE_HIT_TEST_METRICS firstMetrics{};
    visual.textLayout->HitTestTextPosition(0, FALSE, &firstX, &firstY,
                                           &firstMetrics);
    if (mdge::IsTask(block.type)) {
        RectF box = TaskCheckboxRect(visual);
        if (box.Width() > 0.0f) {
            box = box.Offset(pageLeft, pageTop);
            const bool checked = block.type == mdge::BlockType::TaskChecked;
            if (checked) {
                FillRounded(box, 4.0f, palette::kAccent, 1.0f);
                ID2D1SolidColorBrush* checkBrush = Brush(0xFAF7F0, 1.0f);
                if (checkBrush) {
                    renderTarget_->DrawLine(
                        D2D1::Point2F(box.left + 3.7f, box.CenterY()),
                        D2D1::Point2F(box.left + 6.7f, box.bottom - 4.2f),
                        checkBrush, 1.8f);
                    renderTarget_->DrawLine(
                        D2D1::Point2F(box.left + 6.7f, box.bottom - 4.2f),
                        D2D1::Point2F(box.right - 3.6f, box.top + 4.6f),
                        checkBrush, 1.8f);
                }
            } else {
                FillRounded(box, 4.0f, 0xFBF8F0, 0.96f);
                StrokeRounded(box, 4.0f, 0x9AA69C, 1.25f, 0.95f);
            }
        }
    } else if (block.type == mdge::BlockType::Bullet) {
        const float markerX = textX - 16.0f;
        const float markerY = textY + firstY + firstMetrics.height * 0.5f;
        renderTarget_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(markerX, markerY), 2.6f, 2.6f),
            Brush(palette::kAccent, 0.85f));
    } else if (block.type == mdge::BlockType::Numbered) {
        const std::wstring number = std::to_wstring(visual.listNumber) + L".";
        DrawUiLabel(number, bodyFormat_.Get(),
                    MakeRect(textX - 31.0f, textY + firstY - 2.0f, textX - 8.0f,
                             textY + firstY + firstMetrics.height + 2.0f),
                    palette::kAccent, UiTextAlign::Right, UiTextVAlign::Middle);
    }

    // Inline code backgrounds (a code block already has its own dark surface).
    for (std::size_t i = 0; i < block.text.size();) {
        if (i >= block.styles.size() || (block.styles[i] & mdge::StyleCode) == 0) {
            ++i;
            continue;
        }
        std::size_t end = i + 1;
        while (end < block.text.size() && end < block.styles.size() &&
               (block.styles[end] & mdge::StyleCode) != 0) {
            ++end;
        }
        for (const DWRITE_HIT_TEST_METRICS& metric :
             RangeMetrics(visual.textLayout.Get(), i, end - i)) {
            if (metric.width <= 0.0f) {
                continue;
            }
            FillRounded(MakeRect(textX + metric.left - 2.0f,
                                 textY + metric.top + 2.0f,
                                 textX + metric.left + metric.width + 2.0f,
                                 textY + metric.top + metric.height - 2.0f),
                        3.0f, palette::kGoldSoft, 1.0f);
        }
        i = end;
    }

    if (active && HasSelection()) {
        const std::size_t start = SelectionStart();
        const std::size_t end = SelectionEnd();
        ID2D1SolidColorBrush* brush = Brush(0xA8D8C3, 0.9f);
        for (const DWRITE_HIT_TEST_METRICS& metric :
             RangeMetrics(visual.textLayout.Get(), start, end - start)) {
            if (metric.width <= 0.0f && metric.height <= 0.0f) {
                continue;
            }
            renderTarget_->FillRectangle(
                D2D1::RectF(textX + metric.left, textY + metric.top,
                            textX + metric.left + metric.width,
                            textY + metric.top + metric.height),
                brush);
        }
    }

    const float reveal = RevealProgress();
    const bool revealClipped = reveal < 0.999f;
    if (revealClipped) {
        renderTarget_->PushAxisAlignedClip(
            D2D1::RectF(textX - 3.0f, textY - 4.0f,
                        textX + std::max(1.0f, textWidth * reveal) + 3.0f,
                        textY + textHeight + 4.0f),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }

    if (block.text.empty()) {
        IDWriteTextFormat* format = bodyFormat_.Get();
        switch (block.type) {
            case mdge::BlockType::Heading1: format = h1Format_.Get(); break;
            case mdge::BlockType::Heading2: format = h2Format_.Get(); break;
            case mdge::BlockType::Heading3: format = h3Format_.Get(); break;
            case mdge::BlockType::Quote: format = quoteFormat_.Get(); break;
            default: break;
        }
        DrawUiLabel(PlaceholderText(block.type), format,
                    MakeRect(textX, textY, textX + textWidth, textY + textHeight),
                    palette::kTextMuted, UiTextAlign::Left, UiTextVAlign::Top,
                    false, 0.48f);
    } else {
        DrawLayoutWithRenderer(renderTarget_.Get(), renderTarget_.Get(),
                               visual.textLayout.Get(), &block, textX, textY,
                               LightThemeNormal(block.type), LightThemeLink(),
                               LightThemeInlineCode(), dpi_ / 96.0f);
    }

    if (active && hasFocus_ && !sourceFocused_ && !HasSelection() && caretBlinkOn_) {
        FLOAT caretX = 0.0f;
        FLOAT caretY = 0.0f;
        DWRITE_HIT_TEST_METRICS caretMetrics{};
        if (SUCCEEDED(visual.textLayout->HitTestTextPosition(
                static_cast<UINT32>(selection_.caret), FALSE, &caretX, &caretY,
                &caretMetrics))) {
            const float h = caretMetrics.height > 0.0f ? caretMetrics.height
                                                       : static_cast<float>(27.0f);
            DrawCrispCaret(renderTarget_.Get(),
                           MakeRect(textX + caretX, textY + caretY,
                                    textX + caretX + 2.0f, textY + caretY + h),
                           Brush(palette::kAccent, 1.0f), dpi_);
        }
    }

    if (revealClipped) {
        renderTarget_->PopAxisAlignedClip();
    }
}

void App::DrawCodeBlock(const BlockVisual& visual, const mdge::Block& block,
                        bool active, float pageTop) {
    if (!renderTarget_ || !visual.textLayout) {
        return;
    }
    const float pageLeft = page_.pageLeft;
    const RectF codeSurface = MakeRect(
        pageLeft + page_.contentLeft,
        pageTop + visual.card.top,
        pageLeft + page_.contentLeft + page_.contentWidth,
        pageTop + visual.card.bottom);
    FillRounded(codeSurface, 11.0f, palette::kCodeBg);
    StrokeRounded(codeSurface, 11.0f, 0x34473B, 1.0f, active ? 0.6f : 0.35f);
    if (active) {
        StrokeRounded(codeSurface, 11.0f, palette::kAccent, 1.2f, 0.9f);
    }

    const std::wstring language =
        mdge::TrimText(block.language).empty() ? L"CODE" : mdge::TrimText(block.language);
    DrawUiLabel(language, uiBadge_.Get(),
                MakeRect(codeSurface.right - 100.0f, codeSurface.top + 8.0f,
                         codeSurface.right - 18.0f, codeSurface.top + 22.0f),
                0x9EB5A6, UiTextAlign::Right, UiTextVAlign::Middle);

    const float textX = pageLeft + visual.textRect.left;
    const float textY = pageTop + visual.textRect.top;
    const float textHeight = visual.textRect.Height();
    const float reveal = RevealProgress();
    const bool clipped = reveal < 0.999f;
    if (clipped) {
        renderTarget_->PushAxisAlignedClip(
            D2D1::RectF(textX - 3.0f, textY - 4.0f,
                        textX + std::max(1.0f, visual.textRect.Width() * reveal) + 3.0f,
                        textY + textHeight + 4.0f),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }

    if (active && HasSelection()) {
        ID2D1SolidColorBrush* brush = Brush(0x8DD2B5, 0.28f);
        for (const DWRITE_HIT_TEST_METRICS& metric :
             RangeMetrics(visual.textLayout.Get(), SelectionStart(),
                          SelectionEnd() - SelectionStart())) {
            renderTarget_->FillRectangle(
                D2D1::RectF(textX + metric.left, textY + metric.top,
                            textX + metric.left + metric.width,
                            textY + metric.top + metric.height),
                brush);
        }
    }

    if (block.text.empty()) {
        DrawUiLabel(L"输入代码", codeFormat_.Get(),
                    MakeRect(textX, textY, textX + visual.textRect.Width(),
                             textY + 30.0f),
                    0xA9B8AC, UiTextAlign::Left, UiTextVAlign::Top, false, 0.8f);
    } else {
        DrawLayoutWithRenderer(renderTarget_.Get(), renderTarget_.Get(),
                               visual.textLayout.Get(), &block, textX, textY,
                               CodeThemeNormal(), CodeThemeLink(),
                               CodeThemeInlineCode(), dpi_ / 96.0f);
    }

    if (active && hasFocus_ && !sourceFocused_ && !HasSelection() && caretBlinkOn_) {
        FLOAT caretX = 0.0f;
        FLOAT caretY = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (SUCCEEDED(visual.textLayout->HitTestTextPosition(
                static_cast<UINT32>(selection_.caret), FALSE, &caretX, &caretY,
                &metrics))) {
            const float h = metrics.height > 0.0f ? metrics.height : 21.0f;
            DrawCrispCaret(renderTarget_.Get(),
                           MakeRect(textX + caretX, textY + caretY,
                                    textX + caretX + 2.0f, textY + caretY + h),
                           Brush(0x9FE2C4, 1.0f), dpi_);
        }
    }

    if (clipped) {
        renderTarget_->PopAxisAlignedClip();
    }
}

void App::DrawImageBlock(const BlockVisual& visual, const mdge::Block& block,
                         bool active, float pageTop) {
    (void)block;
    if (!renderTarget_) {
        return;
    }
    const float pageLeft = page_.pageLeft;
    const RectF imageRect = visual.imageRect.Offset(pageLeft, pageTop);
    FillRounded(imageRect, 7.0f, 0xF0EBE0);
    if (visual.image && visual.image->loaded && visual.image->bitmap) {
        renderTarget_->DrawBitmap(visual.image->bitmap.Get(), ToD2D(imageRect), 1.0f,
                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        FillRounded(imageRect, 7.0f, 0xF3EEE4);
        const float centerX = imageRect.CenterX();
        const float centerY = imageRect.CenterY() - 12.0f;
        DrawIcon(renderTarget_.Get(), IconKind::Image,
                 MakeRect(centerX - 20.0f, centerY - 18.0f, centerX + 20.0f,
                          centerY + 18.0f),
                 Brush(palette::kLineStrong, 0.9f), 1.6f);
        std::wstring message = visual.imageError.empty()
                                   ? L"图像未找到"
                                   : visual.imageError;
        std::wstring path = visual.resolvedImagePath;
        if (path.empty()) {
            path = block.imagePath;
        }
        if (!path.empty()) {
            message += L"\n";
            message += mdgeui::EllipsizeMiddle(path, 72);
        }
        DrawUiLabel(message, uiSmall_.Get(),
                    MakeRect(imageRect.left + 20.0f, centerY + 30.0f,
                             imageRect.right - 20.0f, imageRect.bottom - 10.0f),
                    palette::kTextMuted, UiTextAlign::Center, UiTextVAlign::Top,
                    true, 0.9f);
    }
    StrokeRounded(imageRect, 7.0f,
                  active ? palette::kAccent : palette::kLine,
                  active ? 1.1f : 1.0f, active ? 0.55f : 0.8f);
    if (!mdge::TrimText(block.caption).empty() && visual.captionRect.Width() > 0.0f) {
        const RectF caption = visual.captionRect.Offset(pageLeft, pageTop);
        DrawUiLabel(block.caption, uiSmall_.Get(), caption, palette::kTextMuted,
                    UiTextAlign::Left, UiTextVAlign::Top);
    }
}

void App::DrawSelectionAndCaret(const BlockVisual& visual,
                                const mdge::Block& block, float pageTop) {
    // Selection and caret drawing is integrated into the light/dark text
    // renderers above; this helper is retained for the page composition API.
    (void)visual;
    (void)block;
    (void)pageTop;
}

void App::DrawSourceView() {
    if (!renderTarget_) {
        return;
    }
    BuildSourceLines();
    const RectF body = ui_.previewBody;
    renderTarget_->PushAxisAlignedClip(ToD2D(body),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float gutterLeft = body.left + 2.0f;
    const float gutterRight = body.left + 28.0f;
    const float textX = body.left + 38.0f;
    const std::size_t selectionStart = SourceSelectionStart();
    const std::size_t selectionEnd = SourceSelectionEnd();
    const bool drawSelection = sourceFocused_ && selectionStart < selectionEnd;

    for (std::size_t i = 0; i < sourceLines_.size(); ++i) {
        const SourceLineView& line = sourceLines_[i];
        const float y = body.top + 14.0f + line.offsetY - previewScroll_;
        if (y + line.height < body.top || y > body.bottom) {
            continue;
        }
        if (drawSelection && selectionEnd > line.start &&
            selectionStart < line.start + line.length) {
            const std::size_t localStart =
                selectionStart > line.start ? selectionStart - line.start : 0;
            const std::size_t localEnd =
                std::min(line.length, selectionEnd - line.start);
            if (localEnd > localStart && line.layout) {
                ID2D1SolidColorBrush* selectionBrush =
                    Brush(0xA8D8C3, 0.9f);
                for (const DWRITE_HIT_TEST_METRICS& metric :
                     RangeMetrics(line.layout.Get(), localStart,
                                  localEnd - localStart)) {
                    if (metric.width <= 0.0f) {
                        continue;
                    }
                    renderTarget_->FillRectangle(
                        D2D1::RectF(textX + metric.left, y + metric.top,
                                    textX + metric.left + metric.width,
                                    y + metric.top + metric.height),
                        selectionBrush);
                }
            }
        }

        DrawUiLabel(std::to_wstring(i + 1), uiSmall_.Get(),
                    MakeRect(gutterLeft, y + 1.0f, gutterRight,
                             y + line.height - 1.0f),
                    sourceFocused_ ? 0x7F9B8A : palette::kLineStrong,
                    UiTextAlign::Right, UiTextVAlign::Top, false, 0.85f);
        if (line.layout) {
            D2D1_COLOR_F color = Color(0x59645C);
            switch (line.colorKind) {
                case 1: color = Color(0x176A52); break;
                case 2: color = Color(0xA06A25); break;
                case 3: color = Color(0x776648); break;
                case 4: color = Color(0x386756); break;
                case 5: color = Color(0x385F6A); break;
                default: break;
            }
            ID2D1SolidColorBrush* brush = Brush(color);
            if (brush) {
                renderTarget_->DrawTextLayout(
                    D2D1::Point2F(textX, y), line.layout.Get(), brush,
                    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
        }
    }

    if (sourceFocused_ && hasFocus_ && caretBlinkOn_) {
        int lineIndex = 0;
        float localX = 0.0f;
        float localY = 0.0f;
        float caretHeight = 18.0f;
        if (SourceCaretMetrics(sourceCaret_, lineIndex, localX, localY,
                               caretHeight)) {
            const float y = body.top + 14.0f +
                            sourceLines_[static_cast<std::size_t>(lineIndex)].offsetY -
                            previewScroll_ + localY;
            const float x = textX + localX;
            DrawCrispCaret(renderTarget_.Get(),
                           MakeRect(x, y, x + 2.0f, y + caretHeight),
                           Brush(palette::kAccent, 1.0f), dpi_);
        }
    }

    if (sourceLines_.size() == 1 && sourceLines_[0].text.empty()) {
        DrawUiLabel(sourceFocused_ ? L"输入 Markdown 源码..." : L"点击这里编辑 Markdown 源码",
                    uiSmall_.Get(),
                    MakeRect(textX + 4.0f, body.top + 16.0f, body.right - 12.0f,
                             body.top + 64.0f),
                    palette::kTextMuted, UiTextAlign::Left, UiTextVAlign::Top,
                    true, 0.55f);
    }
    renderTarget_->PopAxisAlignedClip();
}

void App::DrawOutlineView() {
    if (!renderTarget_) {
        return;
    }
    const RectF body = ui_.previewBody;
    renderTarget_->PushAxisAlignedClip(ToD2D(body),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const std::vector<OutlineRow> rows = BuildOutlineRows();
    if (rows.empty()) {
        DrawUiLabel(L"还没有标题。", uiBodyBold_.Get(),
                    MakeRect(body.left + 18.0f, body.top + 28.0f,
                             body.right - 18.0f, body.top + 52.0f),
                    palette::kInk, UiTextAlign::Left, UiTextVAlign::Top);
        DrawUiLabel(L"把内容块设为 H1、H2 或 H3 后，提纲会出现在这里。",
                    uiSmall_.Get(),
                    MakeRect(body.left + 18.0f, body.top + 56.0f,
                             body.right - 18.0f, body.top + 102.0f),
                    palette::kTextMuted, UiTextAlign::Left, UiTextVAlign::Top,
                    true, 0.9f);
        renderTarget_->PopAxisAlignedClip();
        return;
    }
    for (const OutlineRow& row : rows) {
        const float rowY = body.top + row.y;
        if (rowY + row.height < body.top || rowY > body.bottom) {
            continue;
        }
        const float indent = 14.0f + static_cast<float>(row.level - 1) * 14.0f;
        const RectF rowRect =
            MakeRect(body.left + indent, rowY, body.right - 24.0f,
                     rowY + row.height - 4.0f);
        const bool hovered = hoveredOutline_ == static_cast<int>(row.blockIndex);
        if (hovered) {
            FillRounded(rowRect, 7.0f, palette::kAccentPale);
        }
        const RectF badge =
            MakeRect(rowRect.left + 6.0f, rowRect.CenterY() - 9.0f,
                     rowRect.left + 34.0f, rowRect.CenterY() + 9.0f);
        const std::uint32_t badgeColor =
            row.level == 1 ? palette::kAccent : (row.level == 2 ? 0x4E8C76 : 0x8A9B8F);
        FillRounded(badge, 4.0f, badgeColor, row.level == 1 ? 0.16f : 0.11f);
        DrawUiLabel(L"H" + std::to_wstring(row.level), uiBadge_.Get(), badge,
                    badgeColor, UiTextAlign::Center, UiTextVAlign::Middle);
        DrawUiLabel(row.label,
                    row.level == 1 ? uiBodyBold_.Get() : uiBody_.Get(),
                    MakeRect(badge.right + 8.0f, rowRect.top,
                             rowRect.right - 6.0f, rowRect.bottom),
                    row.level == 1 ? palette::kInk : palette::kTextMuted,
                    UiTextAlign::Left, UiTextVAlign::Middle);
    }
    renderTarget_->PopAxisAlignedClip();
}

void App::DrawPreview() {
    if (!renderTarget_ || !previewVisible_) {
        return;
    }
    FillRect(ui_.preview, 0xF7F2E8);
    StrokeLine(ui_.preview.left + 0.5f, ui_.preview.top,
               ui_.preview.left + 0.5f, ui_.preview.bottom, palette::kLine, 1.0f);
    FillRect(ui_.previewHeader, 0xF7F2E8);
    StrokeLine(ui_.previewHeader.left + 16.0f, ui_.previewHeader.bottom - 0.5f,
               ui_.previewHeader.right - 16.0f, ui_.previewHeader.bottom - 0.5f,
               palette::kLine, 1.0f);

    for (const ButtonSpec& button : ui_.previewButtons) {
        if (button.action == ActionId::PreviewSource) {
            const bool active = previewTab_ == 0;
            if (active) {
                FillRounded(button.rect, 7.0f, palette::kPage);
                StrokeRounded(button.rect, 7.0f, palette::kLine, 1.0f, 0.9f);
            }
            DrawUiLabel(L"源码", uiButton_.Get(), button.rect,
                        active ? palette::kInk : palette::kTextMuted,
                        UiTextAlign::Center, UiTextVAlign::Middle);
            if (active) {
                FillRounded(MakeRect(button.rect.left + 16.0f,
                                     button.rect.bottom - 4.0f,
                                     button.rect.right - 16.0f,
                                     button.rect.bottom - 1.0f),
                            1.5f, palette::kAccent);
            }
        } else if (button.action == ActionId::PreviewOutline) {
            const bool active = previewTab_ == 1;
            if (active) {
                FillRounded(button.rect, 7.0f, palette::kPage);
                StrokeRounded(button.rect, 7.0f, palette::kLine, 1.0f, 0.9f);
            }
            DrawUiLabel(L"大纲", uiButton_.Get(), button.rect,
                        active ? palette::kInk : palette::kTextMuted,
                        UiTextAlign::Center, UiTextVAlign::Middle);
            if (active) {
                FillRounded(MakeRect(button.rect.left + 16.0f,
                                     button.rect.bottom - 4.0f,
                                     button.rect.right - 16.0f,
                                     button.rect.bottom - 1.0f),
                            1.5f, palette::kAccent);
            }
        } else if (button.action == ActionId::CopySource) {
            const bool enabled = previewTab_ == 0;
            const bool hovered = hoveredButton_ == ActionId::CopySource;
            if (hovered && enabled) {
                FillRounded(button.rect, 6.0f, palette::kPage);
            }
            DrawUiLabel(L"复制", uiSmall_.Get(), button.rect,
                        enabled ? (hovered ? palette::kAccent : palette::kTextMuted)
                                : palette::kLineStrong,
                        UiTextAlign::Center, UiTextVAlign::Middle);
        }
    }

    if (previewTab_ == 0) {
        DrawSourceView();
        const RectF hintRect = MakeRect(ui_.preview.right - 200.0f,
                                        ui_.preview.top + 18.0f,
                                        ui_.preview.right - 76.0f,
                                        ui_.preview.top + 38.0f);
        DrawUiLabel(sourceFocused_ ? L"源码编辑中" : L"点击源码可编辑",
                    uiTiny_.Get(), hintRect,
                    sourceFocused_ ? palette::kAccent : palette::kTextMuted,
                    UiTextAlign::Right, UiTextVAlign::Middle, false, 0.9f);
    } else {
        DrawOutlineView();
    }
    if (sourceFocused_ && previewTab_ == 0) {
        StrokeRounded(ui_.previewBody.Inflated(1.0f), 4.0f, palette::kAccent, 1.0f,
                      0.24f);
    }

    const float previewRange = PreviewScrollRange();
    if (previewRange > 0.0f) {
        const RectF& track = ui_.previewScrollTrack;
        float total = sourceTotalHeight_;
        if (previewTab_ != 0) {
            const std::vector<OutlineRow> rows = BuildOutlineRows();
            total = rows.empty() ? 1.0f
                                 : rows.back().y + rows.back().height + previewScroll_ + 20.0f;
        }
        const float thumbHeight =
            std::max(26.0f, track.Height() * ui_.previewBody.Height() /
                                std::max(1.0f, total));
        const float travel = track.Height() - thumbHeight;
        const float thumbY =
            track.top + travel * (previewScroll_ / previewRange);
        FillRounded(MakeRect(track.CenterX() - 2.5f, track.top,
                             track.CenterX() + 2.5f, track.bottom),
                    3.0f, palette::kInkMuted, 0.12f);
        FillRounded(MakeRect(track.CenterX() - 3.0f, thumbY,
                             track.CenterX() + 3.0f, thumbY + thumbHeight),
                    3.0f, palette::kInkMuted, 0.42f);
    }
}

void App::DrawStatusBar() {
    if (!renderTarget_) {
        return;
    }
    FillRect(ui_.status, 0xFBF8F1);
    StrokeLine(ui_.status.left, ui_.status.top + 0.5f, ui_.status.right,
               ui_.status.top + 0.5f, palette::kLine, 1.0f);
    const int characters = mdge::CountCharacters(document_);
    const int words = mdge::CountWords(document_);
    std::wstring left = L"块 " + std::to_wstring(document_.blocks.size());
    left += L"  /  字符 " + std::to_wstring(characters);
    left += L"  /  词 " + std::to_wstring(words);
    if (const mdge::Block* block = ActiveBlock()) {
        left += L"  /  ";
        left += mdge::BlockTypeName(block->type);
    }
    if (HasSelection()) {
        left += L"  /  已选 ";
        left += std::to_wstring(SelectionEnd() - SelectionStart());
        left += L" 字符";
    }
    DrawUiLabel(left, uiSmall_.Get(),
                MakeRect(22.0f, ui_.status.top, clientWidth_ * 0.62f,
                         ui_.status.bottom),
                palette::kTextMuted, UiTextAlign::Left, UiTextVAlign::Middle);

    std::wstring right;
    if (!projectPath_.empty()) {
        right = dirty_ ? L"未保存" : L"已保存";
    } else if (!markdownPath_.empty()) {
        right = dirty_ ? L"未保存" : L"已导入";
    } else {
        right = L"未保存";
    }
    right += L"  /  MDGE 1.0  /  左至右 8 ms";
    DrawUiLabel(right, uiTiny_.Get(),
                MakeRect(clientWidth_ * 0.55f, ui_.status.top, clientWidth_ - 22.0f,
                         ui_.status.bottom),
                palette::kTextMuted, UiTextAlign::Right, UiTextVAlign::Middle);
    // Save state is communicated by the text label; no extra status dot.
}

void App::DrawDragOverlay() {
    if (!renderTarget_ || !drag_.moved ||
        (drag_.kind != DragState::Kind::Block &&
         drag_.kind != DragState::Kind::Palette)) {
        return;
    }
    if (drag_.overCanvas && drag_.insertIndex >= 0 && page_.valid) {
        renderTarget_->PushAxisAlignedClip(ToD2D(ui_.canvas),
                                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float localY = 0.0f;
        if (drag_.insertIndex < static_cast<int>(page_.blocks.size())) {
            localY = page_.blocks[static_cast<std::size_t>(drag_.insertIndex)].card.top -
                     4.0f;
        } else if (!page_.blocks.empty()) {
            localY = page_.blocks.back().card.bottom + 8.0f;
        } else {
            localY = 46.0f;
        }
        const float y = PageTop() + localY;
        const float x1 = page_.pageLeft + page_.contentLeft - 20.0f;
        const float x2 = page_.pageLeft + page_.contentLeft + page_.contentWidth +
                         20.0f;
        FillRounded(MakeRect(x1, y - 1.5f, x2, y + 1.5f), 1.5f,
                    palette::kAccent, 0.92f);
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x1, y), 3.0f, 3.0f),
                                   Brush(palette::kAccent));
        renderTarget_->PopAxisAlignedClip();
    }

    mdge::BlockType type = drag_.paletteType;
    std::wstring detail;
    if (drag_.kind == DragState::Kind::Block && drag_.sourceIndex >= 0 &&
        static_cast<std::size_t>(drag_.sourceIndex) < document_.blocks.size()) {
        const mdge::Block& block =
            document_.blocks[static_cast<std::size_t>(drag_.sourceIndex)];
        type = block.type;
        if (!block.text.empty()) {
            detail = mdge::MakeExcerpt(block.text, 28);
        } else if (block.type == mdge::BlockType::Image) {
            detail = mdge::FileName(block.imagePath);
        } else {
            detail = mdge::BlockTypeHint(block.type);
        }
    } else {
        detail = mdge::BlockTypeHint(drag_.paletteType);
    }

    const float ghostWidth = 252.0f;
    const float ghostHeight = 62.0f;
    float x = mouseX_ + 18.0f;
    float y = mouseY_ + 16.0f;
    x = std::min(x, clientWidth_ - ghostWidth - 12.0f);
    y = std::min(y, clientHeight_ - ghostHeight - 40.0f);
    const RectF ghost = MakeRectXYWH(x, y, ghostWidth, ghostHeight);
    FillRounded(ghost.Offset(0.0f, 4.0f), 10.0f, 0x8C8476, 0.14f);
    FillRounded(ghost, 10.0f, 0x1C2822, 0.97f);
    StrokeRounded(ghost, 10.0f, 0x3A4B40, 1.0f, 0.9f);
    const RectF badge = MakeRect(ghost.left + 12.0f, ghost.top + 14.0f,
                                 ghost.left + 52.0f, ghost.top + 36.0f);
    FillRounded(badge, 5.0f, 0x2A3E33);
    DrawUiLabel(mdge::BlockTypeShortName(type), uiBadge_.Get(), badge,
                palette::kAccentSoft, UiTextAlign::Center, UiTextVAlign::Middle);
    DrawUiLabel(mdge::BlockTypeName(type), uiButtonBold_.Get(),
                MakeRect(badge.right + 10.0f, ghost.top + 10.0f,
                         ghost.right - 12.0f, ghost.top + 30.0f),
                palette::kSidebarText, UiTextAlign::Left, UiTextVAlign::Middle);
    DrawUiLabel(detail, uiTiny_.Get(),
                MakeRect(badge.right + 10.0f, ghost.top + 33.0f,
                         ghost.right - 12.0f, ghost.bottom - 8.0f),
                palette::kSidebarMuted, UiTextAlign::Left, UiTextVAlign::Middle);
}

void App::DrawToast() {
    if (!renderTarget_ || toastText_.empty() ||
        std::chrono::steady_clock::now() >= toastExpiry_) {
        return;
    }
    const float width = std::min(
        clientWidth_ - 80.0f,
        std::max(220.0f, MeasureTextWidth(writeFactory_.Get(), toastText_,
                                          uiBodyBold_.Get()) +
                             40.0f));
    const float height = 38.0f;
    const RectF rect = MakeRect(clientWidth_ * 0.5f - width * 0.5f,
                                ui_.status.top - height - 18.0f,
                                clientWidth_ * 0.5f + width * 0.5f,
                                ui_.status.top - 18.0f);
    FillRounded(rect.Offset(0.0f, 4.0f), 9.0f, 0x7F7A6D, 0.12f);
    FillRounded(rect, 9.0f, toastError_ ? palette::kDanger : palette::kInk, 0.97f);
    DrawUiLabel(toastText_, uiBodyBold_.Get(), rect.Inset(14.0f),
                toastError_ ? 0xFFF5F1 : 0xF7F3E9, UiTextAlign::Center,
                UiTextVAlign::Middle, false, 1.0f);
}

}  // namespace mdgeui
