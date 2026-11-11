#include "app.h"

#include "file_associations.h"

#include "core/markdown.h"
#include "core/path_utils.h"
#include "core/project_io.h"
#include "dialogs.h"
#include "resource.h"

#include <d2d1helper.h>
#include <dwmapi.h>
#include <imm.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace mdgeui {
namespace {

constexpr wchar_t kWindowTitleBase[] = L"MDGE Studio";
constexpr float kHeaderHeight = 72.0f;
constexpr float kSidebarWidth = 252.0f;
constexpr float kPreviewWidth = 360.0f;
constexpr float kFormatBarHeight = 48.0f;
constexpr float kStatusHeight = 30.0f;
constexpr float kPageTopInset = 24.0f;
constexpr float kTextRevealMilliseconds = 8.0f;
constexpr UINT kCaretTimer = 1;
constexpr UINT kToastTimer = 2;
constexpr int kRoundRadius = 10;

float NowMilliseconds(double frequency) {
    LARGE_INTEGER count{};
    QueryPerformanceCounter(&count);
    if (frequency <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>((static_cast<double>(count.QuadPart) * 1000.0) /
                              frequency);
}

std::wstring FormatNumber(std::size_t value) {
    return std::to_wstring(value);
}

bool HasShift(WPARAM keys) {
    return (keys & MK_SHIFT) != 0;
}

bool HasControl(WPARAM keys) {
    return (keys & MK_CONTROL) != 0;
}

D2D1_COLOR_F ColorWithAlpha(const D2D1_COLOR_F& color, float alpha) {
    return D2D1::ColorF(color.r, color.g, color.b, alpha);
}

void AddButton(std::vector<ButtonSpec>& buttons, ActionId action, float x,
               float y, float width, float height) {
    buttons.push_back(ButtonSpec{action, MakeRectXYWH(x, y, width, height)});
}

ComPtr<IDWriteTextLayout> CreateBlockTextLayout(
    IDWriteFactory* factory, IDWriteTextFormat* format, const mdge::Block& block,
    float width) {
    ComPtr<IDWriteTextLayout> layout;
    const std::wstring layoutText = block.text.empty() ? L" " : block.text;
    if (FAILED(factory->CreateTextLayout(
            layoutText.c_str(), static_cast<UINT32>(layoutText.size()), format,
            width, 100000.0f, layout.GetAddressOf()))) {
        return {};
    }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    const FLOAT baseSize = format->GetFontSize();
    // styles 与 text 的长度不保证始终一致：Markdown 解析出的代码块会带着空
    // styles 进入文档，模型层的纯文本操作也不维护它。这里按缺失即无样式处理，
    // 避免用 text 的下标直接索引 styles 造成越界读取。
    auto styleAt = [&block](std::size_t at) -> std::uint8_t {
        return at < block.styles.size() ? block.styles[at] : 0;
    };
    std::size_t index = 0;
    while (index < block.text.size()) {
        const std::uint8_t flags = styleAt(index);
        const std::uint8_t visibleFlags =
            static_cast<std::uint8_t>(flags & (mdge::StyleBold | mdge::StyleItalic |
                                               mdge::StyleCode | mdge::StyleLink));
        if (visibleFlags == 0) {
            ++index;
            continue;
        }
        std::wstring href;
        if ((flags & mdge::StyleLink) != 0) {
            href = mdge::LinkAt(block, index);
        }
        std::size_t end = index + 1;
        while (end < block.text.size()) {
            std::uint8_t nextFlags = static_cast<std::uint8_t>(
                styleAt(end) &
                (mdge::StyleBold | mdge::StyleItalic | mdge::StyleCode |
                 mdge::StyleLink));
            if ((nextFlags & mdge::StyleLink) != 0 &&
                mdge::LinkAt(block, end).empty()) {
                nextFlags = static_cast<std::uint8_t>(nextFlags & ~mdge::StyleLink);
            }
            if (nextFlags != visibleFlags) {
                break;
            }
            if ((flags & mdge::StyleLink) != 0 &&
                mdge::LinkAt(block, end) != href) {
                break;
            }
            ++end;
        }
        DWRITE_TEXT_RANGE range{static_cast<UINT32>(index),
                                static_cast<UINT32>(end - index)};
        if ((visibleFlags & mdge::StyleBold) != 0) {
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        }
        if ((visibleFlags & mdge::StyleItalic) != 0) {
            layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        }
        if ((visibleFlags & mdge::StyleCode) != 0) {
            layout->SetFontFamilyName(L"Consolas", range);
            layout->SetFontSize(baseSize * 0.88f, range);
        }
        if ((visibleFlags & mdge::StyleLink) != 0 && !href.empty()) {
            layout->SetUnderline(TRUE, range);
        }
        index = end;
    }
    return layout;
}

float GapBefore(mdge::BlockType type, mdge::BlockType previous, bool hasPrevious) {
    if (!hasPrevious) {
        return 0.0f;
    }
    if ((type == mdge::BlockType::Bullet && previous == mdge::BlockType::Bullet) ||
        (type == mdge::BlockType::Numbered &&
         previous == mdge::BlockType::Numbered) ||
        (mdge::IsTask(type) && mdge::IsTask(previous))) {
        return 4.0f;
    }
    switch (type) {
        case mdge::BlockType::Heading1:
            return 30.0f;
        case mdge::BlockType::Heading2:
            return 24.0f;
        case mdge::BlockType::Heading3:
            return 18.0f;
        case mdge::BlockType::Quote:
            return 16.0f;
        case mdge::BlockType::Code:
            return 16.0f;
        case mdge::BlockType::Image:
            return 20.0f;
        case mdge::BlockType::Divider:
            return 18.0f;
        default:
            return 10.0f;
    }
}

}  // namespace

App::App() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    qpcFrequency_ = static_cast<double>(frequency.QuadPart);
    if (qpcFrequency_ <= 0.0) {
        qpcFrequency_ = 1.0;
    }
    selection_.block = 0;
    selection_.caret = 0;
    selection_.anchor = 0;
}

App::~App() {
    if (window_) {
        KillTimer(window_, kCaretTimer);
        KillTimer(window_, kToastTimer);
    }
    ReleaseProjectWindowIcon();
    DiscardRenderTarget();
    bodyFormat_.Reset();
    h1Format_.Reset();
    h2Format_.Reset();
    h3Format_.Reset();
    quoteFormat_.Reset();
    codeFormat_.Reset();
    sourceFormat_.Reset();
    uiTiny_.Reset();
    uiSmall_.Reset();
    uiBody_.Reset();
    uiBodyBold_.Reset();
    uiButton_.Reset();
    uiButtonBold_.Reset();
    uiDocTitle_.Reset();
    uiBrand_.Reset();
    uiBadge_.Reset();
    uiMono_.Reset();
    brushes_.clear();
    imageCache_.clear();
    roundStroke_.Reset();
    writeFactory_.Reset();
    d2dFactory_.Reset();
    if (instance_) {
        UnregisterClassW(kMainWindowClassName, instance_);
    }
}

