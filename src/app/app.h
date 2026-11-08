// MDGE Studio - visual editor window
#pragma once

#include "core/model.h"
#include "image_loader.h"
#include "ui_util.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdgeui {

enum class ActionId {
    None = 0,
    NewProject,
    OpenProject,
    SaveProject,
    ExportMarkdown,
    Undo,
    Redo,
    TogglePreview,
    DocumentSettings,
    StyleParagraph,
    StyleHeading1,
    StyleHeading2,
    StyleHeading3,
    InlineBold,
    InlineItalic,
    InlineCode,
    InlineLink,
    DeleteBlock,
    PreviewSource,
    PreviewOutline,
    CopySource,
};

struct ButtonSpec {
    ActionId action = ActionId::None;
    RectF rect;
};

struct PaletteSpec {
    mdge::BlockType type = mdge::BlockType::Paragraph;
    RectF rect;
};

struct ImageAsset {
    ComPtr<ID2D1Bitmap> bitmap;
    std::wstring error;
    UINT width = 0;
    UINT height = 0;
    bool loaded = false;
};

using ImageAssetPtr = std::shared_ptr<ImageAsset>;

struct BlockVisual {
    std::size_t index = 0;
    mdge::BlockType type = mdge::BlockType::Paragraph;
    RectF card;
    RectF textRect;
    RectF gripRect;
    float height = 0.0f;
    int listNumber = 1;
    ComPtr<IDWriteTextLayout> textLayout;
    std::wstring layoutText;
    ImageAssetPtr image;
    RectF imageRect;
    RectF captionRect;
    std::wstring imageError;
    std::wstring resolvedImagePath;
};

struct PageLayout {
    float pageLeft = 0.0f;
    float pageWidth = 0.0f;
    float pageHeight = 0.0f;
    float contentLeft = 0.0f;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    float builtCanvasWidth = -1.0f;
    bool valid = false;
    std::vector<BlockVisual> blocks;
};

struct UiGeometry {
    RectF client;
    RectF header;
    RectF brand;
    RectF headerContent;
    RectF documentTitleRect;
    RectF documentSubtitleRect;
    RectF sidebar;
    RectF formatBar;
    RectF canvas;
    RectF preview;
    RectF status;
    RectF docCard;
    RectF docIconRect;
    RectF docTitleRect;
    RectF docMetaRect;
    RectF docHintRect;
    RectF paletteLabelRect;
    RectF paletteHintRect;
    RectF sidebarTipRect;
    RectF sidebarFooterRect;
    RectF previewHeader;
    RectF previewBody;
    RectF previewScrollTrack;
    RectF canvasScrollTrack;
    RectF toastRect;
    std::vector<ButtonSpec> headerButtons;
    std::vector<ButtonSpec> formatButtons;
    std::vector<ButtonSpec> previewButtons;
    std::vector<PaletteSpec> paletteItems;
};

struct SourceLineView {
    std::wstring text;
    ComPtr<IDWriteTextLayout> layout;
    float height = 18.0f;
    float offsetY = 0.0f;
    std::size_t start = 0;
    std::size_t length = 0;
    int colorKind = 0;
    bool blank = true;
};

struct OutlineRow {
    std::size_t blockIndex = 0;
    int level = 1;
    std::wstring label;
    float y = 0.0f;
    float height = 32.0f;
};

struct EditorSnapshot {
    mdge::Document document;
    mdge::EditorSelection selection;
    bool sourceFocused = false;
    std::wstring sourceText;
    std::size_t sourceCaret = 0;
    std::size_t sourceAnchor = 0;
};

struct DragState {
    enum class Kind {
        None,
        TextSelect,
        SourceSelect,
        Block,
        Palette,
        CanvasScroll,
        PreviewScroll,
    };
    Kind kind = Kind::None;
    int sourceIndex = -1;
    mdge::BlockType paletteType = mdge::BlockType::Paragraph;
    int insertIndex = -1;
    bool moved = false;
    bool overCanvas = false;
    float startX = 0.0f;
    float startY = 0.0f;
    float scrollStart = 0.0f;
    float grabOffsetX = 0.0f;
    float grabOffsetY = 0.0f;
};

class App {
  public:
    App();
    ~App();

