#include "app.h"

#include "core/markdown.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace mdgeui {
namespace {

std::size_t PrevCodeUnit(const std::wstring& text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    --position;
    if (position > 0) {
        const wchar_t low = text[position];
        const wchar_t high = text[position - 1];
        if (low >= 0xDC00 && low <= 0xDFFF && high >= 0xD800 &&
            high <= 0xDBFF) {
            return position - 1;
        }
    }
    return position;
}

std::size_t NextCodeUnit(const std::wstring& text, std::size_t position) {
    if (position >= text.size()) {
        return text.size();
    }
    const wchar_t high = text[position];
    if (high >= 0xD800 && high <= 0xDBFF && position + 1 < text.size()) {
        const wchar_t low = text[position + 1];
        if (low >= 0xDC00 && low <= 0xDFFF) {
            return position + 2;
        }
    }
    return position + 1;
}

bool IsSpace(wchar_t ch) {
    return iswspace(static_cast<wint_t>(ch)) != 0;
}

bool IsWord(wchar_t ch) {
    return iswalnum(static_cast<wint_t>(ch)) != 0 || ch == L'_' ||
           (ch >= 0x3400 && ch <= 0x9FFF);
}

std::size_t PreviousWordEdge(const std::wstring& text, std::size_t position) {
    std::size_t cursor = std::min(position, text.size());
    while (cursor > 0) {
        const std::size_t previous = PrevCodeUnit(text, cursor);
        if (!IsSpace(text[previous])) {
            break;
        }
        cursor = previous;
    }
    if (cursor == 0) {
        return 0;
    }
    const bool word = IsWord(text[PrevCodeUnit(text, cursor)]);
    while (cursor > 0) {
        const std::size_t previous = PrevCodeUnit(text, cursor);
        if (IsSpace(text[previous]) || IsWord(text[previous]) != word) {
            break;
        }
        cursor = previous;
    }
    return cursor;
}

std::size_t NextWordEdge(const std::wstring& text, std::size_t position) {
    std::size_t cursor = std::min(position, text.size());
    while (cursor < text.size() && IsSpace(text[cursor])) {
        cursor = NextCodeUnit(text, cursor);
    }
    if (cursor >= text.size()) {
        return text.size();
    }
    const bool word = IsWord(text[cursor]);
    while (cursor < text.size() && !IsSpace(text[cursor]) &&
           IsWord(text[cursor]) == word) {
        cursor = NextCodeUnit(text, cursor);
    }
    return cursor;
}

}  // namespace