bool App::CreateDeviceIndependentResources() {
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       d2dFactory_.GetAddressOf());
    if (FAILED(result)) {
        MessageBoxW(nullptr, L"Direct2D 初始化失败。", L"MDGE Studio",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
    if (FAILED(result)) {
        MessageBoxW(nullptr, L"DirectWrite 初始化失败。", L"MDGE Studio",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    D2D1_STROKE_STYLE_PROPERTIES strokeProperties = D2D1::StrokeStyleProperties();
    strokeProperties.startCap = D2D1_CAP_STYLE_ROUND;
    strokeProperties.endCap = D2D1_CAP_STYLE_ROUND;
    strokeProperties.dashCap = D2D1_CAP_STYLE_ROUND;
    strokeProperties.lineJoin = D2D1_LINE_JOIN_ROUND;
    d2dFactory_->CreateStrokeStyle(strokeProperties, nullptr, 0,
                                   roundStroke_.GetAddressOf());

    std::wstring imageError;
    if (!imageLoader_.Initialize(imageError)) {
        // The text editor still works without WIC; images will show an error
        // placeholder. Report it once through a message box.
        MessageBoxW(nullptr, imageError.c_str(), L"图像组件", MB_OK | MB_ICONWARNING);
    }

    CreateTextFormats();
    return true;
}

void App::CreateTextFormats() {
    IDWriteFactory* factory = writeFactory_.Get();
    const wchar_t* family = L"Microsoft YaHei UI";
    const wchar_t* mono = L"Consolas";
    const DWRITE_TEXT_ALIGNMENT leading = DWRITE_TEXT_ALIGNMENT_LEADING;

    uiTiny_ = CreateTextFormat(factory, family, 9.0f, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, leading,
                               DWRITE_WORD_WRAPPING_NO_WRAP);
    uiSmall_ = CreateTextFormat(factory, family, 10.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_FONT_STYLE_NORMAL, leading,
                                DWRITE_WORD_WRAPPING_NO_WRAP);
    uiBody_ = CreateTextFormat(factory, family, 12.0f, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, leading,
                               DWRITE_WORD_WRAPPING_WRAP);
    uiBodyBold_ = CreateTextFormat(factory, family, 12.0f,
                                   DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                   DWRITE_FONT_STYLE_NORMAL, leading,
                                   DWRITE_WORD_WRAPPING_WRAP);
    uiButton_ = CreateTextFormat(factory, family, 12.0f, DWRITE_FONT_WEIGHT_MEDIUM,
                                 DWRITE_FONT_STYLE_NORMAL, leading,
                                 DWRITE_WORD_WRAPPING_NO_WRAP);
    uiButtonBold_ = CreateTextFormat(factory, family, 12.0f,
                                     DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, leading,
                                     DWRITE_WORD_WRAPPING_NO_WRAP);
    uiDocTitle_ = CreateTextFormat(factory, family, 16.0f,
                                   DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                   DWRITE_FONT_STYLE_NORMAL, leading,
                                   DWRITE_WORD_WRAPPING_NO_WRAP);
    uiBrand_ = CreateTextFormat(factory, family, 18.0f, DWRITE_FONT_WEIGHT_BOLD,
                                DWRITE_FONT_STYLE_NORMAL, leading,
                                DWRITE_WORD_WRAPPING_NO_WRAP);
    uiBadge_ = CreateTextFormat(factory, family, 8.5f,
                                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_FONT_STYLE_NORMAL, leading,
                                DWRITE_WORD_WRAPPING_NO_WRAP);
    uiMono_ = CreateTextFormat(factory, mono, 10.0f, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL, leading,
                               DWRITE_WORD_WRAPPING_NO_WRAP);

    bodyFormat_ = CreateTextFormat(factory, family, 16.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_FONT_STYLE_NORMAL, leading,
                                   DWRITE_WORD_WRAPPING_WRAP);
    h1Format_ = CreateTextFormat(factory, family, 31.0f, DWRITE_FONT_WEIGHT_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, leading,
                                 DWRITE_WORD_WRAPPING_WRAP);
    h2Format_ = CreateTextFormat(factory, family, 23.0f, DWRITE_FONT_WEIGHT_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, leading,
                                 DWRITE_WORD_WRAPPING_WRAP);
    h3Format_ = CreateTextFormat(factory, family, 18.5f,
                                 DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                 DWRITE_FONT_STYLE_NORMAL, leading,
                                 DWRITE_WORD_WRAPPING_WRAP);
    quoteFormat_ = CreateTextFormat(factory, family, 17.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                    DWRITE_FONT_STYLE_NORMAL, leading,
                                    DWRITE_WORD_WRAPPING_WRAP);
    codeFormat_ = CreateTextFormat(factory, mono, 13.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_FONT_STYLE_NORMAL, leading,
                                   DWRITE_WORD_WRAPPING_WRAP);
    sourceFormat_ = CreateTextFormat(factory, mono, 11.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, leading,
                                     DWRITE_WORD_WRAPPING_WRAP);

    auto setSpacing = [](ComPtr<IDWriteTextFormat>& format, float lineSpacing,
                         float baseline) {
        if (format) {
            format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lineSpacing,
                                   baseline);
        }
    };
    setSpacing(bodyFormat_, 27.0f, 21.0f);
    setSpacing(h1Format_, 41.0f, 32.0f);
    setSpacing(h2Format_, 32.0f, 25.0f);
    setSpacing(h3Format_, 27.0f, 21.0f);
    setSpacing(quoteFormat_, 28.0f, 22.0f);
    setSpacing(codeFormat_, 21.0f, 16.0f);
    setSpacing(sourceFormat_, 18.0f, 14.0f);
    setSpacing(uiBody_, 18.0f, 14.0f);
    setSpacing(uiBodyBold_, 18.0f, 14.0f);
}

bool App::Create(HINSTANCE instance, const std::wstring& initialProjectPath) {
    instance_ = instance;
    if (!CreateDeviceIndependentResources()) {
        return false;
    }
    InitializeBlankDocument();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = App::WindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance_;
    windowClass.hIcon = reinterpret_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32,
                   LR_DEFAULTCOLOR | LR_SHARED));
    windowClass.hIconSm = reinterpret_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16,
                   LR_DEFAULTCOLOR | LR_SHARED));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszMenuName = nullptr;
    windowClass.lpszClassName = kMainWindowClassName;
    RegisterClassExW(&windowClass);

    UINT systemDpi = GetDpiForSystem();
    const float scale = static_cast<float>(systemDpi) / 96.0f;
    const int desiredClientWidth = static_cast<int>(1400.0f * scale);
    const int desiredClientHeight = static_cast<int>(860.0f * scale);
    RECT windowRect{0, 0, desiredClientWidth, desiredClientHeight};
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFO);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY),
                    &monitorInfo);
    const int monitorWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int monitorHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    const int maxWidth = static_cast<int>(monitorWidth * 0.94f);
    const int maxHeight = static_cast<int>(monitorHeight * 0.92f);
    windowWidth = std::min(windowWidth, maxWidth);
    windowHeight = std::min(windowHeight, maxHeight);
    const int x = monitorInfo.rcWork.left +
                  std::max(0, (monitorWidth - windowWidth) / 2);
    const int y = monitorInfo.rcWork.top +
                  std::max(0, (monitorHeight - windowHeight) / 2);

    window_ = CreateWindowExW(0, kMainWindowClassName, kWindowTitleBase,
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, windowWidth,
                              windowHeight, nullptr, nullptr, instance_, this);
    if (!window_) {
        MessageBoxW(nullptr, L"无法创建 MDGE Studio 窗口。", L"MDGE Studio",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    RECT clientRect{};
    GetClientRect(window_, &clientRect);
    dpi_ = static_cast<float>(GetDpiForWindow(window_));
    if (dpi_ <= 0.0f) {
        dpi_ = 96.0f;
    }
    clientWidth_ = static_cast<float>(clientRect.right - clientRect.left) * 96.0f / dpi_;
    clientHeight_ =
        static_cast<float>(clientRect.bottom - clientRect.top) * 96.0f / dpi_;
    RecalculateUi();
    UpdateWindowTitle();
    UpdateWindowIconFromProject();
    SetTimer(window_, kCaretTimer, 530, nullptr);
    DragAcceptFiles(window_, TRUE);

    if (!initialProjectPath.empty()) {
        const bool isMarkdown =
            mdge::FileExtensionLower(initialProjectPath) == L".md";
        const bool opened = isMarkdown
                                ? OpenMarkdownFile(initialProjectPath, true, true)
                                : OpenProjectFile(initialProjectPath, true, true);
        if (!opened) {
            // The open routine already displayed a detailed error.
            InitializeBlankDocument();
        }
    }
    revealStartMs_ = NowMilliseconds(qpcFrequency_);
    revealActive_ = true;
    return true;
}

int App::RunMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK App::WindowProc(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<App*>(create->lpCreateParams);
        if (app) {
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(app));
        }
    }
    if (!app) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    const LRESULT result = app->HandleMessage(message, wparam, lparam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return result;
}

