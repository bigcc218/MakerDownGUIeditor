#include "app.h"

#include "core/utf8.h"
#include "dialogs.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cwctype>

namespace mdgeui {
namespace {

constexpr std::size_t kMaxUndoEntries = 120;

std::size_t PreviousCodePoint(const std::wstring& text, std::size_t position) {
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

std::size_t NextCodePoint(const std::wstring& text, std::size_t position) {
    if (position >= text.size()) {
        return text.size();
    }
    const wchar_t first = text[position];
    if (first >= 0xD800 && first <= 0xDBFF && position + 1 < text.size()) {
        const wchar_t second = text[position + 1];
        if (second >= 0xDC00 && second <= 0xDFFF) {
            return position + 2;
        }
    }
    return position + 1;
}

bool IsSpaceCharacter(wchar_t ch) {
    return iswspace(static_cast<wint_t>(ch)) != 0;
}

bool IsWordCharacter(wchar_t ch) {
    return iswalnum(static_cast<wint_t>(ch)) != 0 || ch == L'_' ||
           ch == 0x200D || (ch >= 0x3400 && ch <= 0x9FFF);
}

std::size_t PreviousWordBoundary(const std::wstring& text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    std::size_t cursor = std::min(position, text.size());
    while (cursor > 0) {
        const std::size_t previous = PreviousCodePoint(text, cursor);
        if (!IsSpaceCharacter(text[previous])) {
            break;
        }
        cursor = previous;
    }
    if (cursor == 0) {
        return 0;
    }
    bool word = IsWordCharacter(text[PreviousCodePoint(text, cursor)]);
    while (cursor > 0) {
        const std::size_t previous = PreviousCodePoint(text, cursor);
        const bool thisWord = IsWordCharacter(text[previous]);
        if (IsSpaceCharacter(text[previous]) || thisWord != word) {
            break;
        }
        cursor = previous;
    }
    return cursor;
}

std::size_t NextWordBoundary(const std::wstring& text, std::size_t position) {
    if (position >= text.size()) {
        return text.size();
    }
    std::size_t cursor = position;
    while (cursor < text.size()) {
        const wchar_t ch = text[cursor];
        if (!IsSpaceCharacter(ch)) {
            break;
        }
        cursor = NextCodePoint(text, cursor);
    }
    if (cursor >= text.size()) {
        return text.size();
    }
    bool word = IsWordCharacter(text[cursor]);
    while (cursor < text.size()) {
        const wchar_t ch = text[cursor];
        if (IsSpaceCharacter(ch) || IsWordCharacter(ch) != word) {
            break;
        }
        cursor = NextCodePoint(text, cursor);
    }
    return cursor;
}

std::wstring ReadClipboardUnicode(HWND owner) {
    if (!OpenClipboard(owner)) {
        return {};
    }
    std::wstring result;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle));
        if (data) {
            result.assign(data);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

bool WriteClipboardUnicode(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return false;
    }
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    std::memcpy(target, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

bool GetLineBounds(IDWriteTextLayout* layout, std::size_t caret,
                   std::size_t textLength, std::size_t& lineStart,
                   std::size_t& lineEnd) {
    if (!layout) {
        return false;
    }
    UINT32 count = 0;
    if (FAILED(layout->GetLineMetrics(nullptr, 0, &count)) || count == 0) {
        return false;
    }
    std::vector<DWRITE_LINE_METRICS> metrics(count);
    if (FAILED(layout->GetLineMetrics(metrics.data(), count, &count)) || count == 0) {
        return false;
    }
    std::size_t start = 0;
    for (UINT32 i = 0; i < count; ++i) {
        const std::size_t end = start + metrics[i].length;
        const bool last = (i + 1 == count);
        if (caret < end || last) {
            lineStart = start;
            const std::size_t newline = metrics[i].newlineLength;
            const std::size_t contentEnd = end >= newline ? end - newline : end;
            lineEnd = std::min(textLength, contentEnd);
            return true;
        }
        start = end;
    }
    return false;
}

}  // namespace

std::size_t App::SelectionStart() const {
    const mdge::Block* block = ActiveBlock();
    const std::size_t length = block ? block->text.size() : 0;
    return std::min(std::min(selection_.caret, selection_.anchor), length);
}

std::size_t App::SelectionEnd() const {
    const mdge::Block* block = ActiveBlock();
    const std::size_t length = block ? block->text.size() : 0;
    return std::min(std::max(selection_.caret, selection_.anchor), length);
}

bool App::HasSelection() const {
    return ActiveBlockIsText() && SelectionStart() < SelectionEnd();
}

void App::NormalizeSelection() {
    if (document_.blocks.empty()) {
        selection_.block = 0;
        selection_.caret = 0;
        selection_.anchor = 0;
        return;
    }
    if (selection_.block < 0 ||
        static_cast<std::size_t>(selection_.block) >= document_.blocks.size()) {
        selection_.block = 0;
    }
    const mdge::Block& block =
        document_.blocks[static_cast<std::size_t>(selection_.block)];
    const std::size_t limit = block.text.size();
    selection_.caret = std::min(selection_.caret, limit);
    selection_.anchor = std::min(selection_.anchor, limit);
    if (!mdge::IsTextual(block.type)) {
        selection_.caret = 0;
        selection_.anchor = 0;
    }
}

void App::ClearSelection() {
    selection_.anchor = selection_.caret;
}

void App::SetActiveBlock(int index, std::size_t caret, bool extend) {
    if (document_.blocks.empty()) {
        selection_ = {};
        return;
    }
    index = std::max(0, std::min(index, static_cast<int>(document_.blocks.size()) - 1));
    const mdge::Block& block = document_.blocks[static_cast<std::size_t>(index)];
    const std::size_t limit = block.text.size();
    caret = std::min(caret, limit);
    const bool sameBlock = selection_.block == index;
    selection_.block = index;
    selection_.caret = caret;
    if (!extend) {
        selection_.anchor = caret;
    } else if (!sameBlock) {
        selection_.anchor = caret;
    } else {
        selection_.anchor = std::min(selection_.anchor, limit);
    }
    if (!mdge::IsTextual(block.type)) {
        selection_.caret = 0;
        selection_.anchor = 0;
    }
    lastUndoKey_.clear();
    preferredCaretX_ = -1.0f;
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
    FocusEditor();
    InvalidateRect(window_, nullptr, FALSE);
}

size_t App::CaretFromPoint(int blockIndex, float pageX, float pageY) {
    EnsurePageLayout();
    if (blockIndex < 0 ||
        static_cast<std::size_t>(blockIndex) >= document_.blocks.size()) {
        return 0;
    }
    const mdge::Block& block =
        document_.blocks[static_cast<std::size_t>(blockIndex)];
    if (!mdge::IsTextual(block.type)) {
        return 0;
    }
    for (const BlockVisual& visual : page_.blocks) {
        if (visual.index != static_cast<std::size_t>(blockIndex) ||
            !visual.textLayout) {
            continue;
        }
        const float localX = pageX - page_.pageLeft - visual.textRect.left;
        const float localY = pageY - PageTop() - visual.textRect.top;
        BOOL trailing = FALSE;
        BOOL inside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (FAILED(visual.textLayout->HitTestPoint(localX, localY, &trailing,
                                                   &inside, &metrics))) {
            return static_cast<size_t>(selection_.caret);
        }
        std::size_t caret = metrics.textPosition;
        if (trailing) {
            caret = std::min(block.text.size(), static_cast<std::size_t>(caret) + 1);
        }
        return std::min(caret, block.text.size());
    }
    return 0;
}

bool App::CaretLocalPosition(int blockIndex, std::size_t caret, float& x, float& y,
                             float& height) {
    EnsurePageLayout();
    if (blockIndex < 0 ||
        static_cast<std::size_t>(blockIndex) >= document_.blocks.size()) {
        return false;
    }
    const mdge::Block& block =
        document_.blocks[static_cast<std::size_t>(blockIndex)];
    if (!mdge::IsTextual(block.type)) {
        return false;
    }
    for (const BlockVisual& visual : page_.blocks) {
        if (visual.index != static_cast<std::size_t>(blockIndex) ||
            !visual.textLayout) {
            continue;
        }
        caret = std::min(caret, block.text.size());
        UINT32 layoutPosition = static_cast<UINT32>(caret);
        if (visual.layoutText.size() == 1 && block.text.empty()) {
            layoutPosition = 0;
        }
        DWRITE_HIT_TEST_METRICS metrics{};
        FLOAT pointX = 0.0f;
        FLOAT pointY = 0.0f;
        if (FAILED(visual.textLayout->HitTestTextPosition(
                layoutPosition, FALSE, &pointX, &pointY, &metrics))) {
            return false;
        }
        x = pointX;
        y = pointY;
        height = metrics.height > 0.0f ? metrics.height : 24.0f;
        return true;
    }
    return false;
}

void App::NotifyCaretMoved() {
    lastUndoKey_.clear();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::EnsureCaretVisible() {
    EnsurePageLayout();
    const BlockVisual* visual = ActiveVisual();
    if (!visual) {
        return;
    }
    const float viewTop = ui_.canvas.top + 66.0f;
    const float viewBottom = ui_.canvas.bottom - 44.0f;
    const float pageTop = PageTop();
    float delta = 0.0f;

    if (!ActiveBlockIsText() || !visual->textLayout) {
        const float blockTop = pageTop + visual->card.top;
        const float blockBottom = pageTop + visual->card.bottom;
        if (blockTop < viewTop) {
            delta = blockTop - viewTop;
        } else if (blockBottom > viewBottom) {
            delta = std::min(blockBottom - viewBottom,
                             std::max(0.0f, blockTop - viewTop));
        }
    } else {
        float localX = 0.0f;
        float localY = 0.0f;
        float caretHeight = 20.0f;
        if (!CaretLocalPosition(selection_.block, selection_.caret, localX, localY,
                                caretHeight)) {
            return;
        }
        const float caretTop = pageTop + visual->textRect.top + localY;
        const float caretBottom = caretTop + caretHeight;
        if (caretTop < viewTop) {
            delta = caretTop - viewTop;
        } else if (caretBottom > viewBottom) {
            delta = caretBottom - viewBottom;
        }
    }
    if (std::abs(delta) > 0.5f) {
        editorScroll_ = std::max(0.0f, editorScroll_ + delta);
        editorScroll_ = std::min(editorScroll_, EditorScrollRange());
    }
}

void App::InsertTextAtCaret(const std::wstring& text) {
    if (text.empty()) {
        return;
    }
    if (!ActiveBlockIsText()) {
        if (document_.blocks.empty()) {
            document_.blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
            selection_.block = 0;
        } else {
            int index = std::min(static_cast<int>(document_.blocks.size()),
                                 selection_.block + 1);
            document_.blocks.insert(
                document_.blocks.begin() + index,
                mdge::MakeBlock(mdge::BlockType::Paragraph));
            selection_.block = index;
        }
        selection_.caret = 0;
        selection_.anchor = 0;
    }
    if (HasSelection()) {
        DeleteSelection();
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    if (!mdge::IsTextual(block->type)) {
        return;
    }
    selection_.caret = std::min(selection_.caret, block->text.size());
    std::uint8_t style = 0;
    if (selection_.caret > 0 && selection_.caret <= block->styles.size()) {
        style = static_cast<std::uint8_t>(
            block->styles[selection_.caret - 1] &
            (mdge::StyleBold | mdge::StyleItalic | mdge::StyleCode));
    }
    mdge::InsertText(*block, selection_.caret, text, style);
    selection_.caret += text.size();
    selection_.anchor = selection_.caret;
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ToggleTaskBlock(int blockIndex) {
    if (blockIndex < 0 ||
        static_cast<std::size_t>(blockIndex) >= document_.blocks.size()) {
        return;
    }
    mdge::Block& block = document_.blocks[static_cast<std::size_t>(blockIndex)];
    if (!mdge::IsTask(block.type)) {
        return;
    }
    const bool wasChecked = block.type == mdge::BlockType::TaskChecked;
    PushUndo(wasChecked ? L"uncheck-task" : L"check-task", false);
    mdge::SetBlockType(block,
                       wasChecked ? mdge::BlockType::TaskUnchecked
                                  : mdge::BlockType::TaskChecked);
    const int previousBlock = selection_.block;
    selection_.block = blockIndex;
    if (previousBlock != blockIndex) {
        selection_.caret = 0;
        selection_.anchor = 0;
    }
    NormalizeSelection();
    lastUndoKey_.clear();
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
    FocusEditor();
    InvalidateRect(window_, nullptr, FALSE);
    ShowToast(wasChecked ? L"任务已恢复为未完成。" : L"任务已完成。");
}

void App::DeleteSelection() {
    if (!HasSelection()) {
        return;
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    const std::size_t start = SelectionStart();
    const std::size_t end = SelectionEnd();
    mdge::EraseText(*block, start, end);
    selection_.caret = start;
    selection_.anchor = start;
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
}

void App::BackspaceAtCaret() {
    if (!ActiveBlockIsText()) {
        DeleteActiveMediaBlock();
        return;
    }
    if (HasSelection()) {
        PushUndo(L"delete", true);
        DeleteSelection();
        return;
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    if (selection_.caret > 0) {
        const std::size_t previous =
            PreviousCodePoint(block->text, selection_.caret);
        PushUndo(L"delete", true);
        mdge::EraseText(*block, previous, selection_.caret);
        selection_.caret = previous;
        selection_.anchor = previous;
        preferredCaretX_ = -1.0f;
        MarkDocumentDirty();
        InvalidateEditorState();
        EnsurePageLayout();
        EnsureCaretVisible();
        ScheduleCaretBlink();
        return;
    }
    if (selection_.block <= 0) {
        return;
    }
    const int currentIndex = selection_.block;
    const int previousIndex = currentIndex - 1;
    mdge::Block& current = document_.blocks[static_cast<std::size_t>(currentIndex)];
    mdge::Block& previous =
        document_.blocks[static_cast<std::size_t>(previousIndex)];
    if (!mdge::IsTextual(previous.type)) {
        return;
    }
    PushUndo(L"merge", false);
    const std::size_t oldSize = previous.text.size();
    mdge::MergeBlock(previous, current);
    document_.blocks.erase(document_.blocks.begin() + currentIndex);
    selection_.block = previousIndex;
    selection_.caret = oldSize;
    selection_.anchor = oldSize;
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
}

void App::DeleteForward() {
    if (!ActiveBlockIsText()) {
        DeleteActiveMediaBlock();
        return;
    }
    if (HasSelection()) {
        PushUndo(L"delete", true);
        DeleteSelection();
        return;
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    if (selection_.caret < block->text.size()) {
        const std::size_t next = NextCodePoint(block->text, selection_.caret);
        PushUndo(L"delete", true);
        mdge::EraseText(*block, selection_.caret, next);
        preferredCaretX_ = -1.0f;
        MarkDocumentDirty();
        InvalidateEditorState();
        EnsurePageLayout();
        EnsureCaretVisible();
        ScheduleCaretBlink();
        return;
    }
    if (selection_.block + 1 >= static_cast<int>(document_.blocks.size())) {
        return;
    }
    mdge::Block& next = document_.blocks[static_cast<std::size_t>(selection_.block + 1)];
    if (!mdge::IsTextual(next.type)) {
        return;
    }
    PushUndo(L"merge", false);
    mdge::MergeBlock(*block, next);
    document_.blocks.erase(document_.blocks.begin() + selection_.block + 1);
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
}

void App::SplitAtCaret() {
    if (!ActiveBlockIsText()) {
        PushUndo(L"enter", false);
        const int index = std::min(static_cast<int>(document_.blocks.size()),
                                   selection_.block + 1);
        document_.blocks.insert(document_.blocks.begin() + index,
                                mdge::MakeBlock(mdge::BlockType::Paragraph));
        selection_.block = index;
        selection_.caret = 0;
        selection_.anchor = 0;
        MarkDocumentDirty();
        InvalidateEditorState();
        EnsurePageLayout();
        EnsureCaretVisible();
        ScheduleCaretBlink();
        return;
    }
    PushUndo(L"enter", false);
    if (HasSelection()) {
        DeleteSelection();
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    if (block->type == mdge::BlockType::Code) {
        InsertTextAtCaret(L"\n");
        return;
    }
    if (block->text.empty() &&
        (mdge::IsList(block->type) || block->type == mdge::BlockType::Quote)) {
        mdge::SetBlockType(*block, mdge::BlockType::Paragraph);
        MarkDocumentDirty();
        InvalidateEditorState();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    const int currentIndex = selection_.block;
    const std::size_t caret = selection_.caret;
    mdge::Block right = mdge::SplitBlock(*block, caret);
    mdge::BlockType newType = mdge::BlockType::Paragraph;
    if (block->type == mdge::BlockType::Bullet) {
        newType = mdge::BlockType::Bullet;
    } else if (block->type == mdge::BlockType::Numbered) {
        newType = mdge::BlockType::Numbered;
    } else if (mdge::IsTask(block->type)) {
        newType = mdge::BlockType::TaskUnchecked;
    } else if (block->type == mdge::BlockType::Quote) {
        newType = mdge::BlockType::Quote;
    }
    mdge::SetBlockType(right, newType);
    document_.blocks.insert(document_.blocks.begin() + currentIndex + 1,
                            std::move(right));
    selection_.block = currentIndex + 1;
    selection_.caret = 0;
    selection_.anchor = 0;
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
}

void App::MergeWithPreviousBlock() {
    BackspaceAtCaret();
}

void App::MoveCaretHorizontal(bool right, bool extend, bool byWord) {
    if (!ActiveBlockIsText()) {
        return;
    }
    if (HasSelection() && !extend) {
        selection_.caret = right ? SelectionEnd() : SelectionStart();
        selection_.anchor = selection_.caret;
        preferredCaretX_ = -1.0f;
        NotifyCaretMoved();
        return;
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    std::size_t caret = selection_.caret;
    if (byWord) {
        caret = right ? NextWordBoundary(block->text, caret)
                      : PreviousWordBoundary(block->text, caret);
    } else {
        caret = right ? NextCodePoint(block->text, caret)
                      : PreviousCodePoint(block->text, caret);
    }

    if (caret == selection_.caret && !byWord) {
        if (right) {
            if (selection_.block + 1 < static_cast<int>(document_.blocks.size())) {
                if (extend) {
                    return;
                }
                for (int i = selection_.block + 1;
                     i < static_cast<int>(document_.blocks.size()); ++i) {
                    if (mdge::IsTextual(
                            document_.blocks[static_cast<std::size_t>(i)].type)) {
                        selection_.block = i;
                        selection_.caret = 0;
                        selection_.anchor = 0;
                        preferredCaretX_ = -1.0f;
                        NotifyCaretMoved();
                        return;
                    }
                }
            }
        } else {
            if (selection_.block > 0) {
                if (extend) {
                    return;
                }
                for (int i = selection_.block - 1; i >= 0; --i) {
                    if (mdge::IsTextual(
                            document_.blocks[static_cast<std::size_t>(i)].type)) {
                        selection_.block = i;
                        selection_.caret =
                            document_.blocks[static_cast<std::size_t>(i)].text.size();
                        selection_.anchor = selection_.caret;
                        preferredCaretX_ = -1.0f;
                        NotifyCaretMoved();
                        return;
                    }
                }
            }
        }
        return;
    }
    selection_.caret = caret;
    if (!extend) {
        selection_.anchor = caret;
    }
    preferredCaretX_ = -1.0f;
    NotifyCaretMoved();
}

void App::MoveCaretVertical(bool down, bool extend) {
    if (!ActiveBlockIsText()) {
        if (document_.blocks.empty()) {
            return;
        }
        int target = selection_.block + (down ? 1 : -1);
        while (target >= 0 && target < static_cast<int>(document_.blocks.size()) &&
               !mdge::IsTextual(document_.blocks[static_cast<std::size_t>(target)].type)) {
            target += down ? 1 : -1;
        }
        if (target >= 0 && target < static_cast<int>(document_.blocks.size())) {
            selection_.block = target;
            selection_.caret = 0;
            selection_.anchor = 0;
            NotifyCaretMoved();
        }
        return;
    }
    EnsurePageLayout();
    const BlockVisual* visual = ActiveVisual();
    mdge::Block* block = ActiveBlock();
    if (!visual || !block || !visual->textLayout) {
        return;
    }
    if (HasSelection() && !extend) {
        selection_.caret = down ? SelectionEnd() : SelectionStart();
        selection_.anchor = selection_.caret;
    }

    FLOAT caretX = 0.0f;
    FLOAT caretY = 0.0f;
    DWRITE_HIT_TEST_METRICS caretMetrics{};
    const UINT32 layoutPosition = static_cast<UINT32>(
        std::min(selection_.caret, visual->layoutText.size()));
    if (FAILED(visual->textLayout->HitTestTextPosition(
            layoutPosition, FALSE, &caretX, &caretY, &caretMetrics))) {
        return;
    }
    if (preferredCaretX_ < 0.0f) {
        preferredCaretX_ = caretX;
    }

    DWRITE_TEXT_METRICS textMetrics{};
    visual->textLayout->GetMetrics(&textMetrics);
    const float lineHeight =
        caretMetrics.height > 0.0f ? caretMetrics.height : 24.0f;
    const bool atTop = caretY <= 0.5f;
    const bool atBottom = caretY + lineHeight >= textMetrics.height - 1.5f;

    if (!down && atTop) {
        if (extend) {
            return;
        }
        for (int i = selection_.block - 1; i >= 0; --i) {
            const mdge::Block& previous =
                document_.blocks[static_cast<std::size_t>(i)];
            if (!mdge::IsTextual(previous.type)) {
                continue;
            }
            selection_.block = i;
            // Place the caret near the same horizontal position on the last
            // line of the previous block.
            EnsurePageLayout();
            const BlockVisual* previousVisual = nullptr;
            for (const BlockVisual& candidate : page_.blocks) {
                if (candidate.index == static_cast<std::size_t>(i)) {
                    previousVisual = &candidate;
                    break;
                }
            }
            if (previousVisual && previousVisual->textLayout) {
                BOOL trailing = FALSE;
                BOOL inside = FALSE;
                DWRITE_HIT_TEST_METRICS hit{};
                previousVisual->textLayout->HitTestPoint(
                    preferredCaretX_, 100000.0f, &trailing, &inside, &hit);
                std::size_t caret = hit.textPosition;
                if (trailing) {
                    caret = std::min(previous.text.size(),
                                     static_cast<std::size_t>(caret) + 1);
                }
                selection_.caret = std::min(caret, previous.text.size());
            } else {
                selection_.caret = previous.text.size();
            }
            selection_.anchor = selection_.caret;
            NotifyCaretMoved();
            return;
        }
        return;
    }
    if (down && atBottom) {
        if (extend) {
            return;
        }
        for (int i = selection_.block + 1;
             i < static_cast<int>(document_.blocks.size()); ++i) {
            const mdge::Block& next = document_.blocks[static_cast<std::size_t>(i)];
            if (!mdge::IsTextual(next.type)) {
                continue;
            }
            selection_.block = i;
            EnsurePageLayout();
            const BlockVisual* nextVisual = nullptr;
            for (const BlockVisual& candidate : page_.blocks) {
                if (candidate.index == static_cast<std::size_t>(i)) {
                    nextVisual = &candidate;
                    break;
                }
            }
            if (nextVisual && nextVisual->textLayout) {
                BOOL trailing = FALSE;
                BOOL inside = FALSE;
                DWRITE_HIT_TEST_METRICS hit{};
                nextVisual->textLayout->HitTestPoint(preferredCaretX_, 0.0f,
                                                     &trailing, &inside, &hit);
                std::size_t caret = hit.textPosition;
                if (trailing) {
                    caret = std::min(next.text.size(),
                                     static_cast<std::size_t>(caret) + 1);
                }
                selection_.caret = std::min(caret, next.text.size());
            } else {
                selection_.caret = 0;
            }
            selection_.anchor = selection_.caret;
            NotifyCaretMoved();
            return;
        }
        return;
    }

    const float targetY = down ? caretY + lineHeight + 1.0f : caretY - 1.0f;
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    if (FAILED(visual->textLayout->HitTestPoint(preferredCaretX_, targetY, &trailing,
                                                &inside, &hit))) {
        return;
    }
    std::size_t newCaret = hit.textPosition;
    if (trailing) {
        newCaret = std::min(block->text.size(),
                            static_cast<std::size_t>(newCaret) + 1);
    }
    selection_.caret = std::min(newCaret, block->text.size());
    if (!extend) {
        selection_.anchor = selection_.caret;
    }
    NotifyCaretMoved();
}

void App::MoveCaretToAdjacentBlock(bool down, float preferredX) {
    if (document_.blocks.empty()) {
        return;
    }
    int index = selection_.block + (down ? 1 : -1);
    while (index >= 0 && index < static_cast<int>(document_.blocks.size()) &&
           !mdge::IsTextual(document_.blocks[static_cast<std::size_t>(index)].type)) {
        index += down ? 1 : -1;
    }
    if (index < 0 || index >= static_cast<int>(document_.blocks.size())) {
        return;
    }
    selection_.block = index;
    EnsurePageLayout();
    const BlockVisual* visual = nullptr;
    for (const BlockVisual& candidate : page_.blocks) {
        if (candidate.index == static_cast<std::size_t>(index)) {
            visual = &candidate;
            break;
        }
    }
    if (!visual || !visual->textLayout) {
        selection_.caret = 0;
    } else {
        BOOL trailing = FALSE;
        BOOL inside = FALSE;
        DWRITE_HIT_TEST_METRICS hit{};
        visual->textLayout->HitTestPoint(preferredX, down ? 0.0f : 100000.0f,
                                         &trailing, &inside, &hit);
        std::size_t caret = hit.textPosition;
        if (trailing) {
            caret = std::min(document_.blocks[static_cast<std::size_t>(index)].text.size(),
                             static_cast<std::size_t>(caret) + 1);
        }
        selection_.caret = caret;
    }
    selection_.anchor = selection_.caret;
    NotifyCaretMoved();
}

void App::MoveCaretLineEdge(bool end, bool extend, bool documentEdge) {
    if (!ActiveBlockIsText()) {
        return;
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    std::size_t newCaret = selection_.caret;
    if (documentEdge) {
        newCaret = end ? block->text.size() : 0;
    } else {
        EnsurePageLayout();
        BlockVisual* visual = ActiveVisual();
        if (!visual || !visual->textLayout) {
            return;
        }
        std::size_t lineStart = 0;
        std::size_t lineEnd = 0;
        if (GetLineBounds(visual->textLayout.Get(), selection_.caret,
                          block->text.size(), lineStart, lineEnd)) {
            newCaret = end ? lineEnd : lineStart;
        }
    }
    selection_.caret = std::min(newCaret, block->text.size());
    if (!extend) {
        selection_.anchor = selection_.caret;
    }
    preferredCaretX_ = -1.0f;
    NotifyCaretMoved();
}

void App::SelectAllInBlock() {
    mdge::Block* block = ActiveBlock();
    if (!block || !mdge::IsTextual(block->type)) {
        return;
    }
    selection_.anchor = 0;
    selection_.caret = block->text.size();
    NotifyCaretMoved();
}

void App::SelectWord(int blockIndex, float pageX, float pageY) {
    if (blockIndex < 0 ||
        static_cast<std::size_t>(blockIndex) >= document_.blocks.size()) {
        return;
    }
    const mdge::Block& block =
        document_.blocks[static_cast<std::size_t>(blockIndex)];
    if (!mdge::IsTextual(block.type)) {
        return;
    }
    const std::size_t caret = CaretFromPoint(blockIndex, pageX, pageY);
    std::size_t start = caret;
    std::size_t end = caret;
    if (start < block.text.size()) {
        const wchar_t anchor = block.text[start];
        const bool word = IsWordCharacter(anchor);
        const bool space = IsSpaceCharacter(anchor);
        while (start > 0) {
            const std::size_t prev = PreviousCodePoint(block.text, start);
            const wchar_t ch = block.text[prev];
            if ((word && IsWordCharacter(ch)) ||
                (space && IsSpaceCharacter(ch)) ||
                (!word && !space && !IsWordCharacter(ch) && !IsSpaceCharacter(ch))) {
                start = prev;
            } else {
                break;
            }
        }
        while (end < block.text.size()) {
            const wchar_t ch = block.text[end];
            if ((word && IsWordCharacter(ch)) ||
                (space && IsSpaceCharacter(ch)) ||
                (!word && !space && !IsWordCharacter(ch) && !IsSpaceCharacter(ch))) {
                end = NextCodePoint(block.text, end);
            } else {
                break;
            }
        }
    } else if (start > 0) {
        const std::size_t prev = PreviousCodePoint(block.text, start);
        if (IsWordCharacter(block.text[prev])) {
            while (start > 0) {
                const std::size_t before = PreviousCodePoint(block.text, start);
                if (!IsWordCharacter(block.text[before])) {
                    break;
                }
                start = before;
            }
            end = caret;
        }
    }
    selection_.block = blockIndex;
    selection_.anchor = start;
    selection_.caret = std::max(end, start);
    preferredCaretX_ = -1.0f;
    FocusEditor();
    NotifyCaretMoved();
}

void App::DeleteActiveMediaBlock() {
    const mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    if (mdge::IsTextual(block->type)) {
        return;
    }
    DeleteActiveBlock(true);
}

void App::DeleteActiveBlock(bool keepOneEmptyBlock) {
    if (selection_.block < 0 ||
        static_cast<std::size_t>(selection_.block) >= document_.blocks.size()) {
        return;
    }
    PushUndo(L"delete-block", false);
    const int removed = selection_.block;
    document_.blocks.erase(document_.blocks.begin() + removed);
    if (document_.blocks.empty()) {
        if (keepOneEmptyBlock) {
            document_.blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
        }
    }
    if (document_.blocks.empty()) {
        selection_ = {};
    } else {
        const int nextIndex =
            std::min(removed, static_cast<int>(document_.blocks.size()) - 1);
        selection_.block = std::max(0, nextIndex);
        selection_.caret = 0;
        selection_.anchor = 0;
    }
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    ClampScrollPositions();
    EnsureCaretVisible();
    ScheduleCaretBlink();
    UpdateWindowIconFromProject();
}

void App::ApplyBlockStyle(mdge::BlockType type) {
    mdge::Block* block = ActiveBlock();
    if (!block || !mdge::IsTextual(block->type) || block->type == type) {
        return;
    }
    PushUndo(L"block-style", false);
    mdge::SetBlockType(*block, type);
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    EnsureCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ApplyInlineFromToolbar(ActionId action) {
    if (!HasSelection()) {
        ShowToast(L"请先选择文字，再应用行内样式。", true);
        return;
    }
    mdge::Block* block = ActiveBlock();
    if (!block || !mdge::IsTextual(block->type)) {
        return;
    }
    const std::size_t start = SelectionStart();
    const std::size_t end = SelectionEnd();
    if (action == ActionId::InlineLink) {
        EditSelectedLink();
        return;
    }
    std::uint8_t flag = mdge::StyleBold;
    if (action == ActionId::InlineItalic) {
        flag = mdge::StyleItalic;
    } else if (action == ActionId::InlineCode) {
        flag = mdge::StyleCode;
    }
    PushUndo(L"inline-style", false);
    const bool enabled = !mdge::HasStyleInRange(*block, start, end, flag);
    mdge::ApplyInlineStyle(*block, start, end, flag, enabled);
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    EnsureCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::EditSelectedLink() {
    if (!HasSelection()) {
        ShowToast(L"请先选择文字，再设置链接。", true);
        return;
    }
    mdge::Block* block = ActiveBlock();
    if (!block || !mdge::IsTextual(block->type)) {
        return;
    }
    // 记录块下标与选区：ShowLinkDialog 是模态对话框，期间文档可能被重新
    // 分配，拿着的 Block* 会失效，所以对话框返回后按下标重新取一次。
    const std::size_t blockIndex = selection_.block;
    const std::size_t start = SelectionStart();
    const std::size_t end = SelectionEnd();
    const std::wstring selected = block->text.substr(start, end - start);
    const std::wstring currentHref = mdge::LinkAt(*block, start);
    const LinkDialogResult result = ShowLinkDialog(window_, selected, currentHref);
    SetFocus(window_);
    if (!result.accepted) {
        return;
    }
    if (blockIndex >= document_.blocks.size()) {
        return;
    }
    block = &document_.blocks[blockIndex];
    const std::size_t safeEnd = std::min(end, block->text.size());
    if (!mdge::IsTextual(block->type) || start >= safeEnd) {
        return;
    }
    PushUndo(L"link", false);
    mdge::ApplyLink(*block, start, safeEnd, result.href);
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    EnsureCaretVisible();
    InvalidateRect(window_, nullptr, FALSE);
    if (result.href.empty()) {
        ShowToast(L"已移除链接。");
    } else {
        ShowToast(L"链接已更新。");
    }
}

std::wstring App::SelectedText() const {
    if (!HasSelection()) {
        return {};
    }
    const mdge::Block* block = ActiveBlock();
    if (!block) {
        return {};
    }
    return block->text.substr(SelectionStart(), SelectionEnd() - SelectionStart());
}

void App::CopySelectionToClipboard(bool cut) {
    if (!HasSelection()) {
        return;
    }
    const std::wstring selected = SelectedText();
    if (selected.empty()) {
        return;
    }
    if (!SetClipboardText(selected)) {
        ShowToast(L"无法写入剪贴板。", true);
        return;
    }
    if (cut) {
        PushUndo(L"cut", false);
        DeleteSelection();
        ShowToast(L"已剪切到剪贴板。");
    } else {
        ShowToast(L"已复制到剪贴板。");
    }
}

bool App::SetClipboardText(const std::wstring& text) {
    return WriteClipboardUnicode(window_, text);
}

std::wstring App::GetClipboardText() {
    return ReadClipboardUnicode(window_);
}

void App::PasteFromClipboard() {
    std::wstring clip = GetClipboardText();
    if (clip.empty()) {
        return;
    }
    if (clip.size() > 4u * 1024u * 1024u) {
        ShowToast(L"剪贴板内容过大，已取消粘贴。", true);
        return;
    }
    // Normalize all line endings to LF.
    std::wstring normalized;
    normalized.reserve(clip.size());
    for (std::size_t i = 0; i < clip.size(); ++i) {
        if (clip[i] == L'\r') {
            if (i + 1 < clip.size() && clip[i + 1] == L'\n') {
                ++i;
            }
            normalized.push_back(L'\n');
        } else {
            normalized.push_back(clip[i]);
        }
    }

    PushUndo(L"paste", false);
    if (!ActiveBlockIsText()) {
        if (document_.blocks.empty()) {
            document_.blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
            selection_ = {};
        } else {
            const int index = std::min(static_cast<int>(document_.blocks.size()),
                                       selection_.block + 1);
            document_.blocks.insert(document_.blocks.begin() + index,
                                    mdge::MakeBlock(mdge::BlockType::Paragraph));
            selection_.block = index;
            selection_.caret = 0;
            selection_.anchor = 0;
        }
    }
    mdge::Block* block = ActiveBlock();
    if (!block) {
        return;
    }
    const std::size_t newline = normalized.find(L'\n');
    if (newline == std::wstring::npos) {
        InsertTextAtCaret(normalized);
        return;
    }
    if (HasSelection()) {
        DeleteSelection();
    }
    block = ActiveBlock();
    if (!block) {
        return;
    }
    const std::size_t caret = std::min(selection_.caret, block->text.size());
    const std::uint8_t inheritedStyle =
        caret > 0 && caret <= block->styles.size()
            ? static_cast<std::uint8_t>(block->styles[caret - 1] &
                                        (mdge::StyleBold | mdge::StyleItalic |
                                         mdge::StyleCode))
            : 0;
    mdge::BlockType continuationType = mdge::BlockType::Paragraph;
    if (block->type == mdge::BlockType::Bullet ||
        block->type == mdge::BlockType::Numbered ||
        block->type == mdge::BlockType::Quote ||
        block->type == mdge::BlockType::Code) {
        // 代码块也要沿用自身类型：否则多行粘贴会把后续几行变成普通段落，
        // 代码内容被拆散、行内样式也随之中断。
        continuationType = block->type;
    } else if (mdge::IsTask(block->type)) {
        continuationType = mdge::BlockType::TaskUnchecked;
    }
    const std::wstring continuationLanguage = block->language;
    const int currentIndex = selection_.block;
    mdge::Block tail = mdge::SplitBlock(*block, caret);

    std::vector<std::wstring> lines;
    std::size_t lineStart = 0;
    while (lineStart <= normalized.size()) {
        const std::size_t lineEnd = normalized.find(L'\n', lineStart);
        lines.push_back(normalized.substr(
            lineStart, lineEnd == std::wstring::npos ? std::wstring::npos
                                                      : lineEnd - lineStart));
        if (lineEnd == std::wstring::npos) {
            break;
        }
        lineStart = lineEnd + 1;
        if (lines.size() > 2000) {
            break;
        }
    }
    if (lines.empty()) {
        lines.push_back(L"");
    }
    if (continuationType == mdge::BlockType::Code &&
        lines.size() > 1) {
        // 代码块里的粘贴保留原样：用换行把各行接进同一个块，而不是拆块。
        std::wstring joined = lines[0];
        for (std::size_t i = 1; i < lines.size(); ++i) {
            joined += L'\n';
            joined += lines[i];
        }
        mdge::InsertText(document_.blocks[static_cast<std::size_t>(currentIndex)],
                         document_.blocks[static_cast<std::size_t>(currentIndex)].text.size(),
                         joined, 0);
        const std::size_t caretAfterCode =
            document_.blocks[static_cast<std::size_t>(currentIndex)].text.size();
        if (!tail.text.empty()) {
            mdge::MergeBlock(document_.blocks[static_cast<std::size_t>(currentIndex)],
                             tail);
        }
        selection_.block = currentIndex;
        selection_.caret = caretAfterCode;
        selection_.anchor = caretAfterCode;
        MarkDocumentDirty();
        InvalidateEditorState();
        NormalizeSelection();
        EnsurePageLayout();
        EnsureCaretVisible();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }

    mdge::Block& first = document_.blocks[static_cast<std::size_t>(currentIndex)];
    mdge::InsertText(first, first.text.size(), lines[0], inheritedStyle);
    int insertAt = currentIndex + 1;
    int lastIndex = currentIndex;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        mdge::Block added = mdge::MakeBlock(continuationType);
        added.language = continuationLanguage;
        if (!lines[i].empty()) {
            mdge::InsertText(added, 0, lines[i], 0);
        }
        document_.blocks.insert(document_.blocks.begin() + insertAt,
                                std::move(added));
        lastIndex = insertAt;
        ++insertAt;
    }
    std::size_t caretAfterPaste =
        document_.blocks[static_cast<std::size_t>(lastIndex)].text.size();
    if (!tail.text.empty()) {
        mdge::Block& last = document_.blocks[static_cast<std::size_t>(lastIndex)];
        // 光标应停在粘贴内容之后、被合并的尾巴之前。
        caretAfterPaste = last.text.size();
        mdge::MergeBlock(last, tail);
    }
    selection_.block = lastIndex;
    selection_.caret = caretAfterPaste;
    selection_.anchor = caretAfterPaste;
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    NormalizeSelection();
    EnsurePageLayout();
    EnsureCaretVisible();
    ScheduleCaretBlink();
    InvalidateRect(window_, nullptr, FALSE);
}

EditorSnapshot App::CaptureSnapshot() const {
    EditorSnapshot snapshot;
    snapshot.document = document_;
    snapshot.selection = selection_;
    snapshot.sourceFocused = sourceFocused_;
    if (sourceFocused_) {
        snapshot.sourceText = markdownSource_;
        snapshot.sourceCaret = sourceCaret_;
        snapshot.sourceAnchor = sourceAnchor_;
    }
    return snapshot;
}

void App::PushUndo(const std::wstring& key, bool coalesce) {
    const auto now = std::chrono::steady_clock::now();
    if (coalesce && !undoStack_.empty() && key == lastUndoKey_ &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUndoTime_)
                .count() < 900) {
        lastUndoTime_ = now;
        return;
    }
    undoStack_.push_back(CaptureSnapshot());
    while (undoStack_.size() > kMaxUndoEntries) {
        undoStack_.pop_front();
    }
    redoStack_.clear();
    lastUndoKey_ = key;
    lastUndoTime_ = now;
}

void App::Undo() {
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(CaptureSnapshot());
    EditorSnapshot snapshot = std::move(undoStack_.back());
    undoStack_.pop_back();
    RestoreSnapshot(snapshot);
    lastUndoKey_.clear();
    ShowToast(L"已撤销。");
}

void App::Redo() {
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(CaptureSnapshot());
    EditorSnapshot snapshot = std::move(redoStack_.back());
    redoStack_.pop_back();
    RestoreSnapshot(snapshot);
    lastUndoKey_.clear();
    ShowToast(L"已重做。");
}

void App::RestoreSnapshot(const EditorSnapshot& snapshot) {
    document_ = snapshot.document;
    mdge::NormalizeDocument(document_);
    selection_ = snapshot.selection;
    if (document_.blocks.empty()) {
        document_.blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
        selection_ = {};
    }
    sourceFocused_ = snapshot.sourceFocused;
    if (sourceFocused_) {
        markdownSource_ = snapshot.sourceText;
        sourceCaret_ = std::min(snapshot.sourceCaret, markdownSource_.size());
        sourceAnchor_ = std::min(snapshot.sourceAnchor, markdownSource_.size());
        markdownDirty_ = false;
        preferredSourceX_ = -1.0f;
    } else {
        sourceCaret_ = 0;
        sourceAnchor_ = 0;
        markdownDirty_ = true;
    }
    if (sourceFocused_ && (!previewVisible_ || previewTab_ != 0)) {
        sourceFocused_ = false;
        markdownDirty_ = true;
    }
    NormalizeSelection();
    imageCache_.clear();
    InvalidatePage();
    InvalidateSourceLayout();
    UpdateWindowTitle();
    UpdateWindowIconFromProject();
    EnsurePageLayout();
    if (sourceFocused_) {
        BuildSourceLines();
        EnsureSourceCaretVisible();
    } else {
        EnsureCaretVisible();
    }
    ScheduleCaretBlink();
    FocusEditor();
    InvalidateRect(window_, nullptr, FALSE);
    MarkDocumentDirty();
}

void App::ClearUndoHistory() {
    undoStack_.clear();
    redoStack_.clear();
    lastUndoKey_.clear();
    lastUndoTime_ = {};
}

void App::FocusEditor() {
    if (window_ && GetFocus() != window_) {
        SetFocus(window_);
    }
    hasFocus_ = true;
}

}  // namespace mdgeui