void App::BeginSourceEditing(float x, float y, bool extend) {
    EnsureSourceTextCurrent();
    const bool wasFocused = sourceFocused_;
    sourceFocused_ = true;
    hasFocus_ = true;
    BuildSourceLines();
    sourceCaret_ = SourceCaretFromPoint(x, y);
    sourceCaret_ = std::min(sourceCaret_, markdownSource_.size());
    if (!extend || !wasFocused) {
        sourceAnchor_ = sourceCaret_;
    } else {
        sourceAnchor_ = std::min(sourceAnchor_, markdownSource_.size());
    }
    preferredSourceX_ = -1.0f;
    lastUndoKey_.clear();
    // The source pane and the visual page never show competing carets.
    selection_.anchor = selection_.caret;
    FocusEditor();
    ScheduleCaretBlink();
    EnsureSourceCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::LeaveSourceEditing() {
    if (!sourceFocused_) {
        return;
    }
    sourceFocused_ = false;
    preferredSourceX_ = -1.0f;
    lastUndoKey_.clear();
    ScheduleCaretBlink();
    FocusEditor();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::EnsureSourceTextCurrent() {
    if (markdownDirty_ || markdownSource_.empty()) {
        const bool wasDirty = markdownDirty_;
        CurrentMarkdownSource();
        if (wasDirty) {
            InvalidateSourceLayout();
        }
    }
    BuildSourceLines();
    sourceCaret_ = std::min(sourceCaret_, markdownSource_.size());
    sourceAnchor_ = std::min(sourceAnchor_, markdownSource_.size());
}

void App::SyncDocumentFromSource() {
    std::vector<mdge::Block> blocks = mdge::MarkdownToBlocks(markdownSource_);
    if (blocks.empty()) {
        blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
    }
    document_.blocks = std::move(blocks);
    mdge::NormalizeDocument(document_);
    markdownDirty_ = false;
    NormalizeSelection();
    InvalidatePage();
    MarkDocumentDirty();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::CommitSourceTextChange() {
    markdownDirty_ = false;
    SyncDocumentFromSource();
    InvalidateSourceLayout();
    BuildSourceLines();
    ClampScrollPositions();
    EnsureSourceCaretVisible();
    ScheduleCaretBlink();
    InvalidateRect(window_, nullptr, FALSE);
}

std::size_t App::SourceSelectionStart() const {
    const std::size_t limit = markdownSource_.size();
    return std::min(std::min(sourceCaret_, sourceAnchor_), limit);
}

std::size_t App::SourceSelectionEnd() const {
    const std::size_t limit = markdownSource_.size();
    return std::min(std::max(sourceCaret_, sourceAnchor_), limit);
}

bool App::HasSourceSelection() const {
    return sourceFocused_ ? SourceSelectionStart() < SourceSelectionEnd()
                          : false;
}

int App::SourceLineAtOffset(std::size_t offset) const {
    if (sourceLines_.empty()) {
        return 0;
    }
    for (std::size_t i = 0; i < sourceLines_.size(); ++i) {
        const SourceLineView& line = sourceLines_[i];
        if (offset < line.start) {
            return static_cast<int>(i > 0 ? i - 1 : 0);
        }
        if (offset <= line.start + line.length) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(sourceLines_.size()) - 1;
}

std::size_t App::SourceCaretFromPoint(float x, float y) {
    BuildSourceLines();
    if (sourceLines_.empty()) {
        return 0;
    }
    const float contentTop = ui_.previewBody.top + 14.0f - previewScroll_;
    const float localY = y - contentTop;
    std::size_t lineIndex = sourceLines_.size() - 1;
    for (std::size_t i = 0; i < sourceLines_.size(); ++i) {
        const SourceLineView& line = sourceLines_[i];
        if (localY < line.offsetY + line.height) {
            lineIndex = i;
            break;
        }
    }
    const SourceLineView& line = sourceLines_[lineIndex];
    if (!line.layout) {
        return line.start;
    }
    const float localX = x - (ui_.previewBody.left + 38.0f);
    const float localLineY = localY - line.offsetY;
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(line.layout->HitTestPoint(localX, localLineY, &trailing, &inside,
                                         &metrics))) {
        return line.start + line.length;
    }
    std::size_t position = metrics.textPosition;
    if (trailing) {
        position = std::min(line.length, position + 1);
    }
    return std::min(markdownSource_.size(), line.start + position);
}

bool App::SourceCaretMetrics(std::size_t caret, int& lineIndex, float& localX,
                             float& localY, float& height) {
    BuildSourceLines();
    if (sourceLines_.empty()) {
        return false;
    }
    caret = std::min(caret, markdownSource_.size());
    const int index = SourceLineAtOffset(caret);
    lineIndex = std::max(0, std::min(index, static_cast<int>(sourceLines_.size()) - 1));
    const SourceLineView& line = sourceLines_[static_cast<std::size_t>(lineIndex)];
    const std::size_t localPosition =
        caret >= line.start ? std::min(caret - line.start, line.length) : 0;
    if (!line.layout) {
        localX = 0.0f;
        localY = 0.0f;
        height = std::max(18.0f, line.height - 2.0f);
        return true;
    }
    const UINT32 layoutPosition = static_cast<UINT32>(localPosition);
    FLOAT pointX = 0.0f;
    FLOAT pointY = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics{};
    // Use the leading edge of the requested code unit for every caret
    // position, including the end of a line. Using the previous character
    // with isTrailingHit and then adding metrics.width again shifts the
    // caret one character past the end of the text.
    if (FAILED(line.layout->HitTestTextPosition(layoutPosition, FALSE, &pointX,
                                                &pointY, &metrics))) {
        return false;
    }
    localX = pointX;
    localY = pointY;
    height = metrics.height > 0.0f ? metrics.height : 18.0f;
    return true;
}

void App::EnsureSourceCaretVisible() {
    if (!sourceFocused_) {
        return;
    }
    BuildSourceLines();
    int lineIndex = 0;
    float localX = 0.0f;
    float localY = 0.0f;
    float height = 18.0f;
    if (!SourceCaretMetrics(sourceCaret_, lineIndex, localX, localY, height)) {
        return;
    }
    const float top = ui_.previewBody.top + 14.0f -
                      previewScroll_ +
                      sourceLines_[static_cast<std::size_t>(lineIndex)].offsetY +
                      localY;
    const float bottom = top + height;
    const float viewTop = ui_.previewBody.top + 8.0f;
    const float viewBottom = ui_.previewBody.bottom - 8.0f;
    if (top < viewTop) {
        previewScroll_ = std::max(0.0f, previewScroll_ - (viewTop - top));
    } else if (bottom > viewBottom) {
        previewScroll_ =
            std::min(PreviewScrollRange(),
                     previewScroll_ + (bottom - viewBottom));
    }
}

void App::SourceInsertText(const std::wstring& text, bool coalesce,
                           const wchar_t* undoKey) {
    if (text.empty()) {
        return;
    }
    EnsureSourceTextCurrent();
    if (HasSourceSelection()) {
        PushUndo(undoKey, coalesce);
        const std::size_t start = SourceSelectionStart();
        const std::size_t end = SourceSelectionEnd();
        markdownSource_.replace(start, end - start, text);
        sourceCaret_ = start + text.size();
    } else {
        PushUndo(undoKey, coalesce);
        const std::size_t position = std::min(sourceCaret_, markdownSource_.size());
        markdownSource_.insert(position, text);
        sourceCaret_ = position + text.size();
    }
    sourceAnchor_ = sourceCaret_;
    preferredSourceX_ = -1.0f;
    CommitSourceTextChange();
}

void App::SourceDeleteSelection() {
    if (!HasSourceSelection()) {
        return;
    }
    PushUndo(L"source-delete", true);
    const std::size_t start = SourceSelectionStart();
    const std::size_t end = SourceSelectionEnd();
    markdownSource_.erase(start, end - start);
    sourceCaret_ = start;
    sourceAnchor_ = start;
    preferredSourceX_ = -1.0f;
    CommitSourceTextChange();
}

void App::SourceBackspace() {
    if (HasSourceSelection()) {
        SourceDeleteSelection();
        return;
    }
    if (sourceCaret_ == 0 || markdownSource_.empty()) {
        return;
    }
    const std::size_t previous = PrevCodeUnit(markdownSource_, sourceCaret_);
    PushUndo(L"source-delete", true);
    markdownSource_.erase(previous, sourceCaret_ - previous);
    sourceCaret_ = previous;
    sourceAnchor_ = previous;
    preferredSourceX_ = -1.0f;
    CommitSourceTextChange();
}

void App::SourceDeleteForward() {
    if (HasSourceSelection()) {
        SourceDeleteSelection();
        return;
    }
    if (sourceCaret_ >= markdownSource_.size()) {
        return;
    }
    const std::size_t next = NextCodeUnit(markdownSource_, sourceCaret_);
    PushUndo(L"source-delete", true);
    markdownSource_.erase(sourceCaret_, next - sourceCaret_);
    sourceAnchor_ = sourceCaret_;
    preferredSourceX_ = -1.0f;
    CommitSourceTextChange();
}

void App::SourceMoveHorizontal(bool right, bool extend, bool byWord) {
    EnsureSourceTextCurrent();
    if (HasSourceSelection() && !extend) {
        sourceCaret_ = right ? SourceSelectionEnd() : SourceSelectionStart();
        sourceAnchor_ = sourceCaret_;
        preferredSourceX_ = -1.0f;
        lastUndoKey_.clear();
        ScheduleCaretBlink();
        EnsureSourceCaretVisible();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    std::size_t newCaret = sourceCaret_;
    if (byWord) {
        newCaret = right ? NextWordEdge(markdownSource_, sourceCaret_)
                         : PreviousWordEdge(markdownSource_, sourceCaret_);
    } else {
        newCaret = right ? NextCodeUnit(markdownSource_, sourceCaret_)
                         : PrevCodeUnit(markdownSource_, sourceCaret_);
    }
    sourceCaret_ = std::min(newCaret, markdownSource_.size());
    if (!extend) {
        sourceAnchor_ = sourceCaret_;
    }
    preferredSourceX_ = -1.0f;
    lastUndoKey_.clear();
    ScheduleCaretBlink();
    EnsureSourceCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::SourceMoveVertical(bool down, bool extend) {
    EnsureSourceTextCurrent();
    if (sourceLines_.empty()) {
        return;
    }
    if (HasSourceSelection() && !extend) {
        sourceCaret_ = down ? SourceSelectionEnd() : SourceSelectionStart();
        sourceAnchor_ = sourceCaret_;
    }
    int lineIndex = 0;
    float localX = 0.0f;
    float localY = 0.0f;
    float height = 18.0f;
    if (!SourceCaretMetrics(sourceCaret_, lineIndex, localX, localY, height)) {
        return;
    }
    if (preferredSourceX_ < 0.0f) {
        preferredSourceX_ = localX;
    }
    SourceLineView& line = sourceLines_[static_cast<std::size_t>(lineIndex)];

    auto moveToEdgeOfLine = [&](int targetLine, bool atBottom) {
        targetLine = std::max(0, std::min(targetLine,
                                          static_cast<int>(sourceLines_.size()) - 1));
        const SourceLineView& target = sourceLines_[static_cast<std::size_t>(targetLine)];
        if (!target.layout) {
            sourceCaret_ = target.start;
        } else {
            BOOL trailing = FALSE;
            BOOL inside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            target.layout->HitTestPoint(preferredSourceX_,
                                        atBottom ? 100000.0f : 0.0f, &trailing,
                                        &inside, &metrics);
            std::size_t caret = metrics.textPosition + (trailing ? 1u : 0u);
            sourceCaret_ = target.start + std::min(caret, target.length);
        }
    };

    if (!down) {
        if (localY > 0.5f) {
            BOOL trailing = FALSE;
            BOOL inside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            line.layout->HitTestPoint(preferredSourceX_, localY - 1.0f, &trailing,
                                      &inside, &metrics);
            std::size_t caret = metrics.textPosition + (trailing ? 1u : 0u);
            sourceCaret_ = line.start + std::min(caret, line.length);
        } else if (lineIndex > 0) {
            moveToEdgeOfLine(lineIndex - 1, true);
        }
    } else {
        DWRITE_TEXT_METRICS textMetrics{};
        if (line.layout) {
            line.layout->GetMetrics(&textMetrics);
        }
        const bool atBottom = localY + height >= textMetrics.height - 1.0f;
        if (!atBottom) {
            BOOL trailing = FALSE;
            BOOL inside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            line.layout->HitTestPoint(preferredSourceX_, localY + height + 1.0f,
                                      &trailing, &inside, &metrics);
            std::size_t caret = metrics.textPosition + (trailing ? 1u : 0u);
            sourceCaret_ = line.start + std::min(caret, line.length);
        } else if (lineIndex + 1 < static_cast<int>(sourceLines_.size())) {
            moveToEdgeOfLine(lineIndex + 1, false);
        }
    }
    sourceCaret_ = std::min(sourceCaret_, markdownSource_.size());
    if (!extend) {
        sourceAnchor_ = sourceCaret_;
    }
    lastUndoKey_.clear();
    ScheduleCaretBlink();
    EnsureSourceCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::SourceMoveLineEdge(bool end, bool extend, bool whole) {
    EnsureSourceTextCurrent();
    if (whole) {
        sourceCaret_ = end ? markdownSource_.size() : 0;
    } else {
        const int index = SourceLineAtOffset(sourceCaret_);
        if (!sourceLines_.empty()) {
            const SourceLineView& line =
                sourceLines_[static_cast<std::size_t>(
                    std::max(0, std::min(index,
                                         static_cast<int>(sourceLines_.size()) - 1)))];
            sourceCaret_ = end ? line.start + line.length : line.start;
        }
    }
    sourceCaret_ = std::min(sourceCaret_, markdownSource_.size());
    if (!extend) {
        sourceAnchor_ = sourceCaret_;
    }
    preferredSourceX_ = -1.0f;
    lastUndoKey_.clear();
    ScheduleCaretBlink();
    EnsureSourceCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::SourceSelectAll() {
    EnsureSourceTextCurrent();
    sourceAnchor_ = 0;
    sourceCaret_ = markdownSource_.size();
    preferredSourceX_ = -1.0f;
    lastUndoKey_.clear();
    ScheduleCaretBlink();
    EnsureSourceCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::SourceCopy(bool cut) {
    if (!HasSourceSelection()) {
        return;
    }
    const std::wstring selected =
        markdownSource_.substr(SourceSelectionStart(),
                               SourceSelectionEnd() - SourceSelectionStart());
    if (!SetClipboardText(selected)) {
        ShowToast(L"无法写入剪贴板。", true);
        return;
    }
    if (cut) {
        SourceDeleteSelection();
        ShowToast(L"已剪切源码。");
    } else {
        ShowToast(L"已复制源码。");
    }
}

void App::SourcePaste() {
    std::wstring text = GetClipboardText();
    if (text.empty()) {
        return;
    }
    if (text.size() > 4u * 1024u * 1024u) {
        ShowToast(L"剪贴板内容过大，已取消粘贴。", true);
        return;
    }
    std::wstring normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;
            }
            normalized.push_back(L'\n');
        } else {
            normalized.push_back(text[i]);
        }
    }
    SourceInsertText(normalized, false, L"source-paste");
}

void App::SelectSourceWordAt(float x, float y) {
    BeginSourceEditing(x, y, false);
    const std::size_t caret = sourceCaret_;
    std::size_t start = caret;
    std::size_t end = caret;
    if (start < markdownSource_.size()) {
        const bool word = IsWord(markdownSource_[start]);
        const bool space = IsSpace(markdownSource_[start]);
        while (start > 0) {
            const std::size_t previous = PrevCodeUnit(markdownSource_, start);
            const wchar_t ch = markdownSource_[previous];
            if ((word && IsWord(ch)) || (space && IsSpace(ch)) ||
                (!word && !space && !IsWord(ch) && !IsSpace(ch))) {
                start = previous;
            } else {
                break;
            }
        }
        while (end < markdownSource_.size()) {
            const wchar_t ch = markdownSource_[end];
            if ((word && IsWord(ch)) || (space && IsSpace(ch)) ||
                (!word && !space && !IsWord(ch) && !IsSpace(ch))) {
                end = NextCodeUnit(markdownSource_, end);
            } else {
                break;
            }
        }
    }
    sourceAnchor_ = start;
    sourceCaret_ = std::max(start, end);
    preferredSourceX_ = -1.0f;
    lastUndoKey_.clear();
    ScheduleCaretBlink();
    EnsureSourceCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::OnSourceKeyDown(WPARAM key, LPARAM data) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool repeat = (data & (1 << 30)) != 0;

    if (ctrl) {
        switch (key) {
            case 'N':
                if (!repeat) {
                    NewProject();
                }
                return;
            case 'O':
                if (!repeat) {
                    OpenProject();
                }
                return;
            case 'S':
                if (!repeat) {
                    SaveProject(shift);
                }
                return;
            case 'E':
                if (!repeat) {
                    ExportMarkdown();
                }
                return;
            case 'P':
                if (shift && !repeat) {
                    PerformAction(ActionId::TogglePreview);
                }
                return;
            case 'Z':
                if (shift) {
                    Redo();
                } else {
                    Undo();
                }
                return;
            case 'Y':
                Redo();
                return;
            case 'A':
                SourceSelectAll();
                return;
            case 'C':
                SourceCopy(false);
                return;
            case 'X':
                SourceCopy(true);
                return;
            case 'V':
                SourcePaste();
                return;
            case VK_LEFT:
                SourceMoveHorizontal(false, shift, true);
                return;
            case VK_RIGHT:
                SourceMoveHorizontal(true, shift, true);
                return;
            case VK_HOME:
                SourceMoveLineEdge(false, shift, true);
                return;
            case VK_END:
                SourceMoveLineEdge(true, shift, true);
                return;
            default:
                return;
        }
    }

    if (key == VK_ESCAPE) {
        LeaveSourceEditing();
        return;
    }

    switch (key) {
        case VK_LEFT:
            SourceMoveHorizontal(false, shift, false);
            return;
        case VK_RIGHT:
            SourceMoveHorizontal(true, shift, false);
            return;
        case VK_UP:
            SourceMoveVertical(false, shift);
            return;
        case VK_DOWN:
            SourceMoveVertical(true, shift);
            return;
        case VK_HOME:
            SourceMoveLineEdge(false, shift, false);
            return;
        case VK_END:
            SourceMoveLineEdge(true, shift, false);
            return;
        case VK_BACK:
            SourceBackspace();
            return;
        case VK_DELETE:
            SourceDeleteForward();
            return;
        case VK_RETURN:
            SourceInsertText(L"\n", false, L"source-newline");
            return;
        case VK_TAB:
            SourceInsertText(L"    ", true, L"source-typing");
            return;
        case VK_PRIOR:
            previewScroll_ = std::max(0.0f, previewScroll_ -
                                               ui_.previewBody.Height() * 0.8f);
            InvalidateRect(window_, nullptr, FALSE);
            return;
        case VK_NEXT:
            previewScroll_ = std::min(PreviewScrollRange(),
                                      previewScroll_ +
                                          ui_.previewBody.Height() * 0.8f);
            InvalidateRect(window_, nullptr, FALSE);
            return;
        default:
            return;
    }
}

}  // namespace mdgeui