LRESULT App::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            return 0;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                RECT clientRect{};
                GetClientRect(window_, &clientRect);
                dpi_ = static_cast<float>(GetDpiForWindow(window_));
                if (dpi_ <= 0.0f) {
                    dpi_ = 96.0f;
                }
                clientWidth_ = static_cast<float>(clientRect.right - clientRect.left) *
                               96.0f / dpi_;
                clientHeight_ =
                    static_cast<float>(clientRect.bottom - clientRect.top) * 96.0f /
                    dpi_;
                RecalculateUi();
                InvalidatePage();
                ClampScrollPositions();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED: {
            dpi_ = static_cast<float>(HIWORD(wparam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            DiscardRenderTarget();
            RECT clientRect{};
            GetClientRect(window_, &clientRect);
            clientWidth_ = static_cast<float>(clientRect.right - clientRect.left) *
                           96.0f / dpi_;
            clientHeight_ =
                static_cast<float>(clientRect.bottom - clientRect.top) * 96.0f / dpi_;
            RecalculateUi();
            InvalidatePage();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            OnPaint();
            return 0;
        case WM_MOUSEMOVE: {
            if (!trackingMouseLeave_) {
                TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_,
                                      0};
                TrackMouseEvent(&track);
                trackingMouseLeave_ = true;
            }
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            OnMouseMove(static_cast<float>(point.x) * 96.0f / dpi_,
                        static_cast<float>(point.y) * 96.0f / dpi_, wparam);
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouseLeave_ = false;
            OnMouseLeave();
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(window_);
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            OnLeftButtonDown(static_cast<float>(point.x) * 96.0f / dpi_,
                             static_cast<float>(point.y) * 96.0f / dpi_, wparam);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            OnLeftButtonUp(static_cast<float>(point.x) * 96.0f / dpi_,
                           static_cast<float>(point.y) * 96.0f / dpi_, wparam);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            OnLeftButtonDoubleClick(static_cast<float>(point.x) * 96.0f / dpi_,
                                    static_cast<float>(point.y) * 96.0f / dpi_,
                                    wparam);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window_, &point);
            OnMouseWheel(static_cast<float>(point.x) * 96.0f / dpi_,
                         static_cast<float>(point.y) * 96.0f / dpi_,
                         GET_WHEEL_DELTA_WPARAM(wparam));
            return 0;
        }
        case WM_KEYDOWN:
            OnKeyDown(wparam, lparam);
            return 0;
        case WM_CHAR:
            OnChar(static_cast<wchar_t>(wparam));
            return 0;
        case WM_IME_STARTCOMPOSITION:
            UpdateImeCompositionWindow();
            return 0;
        case WM_IME_COMPOSITION:
            OnImeComposition(wparam, lparam);
            return 0;
        case WM_IME_ENDCOMPOSITION:
            compositionText_.clear();
            return 0;
        case WM_IME_CHAR:
            // Committed IME text is inserted through WM_IME_COMPOSITION
            // (GCS_RESULTSTR); ignoring the duplicate avoids double input.
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                SetCursorForHover();
                return TRUE;
            }
            break;
        case WM_SETFOCUS:
            hasFocus_ = true;
            caretVisible_ = true;
            ScheduleCaretBlink();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            hasFocus_ = false;
            compositionText_.clear();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const UINT windowDpi =
                window_ ? GetDpiForWindow(window_) : GetDpiForSystem();
            const float scale = static_cast<float>(windowDpi) / 96.0f;
            info->ptMinTrackSize.x = static_cast<LONG>(1160.0f * scale);
            info->ptMinTrackSize.y = static_cast<LONG>(700.0f * scale);
            return 0;
        }
        case WM_TIMER:
            if (wparam == kCaretTimer) {
                if (hasFocus_ && (sourceFocused_ || ActiveBlockIsText())) {
                    caretBlinkOn_ = !caretBlinkOn_;
                    InvalidateRect(window_, nullptr, FALSE);
                }
                return 0;
            }
            if (wparam == kToastTimer) {
                toastText_.clear();
                InvalidateRect(window_, nullptr, FALSE);
                KillTimer(window_, kToastTimer);
                return 0;
            }
            return 0;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wparam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            POINT point{};
            DragQueryPoint(drop, &point);
            std::vector<std::wstring> files;
            files.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(length);
                files.push_back(std::move(path));
            }
            DragFinish(drop);
            InsertDroppedFiles(files, static_cast<float>(point.x) * 96.0f / dpi_,
                               static_cast<float>(point.y) * 96.0f / dpi_);
            SetForegroundWindow(window_);
            return 0;
        }
        case WM_COPY:
            if (sourceFocused_) {
                SourceCopy(false);
            } else {
                CopySelectionToClipboard(false);
            }
            return 0;
        case WM_CUT:
            if (sourceFocused_) {
                SourceCopy(true);
            } else {
                CopySelectionToClipboard(true);
            }
            return 0;
        case WM_PASTE:
            if (sourceFocused_) {
                SourcePaste();
            } else {
                PasteFromClipboard();
            }
            return 0;
        case WM_COPYDATA: {
            auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
            if (data && data->lpData &&
                data->cbData >= sizeof(DWORD) + sizeof(wchar_t)) {
                DWORD magic = 0;
                std::memcpy(&magic, data->lpData, sizeof(magic));
                if (magic == kOpenFileCopyDataMagic) {
                    const auto* text = reinterpret_cast<const wchar_t*>(
                        static_cast<const unsigned char*>(data->lpData) +
                        sizeof(DWORD));
                    const std::size_t chars =
                        (data->cbData - sizeof(DWORD)) / sizeof(wchar_t);
                    std::wstring path(text, chars);
                    while (!path.empty() && path.back() == L'\0') {
                        path.pop_back();
                    }
                    if (!path.empty()) {
                        pendingExternalPath_ = std::move(path);
                        PostMessageW(window_, WM_APP + 1, 0, 0);
                    }
                }
            }
            return TRUE;
        }
        case WM_APP + 1: {
            std::wstring path = std::move(pendingExternalPath_);
            pendingExternalPath_.clear();
            if (!path.empty()) {
                OpenExternalPath(path);
            }
            ShowWindow(window_, SW_RESTORE);
            SetForegroundWindow(window_);
            SetFocus(window_);
            return 0;
        }
        case WM_CLOSE:
            if (MaybeSaveBeforeDestructiveAction()) {
                DestroyWindow(window_);
            }
            return 0;
        case WM_DESTROY:
            window_ = nullptr;
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            return DefWindowProcW(window_, message, wparam, lparam);
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

void App::OpenExternalPath(const std::wstring& path) {
    if (path.empty()) {
        return;
    }
    if (!MaybeSaveBeforeDestructiveAction()) {
        return;
    }
    const std::wstring extension = mdge::FileExtensionLower(path);
    bool opened = false;
    if (extension == L".mdge") {
        opened = OpenProjectFile(path, true, true);
        if (opened) {
            ShowToast(L"项目已打开。");
        }
    } else if (extension == L".md") {
        opened = OpenMarkdownFile(path, true, true);
    } else {
        MessageBoxW(window_, L"只能打开 .mdge 项目或 .md 文档。", L"MDGE Studio",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (opened) {
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
        SetFocus(window_);
    }
}

void App::RepairFileAssociationsFromUi() {
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return;
    }
    LeaveSourceEditing();
    mdgeui::RepairFileAssociations(window_, std::wstring(path.data(), length));
    InvalidateRect(window_, nullptr, FALSE);
}

bool App::EnsureRenderTarget() {
    if (!window_) {
        return false;
    }
    RECT clientRect{};
    GetClientRect(window_, &clientRect);
    const UINT pixelWidth = static_cast<UINT>(std::max<LONG>(1, clientRect.right));
    const UINT pixelHeight = static_cast<UINT>(std::max<LONG>(1, clientRect.bottom));
    const float windowDpi = static_cast<float>(GetDpiForWindow(window_));
    dpi_ = windowDpi > 0.0f ? windowDpi : 96.0f;
    const float previousWidth = clientWidth_;
    const float previousHeight = clientHeight_;
    clientWidth_ = static_cast<float>(pixelWidth) * 96.0f / dpi_;
    clientHeight_ = static_cast<float>(pixelHeight) * 96.0f / dpi_;
    const bool sizeChanged =
        std::abs(previousWidth - clientWidth_) > 0.5f ||
        std::abs(previousHeight - clientHeight_) > 0.5f || ui_.client.Width() <= 0.0f;

    if (!renderTarget_) {
        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties();
        properties.dpiX = dpi_;
        properties.dpiY = dpi_;
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties =
            D2D1::HwndRenderTargetProperties(
                window_, D2D1::SizeU(pixelWidth, pixelHeight),
                D2D1_PRESENT_OPTIONS_NONE);
        HRESULT result = d2dFactory_->CreateHwndRenderTarget(
            properties, hwndProperties, renderTarget_.GetAddressOf());
        if (FAILED(result) || !renderTarget_) {
            return false;
        }
        renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ClearBrushCache();
        imageCache_.clear();
        deviceReady_ = true;
        InvalidatePage();
        InvalidateRect(window_, nullptr, FALSE);
    } else {
        renderTarget_->SetDpi(dpi_, dpi_);
        const D2D1_SIZE_U current = renderTarget_->GetPixelSize();
        if (current.width != pixelWidth || current.height != pixelHeight) {
            renderTarget_->Resize(D2D1::SizeU(pixelWidth, pixelHeight));
            InvalidateRect(window_, nullptr, FALSE);
        }
    }
    if (sizeChanged) {
        RecalculateUi();
        InvalidatePage();
        ClampScrollPositions();
    }
    return true;
}

void App::DiscardRenderTarget() {
    renderTarget_.Reset();
    brushes_.clear();
    imageCache_.clear();
    deviceReady_ = false;
    InvalidatePage();
}

void App::ClearBrushCache() {
    brushes_.clear();
}

void App::OnPaint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!EnsureRenderTarget()) {
        EndPaint(window_, &paint);
        return;
    }
    DrawScene();
    EndPaint(window_, &paint);
}

