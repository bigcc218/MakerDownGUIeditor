#include "app.h"

#include "core/path_utils.h"
#include "dialogs.h"

#include <imm.h>
#include <windowsx.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>

namespace mdgeui {
namespace {

bool KeyDown(int virtualKey) {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

}  // namespace

ButtonSpec* App::FindButton(float x, float y) {
    for (ButtonSpec& button : ui_.headerButtons) {
        if (button.rect.Contains(x, y)) {
            return &button;
        }
    }
    for (ButtonSpec& button : ui_.formatButtons) {
        if (button.rect.Contains(x, y)) {
            return &button;
        }
    }
    for (ButtonSpec& button : ui_.previewButtons) {
        if (button.rect.Contains(x, y)) {
            return &button;
        }
    }
    return nullptr;
}

ActionId App::HitAction(float x, float y) const {
    for (const ButtonSpec& button : ui_.headerButtons) {
        if (button.rect.Contains(x, y)) {
            return button.action;
        }
    }
    for (const ButtonSpec& button : ui_.formatButtons) {
        if (button.rect.Contains(x, y)) {
            return button.action;
        }
    }
    for (const ButtonSpec& button : ui_.previewButtons) {
        if (button.rect.Contains(x, y)) {
            return button.action;
        }
    }
    if (ui_.docCard.Contains(x, y)) {
        return ActionId::DocumentSettings;
    }
    return ActionId::None;
}

int App::HitPalette(float x, float y) const {
    for (std::size_t i = 0; i < ui_.paletteItems.size(); ++i) {
        if (ui_.paletteItems[i].rect.Contains(x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int App::HitBlockAt(float x, float y) const {
    if (!ui_.canvas.Contains(x, y) || !page_.valid) {
        return -1;
    }
    const float localX = x - page_.pageLeft;
    const float localY = y - PageTop();
    for (const BlockVisual& visual : page_.blocks) {
        if (visual.card.Contains(localX, localY)) {
            return static_cast<int>(visual.index);
        }
    }
    return -1;
}

int App::HitGrip(float x, float y) const {
    if (!ui_.canvas.Contains(x, y) || !page_.valid) {
        return -1;
    }
    const float localX = x - page_.pageLeft;
    const float localY = y - PageTop();
    for (const BlockVisual& visual : page_.blocks) {
        if (visual.gripRect.Contains(localX, localY)) {
            return static_cast<int>(visual.index);
        }
    }
    return -1;
}

int App::HitTaskCheckbox(float x, float y) const {
    if (!ui_.canvas.Contains(x, y) || !page_.valid) {
        return -1;
    }
    const float localX = x - page_.pageLeft;
    const float localY = y - PageTop();
    for (const BlockVisual& visual : page_.blocks) {
        if (!mdge::IsTask(visual.type)) {
            continue;
        }
        const RectF box = TaskCheckboxRect(visual);
        if (box.Width() <= 0.0f) {
            continue;
        }
        const RectF hit = MakeRect(box.left - 4.0f, box.top - 4.0f,
                                   box.right + 4.0f, box.bottom + 4.0f);
        if (hit.Contains(localX, localY)) {
            return static_cast<int>(visual.index);
        }
    }
    return -1;
}

int App::HitDropIndex(float y) const {
    if (!page_.valid) {
        return static_cast<int>(document_.blocks.size());
    }
    const float localY = y - PageTop();
    for (const BlockVisual& visual : page_.blocks) {
        const float center = (visual.card.top + visual.card.bottom) * 0.5f;
        if (localY < center) {
            return static_cast<int>(visual.index);
        }
    }
    return static_cast<int>(page_.blocks.size());
}

int App::HitOutlineRow(float x, float y) const {
    if (!previewVisible_ || previewTab_ != 1 ||
        !ui_.previewBody.Contains(x, y)) {
        return -1;
    }
    const std::vector<OutlineRow> rows = BuildOutlineRows();
    const float localY = y - ui_.previewBody.top;
    for (const OutlineRow& row : rows) {
        if (localY >= row.y && localY < row.y + row.height) {
            return static_cast<int>(row.blockIndex);
        }
    }
    return -1;
}

void App::SetCursorForHover() {
    LPWSTR cursorName = IDC_ARROW;
    if (drag_.kind == DragState::Kind::Block ||
        drag_.kind == DragState::Kind::Palette) {
        cursorName = IDC_SIZEALL;
    } else if (cursorKind_ == 1) {
        cursorName = IDC_IBEAM;
    } else if (cursorKind_ == 2) {
        cursorName = IDC_HAND;
    } else if (cursorKind_ == 3) {
        cursorName = IDC_SIZEALL;
    }
    SetCursor(LoadCursorW(nullptr, cursorName));
}

void App::UpdateHover(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    mouseInside_ = ui_.client.Contains(x, y);
    const ActionId oldButton = hoveredButton_;
    const int oldBlock = hoveredBlock_;
    const int oldGrip = hoveredGrip_;
    const int oldPalette = hoveredPalette_;
    const int oldOutline = hoveredOutline_;
    const bool oldDocCard = hoveredDocCard_;
    const int oldCursor = cursorKind_;

    hoveredButton_ = ActionId::None;
    hoveredBlock_ = -1;
    hoveredGrip_ = -1;
    hoveredPalette_ = -1;
    hoveredOutline_ = -1;
    hoveredDocCard_ = false;
    cursorKind_ = 0;

    if (!mouseInside_) {
        SetCursorForHover();
        if (oldButton != hoveredButton_ || oldBlock != hoveredBlock_ ||
            oldGrip != hoveredGrip_ || oldPalette != hoveredPalette_ ||
            oldOutline != hoveredOutline_ || oldDocCard != hoveredDocCard_ ||
            oldCursor != cursorKind_) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        return;
    }

    if (ButtonSpec* button = FindButton(x, y)) {
        hoveredButton_ = button->action;
        cursorKind_ = ActionEnabled(button->action) ? 2 : 0;
    } else if (ui_.docCard.Contains(x, y)) {
        hoveredDocCard_ = true;
        cursorKind_ = 2;
    } else {
        const int palette = HitPalette(x, y);
        if (palette >= 0) {
            hoveredPalette_ = palette;
            cursorKind_ = 2;
        } else {
            if (previewVisible_ && ui_.canvasScrollTrack.Contains(x, y) &&
                EditorScrollRange() > 0.0f) {
                cursorKind_ = 2;
            } else if (previewVisible_ && ui_.previewScrollTrack.Contains(x, y) &&
                       PreviewScrollRange() > 0.0f) {
                cursorKind_ = 2;
            } else if (previewTab_ == 0 &&
                       ui_.previewBody.Contains(x, y)) {
                cursorKind_ = 1;
            } else if (previewTab_ == 1 && HitOutlineRow(x, y) >= 0) {
                hoveredOutline_ = HitOutlineRow(x, y);
                cursorKind_ = 2;
            } else {
                const int grip = HitGrip(x, y);
                if (grip >= 0) {
                    hoveredGrip_ = grip;
                    cursorKind_ = 3;
                } else if (const int task = HitTaskCheckbox(x, y); task >= 0) {
                    hoveredBlock_ = task;
                    cursorKind_ = 2;
                } else {
                    const int block = HitBlockAt(x, y);
                    if (block >= 0) {
                        hoveredBlock_ = block;
                        const mdge::Block& data =
                            document_.blocks[static_cast<std::size_t>(block)];
                        cursorKind_ = mdge::IsTextual(data.type) ? 1 : 0;
                    }
                }
            }
        }
    }

    if (oldButton != hoveredButton_ || oldBlock != hoveredBlock_ ||
        oldGrip != hoveredGrip_ || oldPalette != hoveredPalette_ ||
        oldOutline != hoveredOutline_ || oldDocCard != hoveredDocCard_ ||
        oldCursor != cursorKind_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
    SetCursorForHover();
}

void App::OnMouseMove(float x, float y, WPARAM keys) {
    mouseX_ = x;
    mouseY_ = y;
    mouseInside_ = ui_.client.Contains(x, y);

    if (drag_.kind == DragState::Kind::SourceSelect) {
        if (y < ui_.previewBody.top + 26.0f) {
            previewScroll_ = std::max(0.0f, previewScroll_ - 12.0f);
        } else if (y > ui_.previewBody.bottom - 26.0f) {
            previewScroll_ =
                std::min(PreviewScrollRange(), previewScroll_ + 12.0f);
        }
        sourceCaret_ = SourceCaretFromPoint(x, y);
        preferredSourceX_ = -1.0f;
        ScheduleCaretBlink();
        cursorKind_ = 1;
        SetCursorForHover();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }

    if (drag_.kind == DragState::Kind::TextSelect) {
        const float dx = x - drag_.startX;
        const float dy = y - drag_.startY;
        if (dx * dx + dy * dy > 16.0f) {
            drag_.moved = true;
        }
        float delta = 0.0f;
        if (y < ui_.canvas.top + 34.0f) {
            delta = y - (ui_.canvas.top + 34.0f);
        } else if (y > ui_.canvas.bottom - 34.0f) {
            delta = y - (ui_.canvas.bottom - 34.0f);
        }
        if (std::abs(delta) > 0.5f) {
            editorScroll_ = std::clamp(editorScroll_ + delta, 0.0f,
                                       EditorScrollRange());
        }
        if (mdge::Block* block = ActiveBlock();
            block && mdge::IsTextual(block->type)) {
            EnsurePageLayout();
            const BlockVisual* visual = ActiveVisual();
            std::size_t caret = selection_.caret;
            if (visual) {
                const float localY = y - PageTop();
                if (localY < visual->textRect.top) {
                    caret = 0;
                } else if (localY > visual->textRect.bottom) {
                    caret = block->text.size();
                } else {
                    caret = CaretFromPoint(selection_.block, x, y);
                }
            }
            selection_.caret = caret;
            preferredCaretX_ = -1.0f;
            ScheduleCaretBlink();
            InvalidateRect(window_, nullptr, FALSE);
        }
        cursorKind_ = 1;
        SetCursorForHover();
        return;
    }

    if (drag_.kind == DragState::Kind::Block ||
        drag_.kind == DragState::Kind::Palette) {
        const float dx = x - drag_.startX;
        const float dy = y - drag_.startY;
        if (dx * dx + dy * dy > 12.0f) {
            drag_.moved = true;
        }
        if (drag_.moved) {
            UpdateDragTarget(x, y);
            InvalidateRect(window_, nullptr, FALSE);
        }
        cursorKind_ = 3;
        SetCursorForHover();
        return;
    }

    if (drag_.kind == DragState::Kind::CanvasScroll) {
        const float delta = y - drag_.startY;
        const float track = std::max(1.0f, ui_.canvasScrollTrack.Height());
        const float target = drag_.scrollStart +
                             delta / track * std::max(1.0f, EditorScrollRange());
        editorScroll_ = std::clamp(target, 0.0f, EditorScrollRange());
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (drag_.kind == DragState::Kind::PreviewScroll) {
        const float delta = y - drag_.startY;
        const float track = std::max(1.0f, ui_.previewScrollTrack.Height());
        const float target = drag_.scrollStart +
                             delta / track * std::max(1.0f, PreviewScrollRange());
        previewScroll_ = std::clamp(target, 0.0f, PreviewScrollRange());
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }

    UpdateHover(x, y);
    (void)keys;
}

void App::StartBlockDrag(int blockIndex) {
    if (blockIndex < 0 ||
        static_cast<std::size_t>(blockIndex) >= document_.blocks.size()) {
        return;
    }
    drag_ = DragState{};
    drag_.kind = DragState::Kind::Block;
    drag_.sourceIndex = blockIndex;
    drag_.startX = mouseX_;
    drag_.startY = mouseY_;
    drag_.overCanvas = false;
    drag_.insertIndex = -1;
    SetCapture(window_);
    selection_.block = blockIndex;
    selection_.caret = 0;
    selection_.anchor = 0;
    preferredCaretX_ = -1.0f;
    ScheduleCaretBlink();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::UpdateDragTarget(float x, float y) {
    drag_.overCanvas = ui_.canvas.Contains(x, y);
    if (!drag_.overCanvas) {
        drag_.insertIndex = -1;
        return;
    }
    if (std::abs(editorScroll_) < 0.01f && page_.pageHeight > ui_.canvas.Height()) {
        // UpdateDragTarget can be called before the first page layout; ensure
        // there is a layout to measure against.
        EnsurePageLayout();
    }
    if (y < ui_.canvas.top + 40.0f) {
        editorScroll_ = std::max(0.0f, editorScroll_ - 10.0f);
    } else if (y > ui_.canvas.bottom - 40.0f) {
        editorScroll_ =
            std::min(EditorScrollRange(), editorScroll_ + 10.0f);
    }
    EnsurePageLayout();
    drag_.insertIndex = HitDropIndex(y);
}

void App::FinishDrag(bool canceled) {
    if (drag_.kind == DragState::Kind::None) {
        return;
    }
    const DragState::Kind kind = drag_.kind;
    if (!canceled) {
        if (kind == DragState::Kind::Block && drag_.overCanvas &&
            drag_.insertIndex >= 0) {
            const int source = drag_.sourceIndex;
            int target = drag_.insertIndex;
            if (source >= 0 &&
                source < static_cast<int>(document_.blocks.size())) {
                if (target > source) {
                    --target;
                }
                target = std::clamp(target, 0,
                                    static_cast<int>(document_.blocks.size()) - 1);
                if (target != source) {
                    PushUndo(L"block-order", false);
                    mdge::Block moved = std::move(document_.blocks[source]);
                    document_.blocks.erase(document_.blocks.begin() + source);
                    document_.blocks.insert(document_.blocks.begin() + target,
                                             std::move(moved));
                    selection_.block = target;
                    selection_.caret = 0;
                    selection_.anchor = 0;
                    MarkDocumentDirty();
                    InvalidateEditorState();
                    EnsurePageLayout();
                    EnsureCaretVisible();
                }
            }
        } else if (kind == DragState::Kind::Palette && drag_.overCanvas &&
                   drag_.insertIndex >= 0) {
            InsertBlock(drag_.paletteType, drag_.insertIndex);
        }
    }
    drag_ = DragState{};
    ReleaseCapture();
    InvalidateRect(window_, nullptr, FALSE);
    SetFocus(window_);
}

void App::InsertBlock(mdge::BlockType type, int atIndex) {
    if (type == mdge::BlockType::Image) {
        InsertImageBlockFromDialog(atIndex);
        return;
    }
    if (document_.blocks.empty()) {
        atIndex = 0;
    }
    atIndex = std::max(0, std::min(atIndex, static_cast<int>(document_.blocks.size())));
    PushUndo(L"insert-block", false);
    mdge::Block block = mdge::MakeBlock(type);
    document_.blocks.insert(document_.blocks.begin() + atIndex, std::move(block));
    selection_.block = atIndex;
    selection_.caret = 0;
    selection_.anchor = 0;
    preferredCaretX_ = -1.0f;
    MarkDocumentDirty();
    InvalidateEditorState();
    EnsurePageLayout();
    EnsureCaretVisible();
    ClampScrollPositions();
    ScheduleCaretBlink();
    FocusEditor();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::OnLeftButtonDown(float x, float y, WPARAM keys) {
    (void)keys;
    UpdateHover(x, y);
    EnsurePageLayout();
    if (drag_.kind != DragState::Kind::None) {
        return;
    }
    if (ButtonSpec* button = FindButton(x, y)) {
        const ActionId action = button->action;
        PerformAction(action);
        return;
    }
    if (ui_.docCard.Contains(x, y)) {
        ShowDocumentSettings();
        return;
    }
    if (const int palette = HitPalette(x, y); palette >= 0) {
        LeaveSourceEditing();
        drag_ = DragState{};
        drag_.kind = DragState::Kind::Palette;
        drag_.sourceIndex = palette;
        drag_.paletteType = ui_.paletteItems[static_cast<std::size_t>(palette)].type;
        drag_.startX = x;
        drag_.startY = y;
        SetCapture(window_);
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (ui_.canvasScrollTrack.Contains(x, y) && EditorScrollRange() > 0.0f) {
        LeaveSourceEditing();
        drag_ = DragState{};
        drag_.kind = DragState::Kind::CanvasScroll;
        drag_.startX = x;
        drag_.startY = y;
        drag_.scrollStart = editorScroll_;
        SetCapture(window_);
        return;
    }
    if (previewVisible_ && ui_.previewScrollTrack.Contains(x, y) &&
        PreviewScrollRange() > 0.0f) {
        drag_ = DragState{};
        drag_.kind = DragState::Kind::PreviewScroll;
        drag_.startX = x;
        drag_.startY = y;
        drag_.scrollStart = previewScroll_;
        SetCapture(window_);
        return;
    }
    if (previewVisible_ && previewTab_ == 0 &&
        ui_.previewBody.Contains(x, y) &&
        !ui_.previewScrollTrack.Contains(x, y)) {
        BeginSourceEditing(x, y, KeyDown(VK_SHIFT));
        drag_ = DragState{};
        drag_.kind = DragState::Kind::SourceSelect;
        drag_.startX = x;
        drag_.startY = y;
        SetCapture(window_);
        cursorKind_ = 1;
        SetCursorForHover();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (previewTab_ == 1) {
        const int outline = HitOutlineRow(x, y);
        if (outline >= 0) {
            const float pageTop = PageTop();
            selection_.block = outline;
            selection_.caret = 0;
            selection_.anchor = 0;
            preferredCaretX_ = -1.0f;
            EnsurePageLayout();
            const BlockVisual* visual = nullptr;
            for (const BlockVisual& candidate : page_.blocks) {
                if (candidate.index == static_cast<std::size_t>(outline)) {
                    visual = &candidate;
                    break;
                }
            }
            if (visual) {
                const float top = pageTop + visual->card.top;
                if (top < ui_.canvas.top + 30.0f ||
                    top > ui_.canvas.bottom - 100.0f) {
                    editorScroll_ = std::clamp(
                        visual->card.top - 70.0f, 0.0f, EditorScrollRange());
                }
            }
            FocusEditor();
            ScheduleCaretBlink();
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
    }
    if (const int grip = HitGrip(x, y); grip >= 0) {
        LeaveSourceEditing();
        StartBlockDrag(grip);
        return;
    }
    if (const int taskIndex = HitTaskCheckbox(x, y); taskIndex >= 0) {
        LeaveSourceEditing();
        ToggleTaskBlock(taskIndex);
        return;
    }
    const int blockIndex = HitBlockAt(x, y);
    if (blockIndex >= 0) {
        LeaveSourceEditing();
        const mdge::Block& block =
            document_.blocks[static_cast<std::size_t>(blockIndex)];
        if (!mdge::IsTextual(block.type)) {
            selection_.block = blockIndex;
            selection_.caret = 0;
            selection_.anchor = 0;
            preferredCaretX_ = -1.0f;
            FocusEditor();
            ScheduleCaretBlink();
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (KeyDown(VK_CONTROL)) {
            const std::size_t hit =
                CaretFromPoint(blockIndex, x, y);
            const std::wstring href = mdge::LinkAt(block, hit);
            if (!href.empty() &&
                (href.rfind(L"http://", 0) == 0 || href.rfind(L"https://", 0) == 0 ||
                 href.rfind(L"mailto:", 0) == 0)) {
                ShellExecuteW(window_, L"open", href.c_str(), nullptr, nullptr,
                              SW_SHOWNORMAL);
                return;
            }
        }
        const bool extend = KeyDown(VK_SHIFT) && selection_.block == blockIndex;
        SetActiveBlock(blockIndex, CaretFromPoint(blockIndex, x, y), extend);
        drag_ = DragState{};
        drag_.kind = DragState::Kind::TextSelect;
        drag_.startX = x;
        drag_.startY = y;
        SetCapture(window_);
        ScheduleCaretBlink();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    LeaveSourceEditing();
    FocusEditor();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::OnLeftButtonUp(float x, float y, WPARAM keys) {
    (void)keys;
    if (drag_.kind == DragState::Kind::SourceSelect) {
        sourceCaret_ = SourceCaretFromPoint(x, y);
        drag_ = DragState{};
        ReleaseCapture();
        ScheduleCaretBlink();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (drag_.kind == DragState::Kind::Palette) {
        const bool moved = drag_.moved;
        if (!moved) {
            int at = selection_.block + 1;
            if (selection_.block < 0 ||
                static_cast<std::size_t>(selection_.block) >=
                    document_.blocks.size()) {
                at = static_cast<int>(document_.blocks.size());
            }
            const mdge::BlockType type = drag_.paletteType;
            drag_ = DragState{};
            ReleaseCapture();
            InsertBlock(type, at);
            return;
        }
        UpdateDragTarget(x, y);
        FinishDrag(false);
        return;
    }
    if (drag_.kind == DragState::Kind::Block) {
        UpdateDragTarget(x, y);
        FinishDrag(!drag_.overCanvas || !drag_.moved);
        return;
    }
    if (drag_.kind == DragState::Kind::TextSelect ||
        drag_.kind == DragState::Kind::CanvasScroll ||
        drag_.kind == DragState::Kind::PreviewScroll) {
        drag_ = DragState{};
        ReleaseCapture();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
}

void App::OnLeftButtonDoubleClick(float x, float y, WPARAM keys) {
    (void)keys;
    if (previewVisible_ && previewTab_ == 0 &&
        ui_.previewBody.Contains(x, y) &&
        !ui_.previewScrollTrack.Contains(x, y)) {
        SelectSourceWordAt(x, y);
        return;
    }
    EnsurePageLayout();
    if (HitTaskCheckbox(x, y) >= 0) {
        return;
    }
    const int blockIndex = HitBlockAt(x, y);
    if (blockIndex < 0) {
        return;
    }
    mdge::Block& block = document_.blocks[static_cast<std::size_t>(blockIndex)];
    if (block.type == mdge::BlockType::Image) {
        const std::wstring picked = PickImageFile(window_, L"更换图片");
        SetFocus(window_);
        if (picked.empty()) {
            return;
        }
        PushUndo(L"replace-image", false);
        block.imagePath = StoreImagePath(picked);
        block.caption = mdge::FileStem(picked);
        MarkDocumentDirty();
        InvalidateEditorState();
        EnsurePageLayout();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (mdge::IsTextual(block.type)) {
        SelectWord(blockIndex, x, y);
    }
}

void App::OnMouseWheel(float x, float y, int wheelDelta) {
    if (wheelDelta == 0) {
        return;
    }
    const float amount =
        -static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA) * 62.0f;
    if (previewVisible_ && ui_.preview.Contains(x, y)) {
        previewScroll_ =
            std::clamp(previewScroll_ + amount, 0.0f, PreviewScrollRange());
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (ui_.canvas.Contains(x, y)) {
        editorScroll_ =
            std::clamp(editorScroll_ + amount, 0.0f, EditorScrollRange());
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void App::OnMouseLeave() {
    mouseInside_ = false;
    if (drag_.kind == DragState::Kind::None) {
        hoveredButton_ = ActionId::None;
        hoveredBlock_ = -1;
        hoveredGrip_ = -1;
        hoveredPalette_ = -1;
        hoveredOutline_ = -1;
        hoveredDocCard_ = false;
        cursorKind_ = 0;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void App::OnKeyDown(WPARAM key, LPARAM data) {
    const bool ctrl = KeyDown(VK_CONTROL);
    const bool shift = KeyDown(VK_SHIFT);
    const bool alt = KeyDown(VK_MENU);
    const bool repeat = (data & (1 << 30)) != 0;
    (void)alt;

    if (sourceFocused_) {
        OnSourceKeyDown(key, data);
        return;
    }

    if (!ctrl && !shift && !alt && !compositionText_.empty()) {
        if (key == VK_BACK || key == VK_DELETE || key == VK_RETURN ||
            key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN) {
            return;
        }
    }

    if (ctrl) {
        ActionId action = ActionId::None;
        switch (key) {
            case VK_RETURN:
                if (!repeat && ActiveBlockIsTask()) {
                    ToggleTaskBlock(selection_.block);
                }
                return;
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
            case 'B':
                action = ActionId::InlineBold;
                break;
            case 'I':
                action = ActionId::InlineItalic;
                break;
            case 'K':
                action = ActionId::InlineLink;
                break;
            case 'A':
                SelectAllInBlock();
                return;
            case 'C':
                CopySelectionToClipboard(false);
                return;
            case 'X':
                CopySelectionToClipboard(true);
                return;
            case 'V':
                PasteFromClipboard();
                return;
            case '1':
                ApplyBlockStyle(mdge::BlockType::Heading1);
                return;
            case '2':
                ApplyBlockStyle(mdge::BlockType::Heading2);
                return;
            case '3':
                ApplyBlockStyle(mdge::BlockType::Heading3);
                return;
            case '0':
                ApplyBlockStyle(mdge::BlockType::Paragraph);
                return;
            case VK_OEM_3:
                action = ActionId::InlineCode;
                break;
            case 'R':
                if (shift) {
                    TriggerTextReveal();
                    InvalidateRect(window_, nullptr, FALSE);
                    ShowToast(L"文字内容以 8 ms 从左侧向右侧显示。");
                    return;
                }
                break;
            default:
                break;
        }
        if (action != ActionId::None) {
            ApplyInlineFromToolbar(action);
            return;
        }
    }

    if (key == VK_ESCAPE) {
        if (drag_.kind != DragState::Kind::None) {
            FinishDrag(true);
            ReleaseCapture();
        } else {
            ClearSelection();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return;
    }

    const bool extend = shift;
    switch (key) {
        case VK_LEFT:
            MoveCaretHorizontal(false, extend, ctrl);
            return;
        case VK_RIGHT:
            MoveCaretHorizontal(true, extend, ctrl);
            return;
        case VK_UP:
            MoveCaretVertical(false, extend);
            return;
        case VK_DOWN:
            MoveCaretVertical(true, extend);
            return;
        case VK_HOME:
            MoveCaretLineEdge(false, extend, ctrl);
            return;
        case VK_END:
            MoveCaretLineEdge(true, extend, ctrl);
            return;
        case VK_BACK:
            BackspaceAtCaret();
            return;
        case VK_DELETE:
            DeleteForward();
            return;
        case VK_RETURN:
            if (shift) {
                if (ActiveBlockIsText()) {
                    PushUndo(L"soft-enter", false);
                    InsertTextAtCaret(L"\n");
                }
            } else {
                SplitAtCaret();
            }
            return;
        case VK_TAB:
            if (ActiveBlockIsText()) {
                const mdge::Block* block = ActiveBlock();
                if (block && block->type == mdge::BlockType::Code) {
                    PushUndo(L"typing", true);
                    InsertTextAtCaret(L"    ");
                }
            }
            return;
        case VK_PRIOR:
            editorScroll_ =
                std::clamp(editorScroll_ - ui_.canvas.Height() * 0.8f, 0.0f,
                           EditorScrollRange());
            InvalidateRect(window_, nullptr, FALSE);
            return;
        case VK_NEXT:
            editorScroll_ =
                std::clamp(editorScroll_ + ui_.canvas.Height() * 0.8f, 0.0f,
                           EditorScrollRange());
            InvalidateRect(window_, nullptr, FALSE);
            return;
        default:
            break;
    }
}

void App::OnChar(wchar_t character) {
    if (character < 0x20 || character == 0x7F) {
        return;
    }
    if (sourceFocused_) {
        SourceInsertText(std::wstring(1, character), true, L"source-typing");
        return;
    }
    if (!ActiveBlockIsText() && !document_.blocks.empty()) {
        // Typing while a media block is selected should add a paragraph
        // instead of silently swallowing the key.
    }
    PushUndo(L"typing", true);
    InsertTextAtCaret(std::wstring(1, character));
}

void App::UpdateImeCompositionWindow() {
    HIMC context = ImmGetContext(window_);
    if (!context) {
        return;
    }
    POINT point{};
    if (sourceFocused_) {
        int lineIndex = 0;
        float localX = 0.0f;
        float localY = 0.0f;
        float height = 18.0f;
        if (SourceCaretMetrics(sourceCaret_, lineIndex, localX, localY, height) &&
            lineIndex >= 0 &&
            static_cast<std::size_t>(lineIndex) < sourceLines_.size()) {
            const float x = ui_.previewBody.left + 38.0f + localX;
            const float y = ui_.previewBody.top + 14.0f -
                            previewScroll_ +
                            sourceLines_[static_cast<std::size_t>(lineIndex)].offsetY +
                            localY;
            point.x = static_cast<LONG>(std::lround(x * dpi_ / 96.0f));
            point.y = static_cast<LONG>(std::lround(y * dpi_ / 96.0f));
        } else {
            point.x = static_cast<LONG>(std::lround(
                (ui_.previewBody.left + 38.0f) * dpi_ / 96.0f));
            point.y = static_cast<LONG>(std::lround(
                (ui_.previewBody.top + 20.0f) * dpi_ / 96.0f));
        }
    } else {
        if (!ActiveBlockIsText()) {
            ImmReleaseContext(window_, context);
            return;
        }
        EnsurePageLayout();
        const BlockVisual* visual = ActiveVisual();
        float localX = 0.0f;
        float localY = 0.0f;
        float height = 22.0f;
        CaretLocalPosition(selection_.block, selection_.caret, localX, localY,
                           height);
        if (visual) {
            const float x = page_.pageLeft + visual->textRect.left + localX;
            const float y = PageTop() + visual->textRect.top + localY;
            point.x = static_cast<LONG>(std::lround(x * dpi_ / 96.0f));
            point.y = static_cast<LONG>(std::lround(y * dpi_ / 96.0f));
        }
    }
    ClientToScreen(window_, &point);

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = point;
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = point;
    candidate.ptCurrentPos.y +=
        static_cast<LONG>(std::lround(26.0f * dpi_ / 96.0f));
    ImmSetCandidateWindow(context, &candidate);

    LOGFONTW logFont{};
    logFont.lfHeight = -static_cast<LONG>(std::lround(18.0f * dpi_ / 96.0f));
    logFont.lfWeight = FW_NORMAL;
    logFont.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(logFont.lfFaceName, L"Microsoft YaHei UI",
              static_cast<int>(sizeof(logFont.lfFaceName) / sizeof(wchar_t)));
    ImmSetCompositionFontW(context, &logFont);
    ImmReleaseContext(window_, context);
}

void App::OnImeComposition(WPARAM wparam, LPARAM lparam) {
    (void)wparam;
    HIMC context = ImmGetContext(window_);
    if (!context) {
        return;
    }
    if (lparam & GCS_RESULTSTR) {
        const LONG bytes =
            ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
        if (bytes > 0) {
            std::wstring result(static_cast<std::size_t>(bytes) / sizeof(wchar_t),
                                L'\0');
            ImmGetCompositionStringW(context, GCS_RESULTSTR, result.data(), bytes);
            compositionText_.clear();
            if (!result.empty()) {
                if (sourceFocused_) {
                    SourceInsertText(result, false, L"source-ime");
                } else {
                    PushUndo(L"ime", false);
                    InsertTextAtCaret(result);
                }
            }
        }
    }
    if (lparam & GCS_COMPSTR) {
        const LONG bytes =
            ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
        if (bytes > 0) {
            compositionText_.assign(static_cast<std::size_t>(bytes) / sizeof(wchar_t),
                                    L'\0');
            ImmGetCompositionStringW(context, GCS_COMPSTR, compositionText_.data(),
                                     bytes);
        } else {
            compositionText_.clear();
        }
    }
    ImmReleaseContext(window_, context);
    UpdateImeCompositionWindow();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ResetComposition() {
    compositionText_.clear();
}

}  // namespace mdgeui