    bool Create(HINSTANCE instance, const std::wstring& initialProjectPath);
    int RunMessageLoop();
    HWND Window() const { return window_; }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                                      LPARAM lparam);

  private:
    // Window and graphics lifecycle.
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool CreateDeviceIndependentResources();
    void CreateTextFormats();
    bool EnsureRenderTarget();
    void DiscardRenderTarget();
    void ClearBrushCache();
    void RecalculateUi();
    void OnPaint();
    void DrawScene();

    // Drawing.
    void DrawHeader();
    void DrawSidebar();
    void DrawFormatBar();
    void DrawCanvas();
    void DrawPage();
    void DrawPreview();
    void DrawStatusBar();
    void DrawDragOverlay();
    void DrawToast();
    void DrawTopButton(const ButtonSpec& button, const std::wstring& label,
                       bool primary = false, bool active = false,
                       IconKind icon = IconKind::Markdown, bool showIcon = false);
    void DrawFormatButton(const ButtonSpec& button, const std::wstring& label,
                          bool active, bool bold = false, bool italic = false);
    void DrawBlock(const BlockVisual& visual, const mdge::Block& block,
                   bool active, bool hovered, float pageTop);
    void DrawTextBlock(const BlockVisual& visual, const mdge::Block& block,
                       bool active, float pageTop);
    void DrawCodeBlock(const BlockVisual& visual, const mdge::Block& block,
                       bool active, float pageTop);
    void DrawImageBlock(const BlockVisual& visual, const mdge::Block& block,
                        bool active, float pageTop);
    void DrawSelectionAndCaret(const BlockVisual& visual, const mdge::Block& block,
                               float pageTop);
    RectF TaskCheckboxRect(const BlockVisual& visual) const;
    int HitTaskCheckbox(float x, float y) const;
    void ToggleTaskBlock(int blockIndex);
    void DrawSourceView();
    void DrawOutlineView();
    void DrawSidebarIcon(const RectF& bounds);
    void DrawEmptyState();

    // Layout and cache.
    void EnsurePageLayout();
    void InvalidatePage();
    void InvalidateSourceLayout();
    void InvalidateEditorState();
    void ClampScrollPositions();
    void BuildSourceLines();
    float EditorScrollRange() const;
    float PreviewScrollRange() const;
    std::vector<OutlineRow> BuildOutlineRows() const;
    void UpdateScrollbarRects();
    float CaretScreenY(float pageTop);
    ImageAssetPtr GetImageAsset(const std::wstring& resolvedPath);
    std::wstring ResolveImagePath(const std::wstring& storedPath) const;
    std::wstring CurrentMarkdownSource();

    // File workflow.
    void InitializeBlankDocument();
    bool OpenProjectFile(const std::wstring& path, bool showErrors,
                         bool skipPrompt = false);
    bool OpenMarkdownFile(const std::wstring& path, bool showErrors,
                          bool skipPrompt = false);
    bool SaveProject(bool saveAs);
    std::wstring ProjectDirectory() const;
    std::wstring BaseDocumentPath() const;
    void NewProject();
    void OpenProject();
    void ExportMarkdown();
    void MarkDocumentDirty();
    bool MaybeSaveBeforeDestructiveAction();
    void ApplyProjectWindowIcon();
    void ReleaseProjectWindowIcon();
    void UpdateWindowTitle();
    void UpdateWindowIconFromProject();

    // Document editing and selection.
    mdge::Block* ActiveBlock();
    const mdge::Block* ActiveBlock() const;
    BlockVisual* ActiveVisual();
    const BlockVisual* ActiveVisual() const;
    size_t SelectionStart() const;
    size_t SelectionEnd() const;
    bool HasSelection() const;
    void ClearSelection();
    bool ActiveBlockIsText() const;
    void NormalizeSelection();
    void SetActiveBlock(int index, std::size_t caret, bool extend);
    void SelectAllInBlock();
    void SelectWord(int blockIndex, float pageX, float pageY);
    size_t CaretFromPoint(int blockIndex, float pageX, float pageY);
    bool CaretLocalPosition(int blockIndex, std::size_t caret, float& x, float& y,
                            float& height);
    void NotifyCaretMoved();
    void EnsureCaretVisible();
    bool ActiveBlockIsTask() const;

    // Editable Markdown source pane.
    void BeginSourceEditing(float x, float y, bool extend);
    void LeaveSourceEditing();
    void EnsureSourceTextCurrent();
    void SyncDocumentFromSource();
    void CommitSourceTextChange();
    std::size_t SourceCaretFromPoint(float x, float y);
    bool SourceCaretMetrics(std::size_t caret, int& lineIndex, float& localX,
                            float& localY, float& height);
    int SourceLineAtOffset(std::size_t offset) const;
    void EnsureSourceCaretVisible();
    std::size_t SourceSelectionStart() const;
    std::size_t SourceSelectionEnd() const;
    bool HasSourceSelection() const;
    void SourceDeleteSelection();
    void SourceInsertText(const std::wstring& text, bool coalesce,
                          const wchar_t* undoKey);
    void SourceBackspace();
    void SourceDeleteForward();
    void SourceMoveHorizontal(bool right, bool extend, bool byWord);
    void SourceMoveVertical(bool down, bool extend);
    void SourceMoveLineEdge(bool end, bool extend, bool whole);
    void SourceSelectAll();
    void SourceCopy(bool cut);
    void SourcePaste();
    void OnSourceKeyDown(WPARAM key, LPARAM data);
    void SelectSourceWordAt(float x, float y);

    void InsertTextAtCaret(const std::wstring& text);
    void DeleteSelection();
    void BackspaceAtCaret();
    void DeleteForward();
    void SplitAtCaret();
    void MergeWithPreviousBlock();
    void MoveCaretHorizontal(bool right, bool extend, bool byWord);
    void MoveCaretVertical(bool down, bool extend);
    void MoveCaretLineEdge(bool end, bool extend, bool documentEdge);
    void MoveCaretToAdjacentBlock(bool down, float preferredX);
    void DeleteActiveMediaBlock();
    void DeleteActiveBlock(bool keepOneEmptyBlock);
    void ApplyInlineFromToolbar(ActionId action);
    void ApplyBlockStyle(mdge::BlockType type);
    void EditSelectedLink();

    // Clipboard.
    void CopySelectionToClipboard(bool cut);
    void PasteFromClipboard();
    bool SetClipboardText(const std::wstring& text);
    std::wstring GetClipboardText();

    // Undo and redo.
    EditorSnapshot CaptureSnapshot() const;
    void PushUndo(const std::wstring& key, bool coalesce);
    void Undo();
    void Redo();
    void RestoreSnapshot(const EditorSnapshot& snapshot);
    void ClearUndoHistory();

    // Commands.
    void PerformAction(ActionId action);
    bool ActionEnabled(ActionId action) const;
    bool ActionActive(ActionId action) const;
    void InsertBlock(mdge::BlockType type, int atIndex);
    void InsertImageBlockFromDialog(int atIndex);
    void AddImageBlockAt(int atIndex, const std::wstring& pickedPath);
    std::wstring StoreImagePath(const std::wstring& pickedPath) const;
    void InsertDroppedFiles(const std::vector<std::wstring>& files, float x,
                            float y);
    void ShowDocumentSettings();
    void StartBlockDrag(int blockIndex);
    void UpdateDragTarget(float x, float y);
    void FinishDrag(bool canceled);
    void ShowToast(const std::wstring& text, bool error = false);
    void TriggerTextReveal();
    float RevealProgress() const;

    // Input.
    void OnMouseMove(float x, float y, WPARAM keys);
    void OnLeftButtonDown(float x, float y, WPARAM keys);
    void OnLeftButtonUp(float x, float y, WPARAM keys);
    void OnLeftButtonDoubleClick(float x, float y, WPARAM keys);
    void OnMouseWheel(float x, float y, int wheelDelta);
    void OnMouseLeave();
    void OnKeyDown(WPARAM key, LPARAM data);
    void OnChar(wchar_t character);
    void OnImeComposition(WPARAM wparam, LPARAM lparam);
    void UpdateHover(float x, float y);
    void SetCursorForHover();
    void UpdateImeCompositionWindow();
    void FocusEditor();

    // Hit testing.
    ButtonSpec* FindButton(float x, float y);
    ActionId HitAction(float x, float y) const;
    int HitPalette(float x, float y) const;
    int HitBlockAt(float x, float y) const;
    int HitGrip(float x, float y) const;
    int HitDropIndex(float y) const;
    int HitOutlineRow(float x, float y) const;
    bool IsOverCanvas(float x, float y) const;
    bool IsOverPreview(float x, float y) const;
    float PageTop() const;
    float PageLocalX(float clientX) const;
    float PageLocalY(float clientY) const;
    BlockVisual* VisualAtIndex(int index);

    // Small utilities.
    ID2D1SolidColorBrush* Brush(std::uint32_t rgb, float alpha = 1.0f);
    ID2D1SolidColorBrush* Brush(const D2D1_COLOR_F& color);
    void FillRect(const RectF& rect, std::uint32_t rgb, float alpha = 1.0f);
    void FillRounded(const RectF& rect, float radius, std::uint32_t rgb,
                     float alpha = 1.0f);
    void StrokeRounded(const RectF& rect, float radius, std::uint32_t rgb,
                       float thickness = 1.0f, float alpha = 1.0f);
    void StrokeLine(float x1, float y1, float x2, float y2, std::uint32_t rgb,
                    float thickness = 1.0f, float alpha = 1.0f);
    void DrawUiLabel(const std::wstring& text, IDWriteTextFormat* format,
                     const RectF& rect, std::uint32_t rgb,
                     UiTextAlign align = UiTextAlign::Left,
                     UiTextVAlign valign = UiTextVAlign::Middle, bool wrap = false,
                     float alpha = 1.0f);
    D2D1_COLOR_F ToColor(std::uint32_t rgb, float alpha) const;
    std::wstring FileDisplayName() const;
    std::wstring SelectedText() const;
    bool IsPointInRect(const RectF& rect, float x, float y) const;
    bool IsWindowActive() const;
    std::wstring FormatCount(std::size_t value) const;
    void ScheduleCaretBlink();

    // IME.
    void ResetComposition();

    // Members: window and graphics.
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    float dpi_ = 96.0f;
    float clientWidth_ = 0.0f;
    float clientHeight_ = 0.0f;
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<ID2D1StrokeStyle> roundStroke_;
    ImageLoader imageLoader_;
    bool deviceReady_ = false;
    std::unordered_map<std::uint64_t, ComPtr<ID2D1SolidColorBrush>> brushes_;
    std::unordered_map<std::wstring, ImageAssetPtr> imageCache_;

    // Formats.
    ComPtr<IDWriteTextFormat> uiTiny_;
    ComPtr<IDWriteTextFormat> uiSmall_;
    ComPtr<IDWriteTextFormat> uiBody_;
    ComPtr<IDWriteTextFormat> uiBodyBold_;
    ComPtr<IDWriteTextFormat> uiButton_;
    ComPtr<IDWriteTextFormat> uiButtonBold_;
    ComPtr<IDWriteTextFormat> uiDocTitle_;
    ComPtr<IDWriteTextFormat> uiBrand_;
    ComPtr<IDWriteTextFormat> uiBadge_;
    ComPtr<IDWriteTextFormat> uiMono_;
    ComPtr<IDWriteTextFormat> bodyFormat_;
    ComPtr<IDWriteTextFormat> h1Format_;
    ComPtr<IDWriteTextFormat> h2Format_;
    ComPtr<IDWriteTextFormat> h3Format_;
    ComPtr<IDWriteTextFormat> quoteFormat_;
    ComPtr<IDWriteTextFormat> codeFormat_;
    ComPtr<IDWriteTextFormat> sourceFormat_;

    // Layout and caches.
    UiGeometry ui_;
    PageLayout page_;
    std::wstring markdownSource_;
    bool markdownDirty_ = true;
    std::vector<SourceLineView> sourceLines_;
    bool sourceLayoutDirty_ = true;
    float sourceLayoutWidth_ = -1.0f;
    float sourceTotalHeight_ = 0.0f;
    float editorScroll_ = 0.0f;
    float previewScroll_ = 0.0f;

    // Document and project.
    mdge::Document document_;
    std::wstring projectPath_;
    std::wstring markdownPath_;
    bool dirty_ = false;
    bool previewVisible_ = true;
    int previewTab_ = 0;  // 0 = Markdown, 1 = Outline
    HICON projectIconLarge_ = nullptr;
    HICON projectIconSmall_ = nullptr;
    bool openingProject_ = false;

    // Editor state.
    mdge::EditorSelection selection_;
    bool hasFocus_ = false;
    bool caretVisible_ = true;
    bool caretBlinkOn_ = true;
    float preferredCaretX_ = -1.0f;
    std::wstring compositionText_;
    bool sourceFocused_ = false;
    std::size_t sourceCaret_ = 0;
    std::size_t sourceAnchor_ = 0;
    float preferredSourceX_ = -1.0f;

    // Undo.
    std::deque<EditorSnapshot> undoStack_;
    std::deque<EditorSnapshot> redoStack_;
    std::wstring lastUndoKey_;
    std::chrono::steady_clock::time_point lastUndoTime_{};

    // Mouse and drag.
    float mouseX_ = -1.0f;
    float mouseY_ = -1.0f;
    bool mouseInside_ = false;
    DragState drag_;
    ActionId hoveredButton_ = ActionId::None;
    int hoveredBlock_ = -1;
    int hoveredGrip_ = -1;
    int hoveredPalette_ = -1;
    int hoveredOutline_ = -1;
    bool hoveredDocCard_ = false;
    bool hoveredPreviewTab_ = false;
    int hoveredPreviewTabIndex_ = -1;
    int cursorKind_ = 0;  // 0 arrow, 1 ibeam, 2 hand, 3 size-all

    // Reveal sweep (left to right, 8 ms total).
    double revealStartMs_ = -1000000.0;
    bool revealActive_ = false;
    double qpcFrequency_ = 1.0;

    // Toast.
    std::wstring toastText_;
    bool toastError_ = false;
    std::chrono::steady_clock::time_point toastExpiry_{};

    bool trackingMouseLeave_ = false;
    bool suppressNextChar_ = false;
    int pressedSpaceBlock_ = -1;
};

}  // namespace mdgeui