void App::RecalculateUi() {
    ui_ = UiGeometry{};
    ui_.client = MakeRect(0.0f, 0.0f, clientWidth_, clientHeight_);
    ui_.header = MakeRect(0.0f, 0.0f, clientWidth_, kHeaderHeight);
    ui_.brand = MakeRect(0.0f, 0.0f, kSidebarWidth, kHeaderHeight);
    ui_.headerContent =
        MakeRect(kSidebarWidth, 0.0f, clientWidth_, kHeaderHeight);
    ui_.sidebar = MakeRect(0.0f, kHeaderHeight, kSidebarWidth,
                           std::max(kHeaderHeight, clientHeight_ - kStatusHeight));
    const float previewWidth = previewVisible_ ? kPreviewWidth : 0.0f;
    const float canvasRight = std::max(kSidebarWidth + 240.0f,
                                       clientWidth_ - previewWidth);
    ui_.formatBar = MakeRect(kSidebarWidth, kHeaderHeight, canvasRight,
                             kHeaderHeight + kFormatBarHeight);
    ui_.canvas = MakeRect(kSidebarWidth, kHeaderHeight + kFormatBarHeight,
                          canvasRight, clientHeight_ - kStatusHeight);
    ui_.preview = previewVisible_
                      ? MakeRect(canvasRight, kHeaderHeight, clientWidth_,
                                 clientHeight_ - kStatusHeight)
                      : RectF{};
    ui_.status = MakeRect(0.0f, clientHeight_ - kStatusHeight, clientWidth_,
                          clientHeight_);

    // Header buttons.
    struct TopButtonSpec {
        ActionId action;
        float width;
    };
    const TopButtonSpec topButtons[] = {
        {ActionId::NewProject, 58.0f},  {ActionId::OpenProject, 58.0f},
        {ActionId::Undo, 58.0f},        {ActionId::Redo, 58.0f},
        {ActionId::TogglePreview, 88.0f},
        {ActionId::ExportMarkdown, 88.0f},
        {ActionId::SaveProject, 96.0f},
    };
    float buttonWidth = 0.0f;
    for (const TopButtonSpec& spec : topButtons) {
        buttonWidth += spec.width;
    }
    buttonWidth += 6.0f * 6.0f;
    const float buttonRight = clientWidth_ - 20.0f;
    const float buttonStart = std::max(kSidebarWidth + 24.0f,
                                       buttonRight - buttonWidth);
    float cursorX = buttonStart;
    const float buttonY = (kHeaderHeight - 34.0f) * 0.5f;
    for (const TopButtonSpec& spec : topButtons) {
        AddButton(ui_.headerButtons, spec.action, cursorX, buttonY, spec.width,
                  34.0f);
        cursorX += spec.width + 6.0f;
    }
    const float titleLeft = kSidebarWidth + 26.0f;
    const float titleRight = std::max(titleLeft + 80.0f, buttonStart - 22.0f);
    ui_.documentTitleRect = MakeRect(titleLeft, 16.0f, titleRight, 39.0f);
    ui_.documentSubtitleRect = MakeRect(titleLeft, 40.0f, titleRight, 57.0f);

    // Formatting toolbar.
    const float formatTop = ui_.formatBar.top + 9.0f;
    float formatX = ui_.formatBar.left + 24.0f;
    auto addFormat = [&](ActionId action, float width) {
        AddButton(ui_.formatButtons, action, formatX, formatTop, width, 30.0f);
        formatX += width + 4.0f;
    };
    addFormat(ActionId::StyleHeading1, 38.0f);
    addFormat(ActionId::StyleHeading2, 38.0f);
    addFormat(ActionId::StyleHeading3, 38.0f);
    addFormat(ActionId::StyleParagraph, 38.0f);
    formatX += 10.0f;
    addFormat(ActionId::InlineBold, 34.0f);
    addFormat(ActionId::InlineItalic, 34.0f);
    addFormat(ActionId::InlineCode, 52.0f);
    addFormat(ActionId::InlineLink, 52.0f);
    formatX += 10.0f;
    addFormat(ActionId::DeleteBlock, 64.0f);

    // Sidebar document card and palette.
    const float sidebarTop = ui_.sidebar.top;
    const float sidebarBottom = ui_.sidebar.bottom;
    ui_.docCard = MakeRect(16.0f, sidebarTop + 18.0f, kSidebarWidth - 16.0f,
                           sidebarTop + 18.0f + 100.0f);
    ui_.docIconRect = MakeRect(ui_.docCard.left + 14.0f, ui_.docCard.top + 14.0f,
                               ui_.docCard.left + 58.0f,
                               ui_.docCard.top + 58.0f);
    ui_.docTitleRect = MakeRect(ui_.docCard.left + 70.0f,
                                ui_.docCard.top + 14.0f,
                                ui_.docCard.right - 12.0f,
                                ui_.docCard.top + 50.0f);
    ui_.docMetaRect = MakeRect(ui_.docCard.left + 70.0f,
                               ui_.docCard.top + 51.0f,
                               ui_.docCard.right - 14.0f,
                               ui_.docCard.top + 67.0f);
    ui_.docHintRect = MakeRect(ui_.docCard.left + 14.0f,
                               ui_.docCard.top + 73.0f,
                               ui_.docCard.right - 14.0f,
                               ui_.docCard.bottom - 7.0f);

    ui_.paletteLabelRect =
        MakeRect(16.0f, ui_.docCard.bottom + 20.0f, 142.0f,
                 ui_.docCard.bottom + 36.0f);
    ui_.paletteHintRect = MakeRect(142.0f, ui_.docCard.bottom + 21.0f,
                                   kSidebarWidth - 16.0f,
                                   ui_.docCard.bottom + 35.0f);

    const bool compact = (sidebarBottom - sidebarTop) < 630.0f;
    const float rowHeight = compact ? 34.0f : 40.0f;
    const float rowGap = compact ? 3.0f : 5.0f;
    const float paletteStartY = ui_.paletteLabelRect.bottom + 10.0f;
    const mdge::BlockType paletteTypes[] = {
        mdge::BlockType::Heading2, mdge::BlockType::Paragraph,
        mdge::BlockType::Bullet,   mdge::BlockType::Numbered,
        mdge::BlockType::TaskUnchecked,
        mdge::BlockType::Quote,    mdge::BlockType::Code,
        mdge::BlockType::Image,    mdge::BlockType::Divider,
    };
    float paletteY = paletteStartY;
    for (mdge::BlockType type : paletteTypes) {
        ui_.paletteItems.push_back(PaletteSpec{
            type,
            MakeRect(16.0f, paletteY, kSidebarWidth - 16.0f, paletteY + rowHeight)});
        paletteY += rowHeight + rowGap;
    }

    const float footerY = sidebarBottom - 26.0f;
    ui_.sidebarFooterRect =
        MakeRect(16.0f, footerY, kSidebarWidth - 16.0f, footerY + 16.0f);
    ui_.repairAssociationRect =
        MakeRect(kSidebarWidth - 84.0f, footerY, kSidebarWidth - 16.0f,
                 footerY + 16.0f);
    const bool showTip = (sidebarBottom - paletteY) > 118.0f;
    if (showTip) {
        ui_.sidebarTipRect = MakeRect(16.0f, paletteY + 14.0f, kSidebarWidth - 16.0f,
                                      paletteY + 100.0f);
    } else {
        ui_.sidebarTipRect = RectF{};
    }

    // Preview panel.
    if (previewVisible_) {
        ui_.previewHeader = MakeRect(ui_.preview.left, ui_.preview.top,
                                     ui_.preview.right, ui_.preview.top + 52.0f);
        ui_.previewBody = MakeRect(ui_.preview.left + 16.0f,
                                   ui_.preview.top + 60.0f,
                                   ui_.preview.right - 16.0f,
                                   ui_.preview.bottom - 12.0f);
        AddButton(ui_.previewButtons, ActionId::PreviewSource,
                  ui_.preview.left + 16.0f, ui_.preview.top + 13.0f, 62.0f, 30.0f);
        AddButton(ui_.previewButtons, ActionId::PreviewOutline,
                  ui_.preview.left + 82.0f, ui_.preview.top + 13.0f, 62.0f, 30.0f);
        AddButton(ui_.previewButtons, ActionId::CopySource,
                  ui_.preview.right - 66.0f, ui_.preview.top + 16.0f, 50.0f, 24.0f);
    }

    UpdateScrollbarRects();
    page_.valid = false;
}

void App::UpdateScrollbarRects() {
    if (ui_.canvas.Width() > 0.0f) {
        ui_.canvasScrollTrack = MakeRect(ui_.canvas.right - 12.0f,
                                         ui_.canvas.top + 12.0f,
                                         ui_.canvas.right - 5.0f,
                                         ui_.canvas.bottom - 12.0f);
    }
    if (previewVisible_ && ui_.previewBody.Width() > 0.0f) {
        ui_.previewScrollTrack = MakeRect(ui_.preview.right - 12.0f,
                                          ui_.previewBody.top,
                                          ui_.preview.right - 5.0f,
                                          ui_.previewBody.bottom);
    }
}

void App::EnsurePageLayout() {
    if (!ui_.canvas.Width() || !ui_.canvas.Height()) {
        return;
    }
    const float available = ui_.canvas.Width();
    const float pageWidth = std::min(820.0f, std::max(380.0f, available - 80.0f));
    if (page_.valid && std::abs(page_.builtCanvasWidth - available) < 0.5f &&
        std::abs(page_.pageWidth - pageWidth) < 0.5f) {
        return;
    }

    PageLayout next;
    next.builtCanvasWidth = available;
    next.pageWidth = pageWidth;
    next.pageLeft = ui_.canvas.left + (available - pageWidth) * 0.5f;
    const float pagePadding = pageWidth >= 650.0f ? 58.0f : 46.0f;
    next.contentLeft = pagePadding;
    next.contentWidth = pageWidth - pagePadding * 2.0f;
    if (next.contentWidth < 240.0f) {
        next.contentWidth = std::max(160.0f, pageWidth - pagePadding * 2.0f);
    }
    float y = 50.0f;
    int numbered = 0;
    mdge::BlockType previousType = mdge::BlockType::Paragraph;
    bool hasPrevious = false;

    for (std::size_t index = 0; index < document_.blocks.size(); ++index) {
        const mdge::Block& block = document_.blocks[index];
        y += GapBefore(block.type, previousType, hasPrevious);

        if (mdge::IsTextual(block.type)) {
            float indent = 0.0f;
            float topPad = 4.0f;
            float bottomPad = 4.0f;
            if (block.type == mdge::BlockType::Bullet ||
                block.type == mdge::BlockType::Numbered) {
                indent = 30.0f;
            } else if (mdge::IsTask(block.type)) {
                indent = 34.0f;
            } else if (block.type == mdge::BlockType::Quote) {
                indent = 24.0f;
                topPad = 10.0f;
                bottomPad = 10.0f;
            } else if (block.type == mdge::BlockType::Code) {
                indent = 18.0f;
                topPad = 16.0f;
                bottomPad = 16.0f;
            }
            const float textWidth =
                std::max(100.0f, next.contentWidth - indent - (block.type == mdge::BlockType::Code ? 18.0f : 0.0f));
            IDWriteTextFormat* format = bodyFormat_.Get();
            switch (block.type) {
                case mdge::BlockType::Heading1:
                    format = h1Format_.Get();
                    break;
                case mdge::BlockType::Heading2:
                    format = h2Format_.Get();
                    break;
                case mdge::BlockType::Heading3:
                    format = h3Format_.Get();
                    break;
                case mdge::BlockType::Quote:
                    format = quoteFormat_.Get();
                    break;
                case mdge::BlockType::Code:
                    format = codeFormat_.Get();
                    break;
                default:
                    format = bodyFormat_.Get();
                    break;
            }
            ComPtr<IDWriteTextLayout> layout =
                CreateBlockTextLayout(writeFactory_.Get(), format, block, textWidth);
            DWRITE_TEXT_METRICS metrics{};
            if (layout) {
                layout->GetMetrics(&metrics);
            }
            const float textHeight = std::max(24.0f, metrics.height);
            BlockVisual visual;
            visual.index = index;
            visual.type = block.type;
            visual.height = topPad + textHeight + bottomPad;
            visual.textRect = MakeRect(next.contentLeft + indent, y + topPad,
                                       next.contentLeft + indent + textWidth,
                                       y + topPad + textHeight);
            visual.card =
                MakeRect(next.contentLeft - 16.0f, y,
                         next.contentLeft + next.contentWidth + 16.0f,
                         y + visual.height);
            visual.gripRect = MakeRect(next.contentLeft - 40.0f, y + topPad + 1.0f,
                                       next.contentLeft - 20.0f,
                                       y + topPad + 19.0f);
            visual.textLayout = std::move(layout);
            visual.layoutText = block.text.empty() ? L" " : block.text;
            if (block.type == mdge::BlockType::Numbered) {
                numbered = (hasPrevious && previousType == mdge::BlockType::Numbered)
                               ? numbered + 1
                               : 1;
                visual.listNumber = numbered;
            } else {
                numbered = 0;
            }
            next.blocks.push_back(std::move(visual));
            y += topPad + textHeight + bottomPad;
        } else if (block.type == mdge::BlockType::Image) {
            BlockVisual visual;
            visual.index = index;
            visual.type = block.type;
            visual.resolvedImagePath = ResolveImagePath(block.imagePath);
            if (!visual.resolvedImagePath.empty() && renderTarget_) {
                visual.image = GetImageAsset(visual.resolvedImagePath);
            }
            float imageWidth = std::min(next.contentWidth, 640.0f);
            float imageHeight = 180.0f;
            if (visual.image && visual.image->loaded &&
                visual.image->bitmap && visual.image->height > 0) {
                const float ratio = static_cast<float>(visual.image->width) /
                                    static_cast<float>(visual.image->height);
                imageWidth = std::min(next.contentWidth, 640.0f);
                imageHeight = imageWidth / std::max(0.05f, ratio);
                if (imageHeight > 420.0f) {
                    imageHeight = 420.0f;
                    imageWidth = imageHeight * ratio;
                }
                imageWidth = std::max(80.0f, std::min(imageWidth, next.contentWidth));
            } else {
                imageWidth = next.contentWidth;
                imageHeight = 170.0f;
                visual.imageError = !visual.resolvedImagePath.empty() && visual.image
                                        ? visual.image->error
                                        : L"图像未找到或尚未载入。";
            }
            visual.imageRect = MakeRect(next.contentLeft, y, next.contentLeft + imageWidth,
                                        y + imageHeight);
            float captionHeight = 0.0f;
            if (!mdge::TrimText(block.caption).empty()) {
                captionHeight = 28.0f;
                visual.captionRect =
                    MakeRect(next.contentLeft + 2.0f, y + imageHeight + 5.0f,
                             next.contentLeft + next.contentWidth,
                             y + imageHeight + 25.0f);
            }
            visual.height = imageHeight + captionHeight + 6.0f;
            visual.card = MakeRect(next.contentLeft - 16.0f, y - 6.0f,
                                    next.contentLeft + next.contentWidth + 16.0f,
                                    y + visual.height + 2.0f);
            visual.gripRect = MakeRect(next.contentLeft - 40.0f, y + 2.0f,
                                       next.contentLeft - 20.0f, y + 22.0f);
            y += visual.height;
            next.blocks.push_back(std::move(visual));
        } else {  // Divider
            BlockVisual visual;
            visual.index = index;
            visual.type = block.type;
            visual.height = 24.0f;
            visual.card = MakeRect(next.contentLeft - 16.0f, y,
                                    next.contentLeft + next.contentWidth + 16.0f,
                                    y + visual.height);
            visual.gripRect = MakeRect(next.contentLeft - 40.0f, y + 3.0f,
                                       next.contentLeft - 20.0f, y + 21.0f);
            y += visual.height;
            next.blocks.push_back(std::move(visual));
        }

        previousType = block.type;
        hasPrevious = true;
    }

    next.contentHeight = std::max(120.0f, y + 64.0f);
    next.pageHeight = std::max(ui_.canvas.Height() - 48.0f, next.contentHeight);
    next.valid = true;
    page_ = std::move(next);
    const float range = EditorScrollRange();
    editorScroll_ = std::max(0.0f, std::min(editorScroll_, range));
}

float App::EditorScrollRange() const {
    if (!page_.valid || ui_.canvas.Height() <= 0.0f) {
        return 0.0f;
    }
    const float range = page_.pageHeight + 48.0f - ui_.canvas.Height();
    return std::max(0.0f, range);
}

float App::PreviewScrollRange() const {
    if (!previewVisible_ || ui_.previewBody.Height() <= 0.0f) {
        return 0.0f;
    }
    if (previewTab_ == 0) {
        return std::max(0.0f, sourceTotalHeight_ - ui_.previewBody.Height());
    }
    const std::vector<OutlineRow> rows = BuildOutlineRows();
    float total = 20.0f;
    if (!rows.empty()) {
        total = rows.back().y + rows.back().height + 20.0f;
    }
    return std::max(0.0f, total - ui_.previewBody.Height());
}

void App::ClampScrollPositions() {
    EnsurePageLayout();
    editorScroll_ = std::max(0.0f, std::min(editorScroll_, EditorScrollRange()));
    if (previewVisible_ && previewTab_ == 0) {
        BuildSourceLines();
    }
    previewScroll_ = std::max(0.0f, std::min(previewScroll_, PreviewScrollRange()));
}

void App::InvalidatePage() {
    page_.valid = false;
}

void App::InvalidateSourceLayout() {
    sourceLayoutDirty_ = true;
    sourceLayoutWidth_ = -1.0f;
}

void App::InvalidateEditorState() {
    if (sourceFocused_) {
        sourceFocused_ = false;
        sourceAnchor_ = sourceCaret_;
    }
    InvalidatePage();
    InvalidateSourceLayout();
    markdownDirty_ = true;
    InvalidateRect(window_, nullptr, FALSE);
}

std::wstring App::CurrentMarkdownSource() {
    if (markdownDirty_) {
        markdownSource_ = mdge::DocumentToMarkdown(document_);
        markdownDirty_ = false;
        InvalidateSourceLayout();
    }
    return markdownSource_;
}

void App::BuildSourceLines() {
    if (!previewVisible_ || ui_.previewBody.Width() <= 0.0f) {
        sourceLines_.clear();
        sourceTotalHeight_ = 0.0f;
        return;
    }
    CurrentMarkdownSource();
    const float textWidth =
        std::max(80.0f, ui_.previewBody.Width() - 46.0f);
    if (!sourceLayoutDirty_ && std::abs(sourceLayoutWidth_ - textWidth) < 0.5f) {
        return;
    }
    sourceLayoutDirty_ = false;
    sourceLayoutWidth_ = textWidth;
    sourceLines_.clear();
    float total = 0.0f;

    std::size_t lineStart = 0;
    while (lineStart <= markdownSource_.size()) {
        const std::size_t newline = markdownSource_.find(L'\n', lineStart);
        const std::size_t lineEnd =
            newline == std::wstring::npos ? markdownSource_.size() : newline;
        std::wstring line = markdownSource_.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        SourceLineView view;
        view.text = line;
        view.start = lineStart;
        view.length = line.size();
        view.blank = line.empty();
        view.offsetY = total;
        const std::wstring trimmed = mdge::TrimText(line);
        if (trimmed.rfind(L"```", 0) == 0) {
            view.colorKind = 2;
        } else if (trimmed.rfind(L"#", 0) == 0) {
            view.colorKind = 1;
        } else if (trimmed.rfind(L">", 0) == 0) {
            view.colorKind = 3;
        } else if (trimmed.rfind(L"- ", 0) == 0 || trimmed.rfind(L"* ", 0) == 0 ||
                   (!trimmed.empty() && iswdigit(trimmed[0]) &&
                    trimmed.find(L". ") != std::wstring::npos &&
                    trimmed.find(L". ") <= 3)) {
            view.colorKind = 4;
        } else if (trimmed.rfind(L"![", 0) == 0) {
            view.colorKind = 5;
        } else {
            view.colorKind = 0;
        }

        if (!view.blank) {
            const std::wstring layoutText = line;
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(writeFactory_->CreateTextLayout(
                    layoutText.c_str(), static_cast<UINT32>(layoutText.size()),
                    sourceFormat_.Get(), textWidth, 100000.0f,
                    layout.GetAddressOf()))) {
                layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                DWRITE_TEXT_METRICS metrics{};
                layout->GetMetrics(&metrics);
                view.height = std::max(18.0f, metrics.height + 2.0f);
                view.layout = std::move(layout);
            } else {
                view.height = 18.0f;
            }
        } else {
            view.height = 18.0f;
        }
        total += view.height;
        sourceLines_.push_back(std::move(view));
        if (newline == std::wstring::npos) {
            break;
        }
        lineStart = newline + 1;
        if (sourceLines_.size() > 8000) {
            break;
        }
    }
    sourceTotalHeight_ = total + 30.0f;
    if (previewTab_ == 0) {
        previewScroll_ =
            std::max(0.0f, std::min(previewScroll_, PreviewScrollRange()));
    }
}

std::vector<OutlineRow> App::BuildOutlineRows() const {
    std::vector<OutlineRow> rows;
    if (!previewVisible_ || ui_.previewBody.Width() <= 0.0f) {
        return rows;
    }
    const float rowHeight = 38.0f;
    float y = 16.0f - previewScroll_;
    for (std::size_t i = 0; i < document_.blocks.size(); ++i) {
        const mdge::Block& block = document_.blocks[i];
        int level = 0;
        if (block.type == mdge::BlockType::Heading1) {
            level = 1;
        } else if (block.type == mdge::BlockType::Heading2) {
            level = 2;
        } else if (block.type == mdge::BlockType::Heading3) {
            level = 3;
        }
        if (level == 0) {
            continue;
        }
        OutlineRow row;
        row.blockIndex = i;
        row.level = level;
        std::wstring label = mdge::TrimText(block.text);
        if (label.empty()) {
            label = L"空标题";
        }
        if (label.size() > 48) {
            label.resize(48);
            label += L"...";
        }
        row.label = label;
        row.y = y;
        row.height = rowHeight;
        y += rowHeight;
        rows.push_back(std::move(row));
    }
    return rows;
}

void App::DrawScene() {
    if (!renderTarget_) {
        return;
    }
    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->Clear(Color(palette::kPaper));

    DrawHeader();
    DrawSidebar();
    DrawFormatBar();
    DrawCanvas();
    DrawPage();
    DrawPreview();
    DrawStatusBar();
    DrawDragOverlay();
    DrawToast();

    const HRESULT result = renderTarget_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardRenderTarget();
        InvalidateRect(window_, nullptr, FALSE);
    }
}

ID2D1SolidColorBrush* App::Brush(std::uint32_t rgb, float alpha) {
    return Brush(ToColor(rgb, alpha));
}

ID2D1SolidColorBrush* App::Brush(const D2D1_COLOR_F& color) {
    if (!renderTarget_) {
        return nullptr;
    }
    const std::uint64_t alphaKey =
        static_cast<std::uint64_t>(std::clamp(static_cast<int>(color.a * 255.0f + 0.5f),
                                              0, 255));
    const std::uint64_t key =
        (static_cast<std::uint64_t>(color.r * 255.0f + 0.5f) << 40) |
        (static_cast<std::uint64_t>(color.g * 255.0f + 0.5f) << 32) |
        (static_cast<std::uint64_t>(color.b * 255.0f + 0.5f) << 24) | alphaKey;
    auto found = brushes_.find(key);
    if (found != brushes_.end() && found->second) {
        return found->second.Get();
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(renderTarget_->CreateSolidColorBrush(color, brush.GetAddressOf()))) {
        return nullptr;
    }
    ID2D1SolidColorBrush* raw = brush.Get();
    brushes_.emplace(key, std::move(brush));
    return raw;
}

D2D1_COLOR_F App::ToColor(std::uint32_t rgb, float alpha) const {
    return Color(rgb, alpha);
}

void App::FillRect(const RectF& rect, std::uint32_t rgb, float alpha) {
    if (!renderTarget_) {
        return;
    }
    ID2D1SolidColorBrush* brush = Brush(rgb, alpha);
    if (brush) {
        renderTarget_->FillRectangle(ToD2D(rect), brush);
    }
}

void App::FillRounded(const RectF& rect, float radius, std::uint32_t rgb,
                      float alpha) {
    if (!renderTarget_) {
        return;
    }
    ID2D1SolidColorBrush* brush = Brush(rgb, alpha);
    if (brush) {
        renderTarget_->FillRoundedRectangle(Rounded(rect, radius), brush);
    }
}

void App::StrokeRounded(const RectF& rect, float radius, std::uint32_t rgb,
                        float thickness, float alpha) {
    if (!renderTarget_) {
        return;
    }
    ID2D1SolidColorBrush* brush = Brush(rgb, alpha);
    if (brush) {
        renderTarget_->DrawRoundedRectangle(Rounded(rect, radius), brush, thickness);
    }
}

void App::StrokeLine(float x1, float y1, float x2, float y2, std::uint32_t rgb,
                     float thickness, float alpha) {
    if (!renderTarget_) {
        return;
    }
    ID2D1SolidColorBrush* brush = Brush(rgb, alpha);
    if (brush) {
        renderTarget_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush,
                                thickness);
    }
}

void App::DrawUiLabel(const std::wstring& text, IDWriteTextFormat* format,
                      const RectF& rect, std::uint32_t rgb, UiTextAlign align,
                      UiTextVAlign valign, bool wrap, float alpha) {
    if (!renderTarget_ || !format) {
        return;
    }
    D2D1_COLOR_F color = ToColor(rgb, alpha);
    DrawUiText(renderTarget_.Get(), writeFactory_.Get(), text, format, rect, color,
               align, valign, wrap);
}

std::wstring App::ResolveImagePath(const std::wstring& storedPath) const {
    if (storedPath.empty()) {
        return {};
    }
    const mdge::ResolvedAssetPath resolved =
        mdge::ResolveAssetPath(storedPath, BaseDocumentPath());
    return resolved.resolvedPath;
}

ImageAssetPtr App::GetImageAsset(const std::wstring& resolvedPath) {
    if (resolvedPath.empty()) {
        return nullptr;
    }
    auto found = imageCache_.find(resolvedPath);
    if (found != imageCache_.end()) {
        return found->second;
    }
    auto asset = std::make_shared<ImageAsset>();
    if (renderTarget_) {
        std::wstring error;
        if (imageLoader_.LoadBitmap(renderTarget_.Get(), resolvedPath,
                                    asset->bitmap, error)) {
            const D2D1_SIZE_U size = asset->bitmap->GetPixelSize();
            asset->width = size.width;
            asset->height = size.height;
            asset->loaded = true;
        } else {
            asset->error = error.empty() ? L"图像无法载入。" : error;
        }
    } else {
        asset->error = L"窗口尚未准备好。";
    }
    imageCache_.emplace(resolvedPath, asset);
    return asset;
}

bool App::ActiveBlockIsText() const {
    const mdge::Block* block = ActiveBlock();
    return block && mdge::IsTextual(block->type);
}

bool App::ActiveBlockIsTask() const {
    const mdge::Block* block = ActiveBlock();
    return block && mdge::IsTask(block->type);
}

mdge::Block* App::ActiveBlock() {
    if (selection_.block < 0 ||
        static_cast<std::size_t>(selection_.block) >= document_.blocks.size()) {
        return nullptr;
    }
    return &document_.blocks[static_cast<std::size_t>(selection_.block)];
}

const mdge::Block* App::ActiveBlock() const {
    if (selection_.block < 0 ||
        static_cast<std::size_t>(selection_.block) >= document_.blocks.size()) {
        return nullptr;
    }
    return &document_.blocks[static_cast<std::size_t>(selection_.block)];
}

BlockVisual* App::ActiveVisual() {
    if (selection_.block < 0 || !page_.valid) {
        return nullptr;
    }
    for (BlockVisual& visual : page_.blocks) {
        if (visual.index == static_cast<std::size_t>(selection_.block)) {
            return &visual;
        }
    }
    return nullptr;
}

const BlockVisual* App::ActiveVisual() const {
    if (selection_.block < 0 || !page_.valid) {
        return nullptr;
    }
    for (const BlockVisual& visual : page_.blocks) {
        if (visual.index == static_cast<std::size_t>(selection_.block)) {
            return &visual;
        }
    }
    return nullptr;
}

BlockVisual* App::VisualAtIndex(int index) {
    if (index < 0) {
        return nullptr;
    }
    EnsurePageLayout();
    for (BlockVisual& visual : page_.blocks) {
        if (visual.index == static_cast<std::size_t>(index)) {
            return &visual;
        }
    }
    return nullptr;
}

bool App::ActionEnabled(ActionId action) const {
    switch (action) {
        case ActionId::Undo:
            return !undoStack_.empty();
        case ActionId::Redo:
            return !redoStack_.empty();
        case ActionId::StyleParagraph:
        case ActionId::StyleHeading1:
        case ActionId::StyleHeading2:
        case ActionId::StyleHeading3:
            return !sourceFocused_ && ActiveBlockIsText();
        case ActionId::InlineBold:
        case ActionId::InlineItalic:
        case ActionId::InlineCode:
        case ActionId::InlineLink:
            return !sourceFocused_ && ActiveBlockIsText() && HasSelection();
        case ActionId::DeleteBlock:
            return !sourceFocused_ && selection_.block >= 0 &&
                   static_cast<std::size_t>(selection_.block) <
                       document_.blocks.size();
        case ActionId::CopySource:
            return previewVisible_ && previewTab_ == 0;
        case ActionId::PreviewSource:
        case ActionId::PreviewOutline:
            return previewVisible_;
        case ActionId::None:
            return false;
        default:
            return true;
    }
}

bool App::ActionActive(ActionId action) const {
    const mdge::Block* block = ActiveBlock();
    if (sourceFocused_ &&
        (action == ActionId::StyleParagraph || action == ActionId::StyleHeading1 ||
         action == ActionId::StyleHeading2 || action == ActionId::StyleHeading3 ||
         action == ActionId::InlineBold || action == ActionId::InlineItalic ||
         action == ActionId::InlineCode || action == ActionId::InlineLink)) {
        return false;
    }
    switch (action) {
        case ActionId::TogglePreview:
            return previewVisible_;
        case ActionId::PreviewSource:
            return previewTab_ == 0;
        case ActionId::PreviewOutline:
            return previewTab_ == 1;
        case ActionId::StyleParagraph:
            return block && block->type == mdge::BlockType::Paragraph;
        case ActionId::StyleHeading1:
            return block && block->type == mdge::BlockType::Heading1;
        case ActionId::StyleHeading2:
            return block && block->type == mdge::BlockType::Heading2;
        case ActionId::StyleHeading3:
            return block && block->type == mdge::BlockType::Heading3;
        case ActionId::InlineBold:
            return block && HasSelection() &&
                   mdge::HasStyleInRange(*block, SelectionStart(), SelectionEnd(),
                                         mdge::StyleBold);
        case ActionId::InlineItalic:
            return block && HasSelection() &&
                   mdge::HasStyleInRange(*block, SelectionStart(), SelectionEnd(),
                                         mdge::StyleItalic);
        case ActionId::InlineCode:
            return block && HasSelection() &&
                   mdge::HasStyleInRange(*block, SelectionStart(), SelectionEnd(),
                                         mdge::StyleCode);
        case ActionId::InlineLink:
            return block && HasSelection() &&
                   mdge::HasStyleInRange(*block, SelectionStart(), SelectionEnd(),
                                         mdge::StyleLink);
        default:
            return false;
    }
}

void App::PerformAction(ActionId action) {
    if (!ActionEnabled(action)) {
        if (sourceFocused_ &&
            (action == ActionId::StyleParagraph || action == ActionId::StyleHeading1 ||
             action == ActionId::StyleHeading2 || action == ActionId::StyleHeading3 ||
             action == ActionId::InlineBold || action == ActionId::InlineItalic ||
             action == ActionId::InlineCode || action == ActionId::InlineLink ||
             action == ActionId::DeleteBlock)) {
            ShowToast(L"源码编辑中。点击画布返回可视化编辑后再使用这些按钮。", true);
        } else if (action == ActionId::InlineBold ||
                   action == ActionId::InlineItalic ||
                   action == ActionId::InlineCode ||
                   action == ActionId::InlineLink) {
            ShowToast(L"请先选择文字，再应用行内样式。", true);
        }
        return;
    }
    switch (action) {
        case ActionId::NewProject:
            NewProject();
            break;
        case ActionId::OpenProject:
            OpenProject();
            break;
        case ActionId::SaveProject:
            SaveProject(false);
            break;
        case ActionId::ExportMarkdown:
            ExportMarkdown();
            break;
        case ActionId::Undo:
            Undo();
            break;
        case ActionId::Redo:
            Redo();
            break;
        case ActionId::TogglePreview:
            previewVisible_ = !previewVisible_;
            if (!previewVisible_) {
                sourceFocused_ = false;
            }
            RecalculateUi();
            InvalidatePage();
            InvalidateSourceLayout();
            ClampScrollPositions();
            revealStartMs_ = NowMilliseconds(qpcFrequency_);
            revealActive_ = true;
            InvalidateRect(window_, nullptr, FALSE);
            break;
        case ActionId::DocumentSettings:
            ShowDocumentSettings();
            break;
        case ActionId::StyleParagraph:
            ApplyBlockStyle(mdge::BlockType::Paragraph);
            break;
        case ActionId::StyleHeading1:
            ApplyBlockStyle(mdge::BlockType::Heading1);
            break;
        case ActionId::StyleHeading2:
            ApplyBlockStyle(mdge::BlockType::Heading2);
            break;
        case ActionId::StyleHeading3:
            ApplyBlockStyle(mdge::BlockType::Heading3);
            break;
        case ActionId::InlineBold:
        case ActionId::InlineItalic:
        case ActionId::InlineCode:
        case ActionId::InlineLink:
            ApplyInlineFromToolbar(action);
            break;
        case ActionId::DeleteBlock:
            DeleteActiveBlock(true);
            break;
        case ActionId::PreviewSource:
            if (previewTab_ != 0) {
                sourceFocused_ = false;
                previewScroll_ = 0.0f;
            }
            previewTab_ = 0;
            EnsureSourceTextCurrent();
            BuildSourceLines();
            InvalidateRect(window_, nullptr, FALSE);
            break;
        case ActionId::PreviewOutline:
            sourceFocused_ = false;
            previewTab_ = 1;
            previewScroll_ = 0.0f;
            InvalidateRect(window_, nullptr, FALSE);
            break;
        case ActionId::CopySource: {
            SetClipboardText(CurrentMarkdownSource());
            ShowToast(L"Markdown 源码已复制到剪贴板。");
            break;
        }
        case ActionId::None:
            break;
    }
}

void App::ShowToast(const std::wstring& text, bool error) {
    toastText_ = text;
    toastError_ = error;
    toastExpiry_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(2600);
    if (window_) {
        KillTimer(window_, kToastTimer);
        SetTimer(window_, kToastTimer, 2600, nullptr);
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void App::ScheduleCaretBlink() {
    caretBlinkOn_ = true;
    if (window_) {
        SetTimer(window_, kCaretTimer, 530, nullptr);
        InvalidateRect(window_, nullptr, FALSE);
    }
}

std::wstring App::FileDisplayName() const {
    if (document_.title.empty()) {
        return L"未命名文档";
    }
    return document_.title;
}

std::wstring App::FormatCount(std::size_t value) const {
    return FormatNumber(value);
}

bool App::IsPointInRect(const RectF& rect, float x, float y) const {
    return rect.Contains(x, y);
}

bool App::IsWindowActive() const {
    return window_ && GetForegroundWindow() == window_ && !IsIconic(window_);
}

bool App::IsOverCanvas(float x, float y) const {
    return ui_.canvas.Contains(x, y);
}

bool App::IsOverPreview(float x, float y) const {
    return previewVisible_ && ui_.preview.Contains(x, y);
}

float App::PageTop() const {
    return ui_.canvas.top + kPageTopInset - editorScroll_;
}

float App::PageLocalX(float clientX) const {
    return clientX - page_.pageLeft;
}

float App::PageLocalY(float clientY) const {
    return clientY - PageTop();
}

float App::CaretScreenY(float pageTop) {
    float x = 0.0f;
    float y = 0.0f;
    float height = 0.0f;
    if (!CaretLocalPosition(selection_.block, selection_.caret, x, y, height)) {
        return pageTop + 60.0f;
    }
    EnsurePageLayout();
    if (const BlockVisual* visual = ActiveVisual()) {
        return pageTop + visual->textRect.top + y;
    }
    return pageTop + 60.0f;
}

void App::TriggerTextReveal() {
    revealStartMs_ = NowMilliseconds(qpcFrequency_);
    revealActive_ = true;
}

float App::RevealProgress() const {
    if (!revealActive_) {
        return 1.0f;
    }
    const float elapsed =
        static_cast<float>(NowMilliseconds(qpcFrequency_) - revealStartMs_);
    const float progress =
        std::clamp(elapsed / kTextRevealMilliseconds, 0.0f, 1.0f);
    return progress;
}

void App::DrawTopButton(const ButtonSpec& button, const std::wstring& label,
                        bool primary, bool active, IconKind icon, bool showIcon) {
    const bool hovered = hoveredButton_ == button.action;
    const bool enabled = ActionEnabled(button.action);
    RectF rect = button.rect;
    if (primary) {
        FillRounded(rect, 8.0f, hovered ? palette::kAccentHover : palette::kAccent,
                    1.0f);
    } else if (active) {
        FillRounded(rect, 8.0f, palette::kAccentSoft, 1.0f);
        StrokeRounded(rect, 8.0f, palette::kAccent, 1.0f, 0.35f);
    } else if (hovered && enabled) {
        FillRounded(rect, 8.0f, palette::kCard, 1.0f);
        StrokeRounded(rect, 8.0f, palette::kLine, 1.0f);
    } else {
        FillRounded(rect, 8.0f, palette::kPaper, 0.0f);
    }

    const std::uint32_t textColor =
        primary ? 0xFFFDF8
                : (!enabled ? palette::kLineStrong
                            : (active ? palette::kAccent : palette::kText));
    float labelLeft = rect.left;
    if (showIcon) {
        const RectF iconRect =
            MakeRect(rect.left + 10.0f, rect.CenterY() - 8.0f,
                     rect.left + 26.0f, rect.CenterY() + 8.0f);
        DrawIcon(renderTarget_.Get(), icon, iconRect, Brush(textColor, 0.9f), 1.5f);
        labelLeft += 30.0f;
    }
    const float labelWidth = std::max(10.0f, rect.right - labelLeft - 8.0f);
    DrawUiLabel(label, uiButton_.Get(),
                MakeRect(labelLeft, rect.top, labelLeft + labelWidth, rect.bottom),
                textColor, UiTextAlign::Center, UiTextVAlign::Middle, false,
                enabled ? 1.0f : 0.6f);
}

void App::DrawFormatButton(const ButtonSpec& button, const std::wstring& label,
                           bool active, bool bold, bool italic) {
    const bool enabled = ActionEnabled(button.action);
    const bool hovered = hoveredButton_ == button.action;
    RectF rect = button.rect;
    if (active) {
        FillRounded(rect, 7.0f, palette::kAccentSoft, 1.0f);
        StrokeRounded(rect, 7.0f, palette::kAccent, 1.0f, 0.24f);
    } else if (hovered && enabled) {
        FillRounded(rect, 7.0f, palette::kCard, 1.0f);
    }
    IDWriteTextFormat* format = bold ? uiButtonBold_.Get() : uiButton_.Get();
    if (italic) {
        // CreateTextFormat does not give us an italic button format; use the
        // text layout's font style in the draw helper.
    }
    DrawUiLabel(label, format, rect,
                !enabled ? palette::kLineStrong
                         : (active ? palette::kAccent : palette::kText),
                UiTextAlign::Center, UiTextVAlign::Middle, false,
                enabled ? 1.0f : 0.55f);
}

}  // namespace mdgeui
