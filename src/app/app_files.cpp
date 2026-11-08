#include "app.h"

#include "core/file_utils.h"
#include "core/markdown.h"
#include "core/path_utils.h"
#include "core/project_io.h"
#include "core/utf8.h"
#include "dialogs.h"
#include "resource.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>

namespace mdgeui {
namespace {

std::uint64_t NowUnixSeconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::wstring EnsureExtension(const std::wstring& path, const std::wstring& extension) {
    std::wstring lowerExtension = mdge::FileExtensionLower(path);
    if (lowerExtension == extension) {
        return path;
    }
    return path + extension;
}

std::wstring DefaultProjectName(const std::wstring& title) {
    return mdge::SanitizeFileStem(title.empty() ? L"未命名文档" : title) + L".mdge";
}

std::wstring DefaultMarkdownName(const std::wstring& title) {
    return mdge::SanitizeFileStem(title.empty() ? L"未命名文档" : title) + L".md";
}

}  // namespace

void App::InitializeBlankDocument() {
    document_ = mdge::MakeStarterDocument();
    const std::uint64_t now = NowUnixSeconds();
    document_.created = now;
    document_.modified = now;
    projectPath_.clear();
    markdownPath_.clear();
    dirty_ = false;
    previewTab_ = 0;
    previewScroll_ = 0.0f;
    editorScroll_ = 0.0f;
    sourceLines_.clear();
    sourceTotalHeight_ = 0.0f;
    markdownSource_.clear();
    markdownDirty_ = true;
    sourceLayoutDirty_ = true;
    sourceFocused_ = false;
    sourceCaret_ = 0;
    sourceAnchor_ = 0;
    preferredSourceX_ = -1.0f;
    imageCache_.clear();
    ClearUndoHistory();
    selection_.block = 0;
    selection_.caret = 0;
    selection_.anchor = 0;
    preferredCaretX_ = -1.0f;
    InvalidatePage();
    UpdateWindowTitle();
    UpdateWindowIconFromProject();
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void App::MarkDocumentDirty() {
    if (!dirty_) {
        dirty_ = true;
        UpdateWindowTitle();
    }
}

bool App::OpenProjectFile(const std::wstring& path, bool showErrors,
                          bool skipPrompt) {
    if (!skipPrompt && !MaybeSaveBeforeDestructiveAction()) {
        return false;
    }
    if (path.empty()) {
        return false;
    }
    mdge::Document loaded;
    std::wstring error;
    if (!mdge::LoadProjectFile(path, loaded, error)) {
        if (showErrors) {
            MessageBoxW(window_, error.c_str(), L"无法打开项目",
                        MB_OK | MB_ICONERROR);
        }
        return false;
    }
    if (loaded.blocks.empty()) {
        loaded.blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
    }
    mdge::NormalizeDocument(loaded);
    if (loaded.title.empty()) {
        loaded.title = mdge::FileStem(path);
        if (loaded.title.empty()) {
            loaded.title = L"未命名文档";
        }
    }

    document_ = std::move(loaded);
    projectPath_ = mdge::GetAbsolutePath(path);
    markdownPath_.clear();
    dirty_ = false;
    previewTab_ = 0;
    previewScroll_ = 0.0f;
    editorScroll_ = 0.0f;
    markdownDirty_ = true;
    sourceFocused_ = false;
    sourceCaret_ = 0;
    sourceAnchor_ = 0;
    preferredSourceX_ = -1.0f;
    imageCache_.clear();
    InvalidateSourceLayout();
    InvalidatePage();
    ClearUndoHistory();
    selection_.block = 0;
    selection_.caret = 0;
    selection_.anchor = 0;
    for (std::size_t i = 0; i < document_.blocks.size(); ++i) {
        if (mdge::IsTextual(document_.blocks[i].type)) {
            selection_.block = static_cast<int>(i);
            break;
        }
    }
    preferredCaretX_ = -1.0f;
    UpdateWindowTitle();
    UpdateWindowIconFromProject();
    TriggerTextReveal();
    if (window_) {
        RecalculateUi();
        InvalidateRect(window_, nullptr, FALSE);
    }
    return true;
}

bool App::OpenMarkdownFile(const std::wstring& path, bool showErrors,
                           bool skipPrompt) {
    if (!skipPrompt && !MaybeSaveBeforeDestructiveAction()) {
        return false;
    }
    if (path.empty()) {
        return false;
    }

    std::vector<std::uint8_t> bytes;
    std::wstring error;
    if (!mdge::ReadFileBytes(path, bytes, error)) {
        if (showErrors) {
            MessageBoxW(window_, error.c_str(), L"无法打开 Markdown 文件",
                        MB_OK | MB_ICONERROR);
        }
        return false;
    }
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF) {
        bytes.erase(bytes.begin(), bytes.begin() + 3);
    }
    const std::string encoded(bytes.begin(), bytes.end());
    std::wstring source;
    if (!mdge::Utf8Decode(encoded, source, &error)) {
        if (showErrors) {
            MessageBoxW(window_,
                        (L"Markdown 文件不是有效的 UTF-8 文本。\n\n" + error).c_str(),
                        L"无法打开 Markdown 文件", MB_OK | MB_ICONERROR);
        }
        return false;
    }

    std::vector<mdge::Block> blocks = mdge::MarkdownToBlocks(source);
    if (blocks.empty()) {
        blocks.push_back(mdge::MakeBlock(mdge::BlockType::Paragraph));
    }

    mdge::Document loaded;
    loaded.title = mdge::FileStem(path);
    if (loaded.title.empty()) {
        loaded.title = L"未命名文档";
    }
    loaded.blocks = std::move(blocks);
    loaded.created = NowUnixSeconds();
    loaded.modified = loaded.created;
    mdge::NormalizeDocument(loaded);

    document_ = std::move(loaded);
    projectPath_.clear();
    markdownPath_ = mdge::GetAbsolutePath(path);
    dirty_ = false;
    previewTab_ = 0;
    previewScroll_ = 0.0f;
    editorScroll_ = 0.0f;
    markdownSource_ = std::move(source);
    markdownDirty_ = false;
    sourceLines_.clear();
    sourceTotalHeight_ = 0.0f;
    sourceLayoutDirty_ = true;
    sourceLayoutWidth_ = -1.0f;
    sourceFocused_ = false;
    sourceCaret_ = 0;
    sourceAnchor_ = 0;
    preferredSourceX_ = -1.0f;
    imageCache_.clear();
    ClearUndoHistory();
    selection_.block = 0;
    selection_.caret = 0;
    selection_.anchor = 0;
    for (std::size_t i = 0; i < document_.blocks.size(); ++i) {
        if (mdge::IsTextual(document_.blocks[i].type)) {
            selection_.block = static_cast<int>(i);
            break;
        }
    }
    preferredCaretX_ = -1.0f;
    UpdateWindowTitle();
    UpdateWindowIconFromProject();
    TriggerTextReveal();
    if (window_) {
        RecalculateUi();
        InvalidateRect(window_, nullptr, FALSE);
    }
    ShowToast(L"已导入 Markdown 文件；保存项目会创建 .mdge 文件。");
    return true;
}

void App::NewProject() {
    if (!MaybeSaveBeforeDestructiveAction()) {
        return;
    }
    InitializeBlankDocument();
    TriggerTextReveal();
    SetFocus(window_);
    InvalidateRect(window_, nullptr, FALSE);
    ShowToast(L"已新建空白项目。");
}

void App::OpenProject() {
    if (!MaybeSaveBeforeDestructiveAction()) {
        return;
    }
    const std::wstring path = PickProjectToOpen(window_);
    if (path.empty()) {
        return;
    }
    if (OpenProjectFile(path, true, true)) {
        ShowToast(L"项目已打开。");
    }
    SetFocus(window_);
}

bool App::SaveProject(bool saveAs) {
    std::wstring path = projectPath_;
    if (saveAs || path.empty()) {
        const std::wstring picked =
            PickProjectSavePath(window_, DefaultProjectName(document_.title));
        if (picked.empty()) {
            return false;
        }
        path = EnsureExtension(picked, L".mdge");
    }
    if (path.empty()) {
        return false;
    }
    if (document_.created == 0) {
        document_.created = NowUnixSeconds();
    }
    document_.modified = NowUnixSeconds();
    std::wstring error;
    if (!mdge::SaveProjectFile(path, document_, error)) {
        MessageBoxW(window_,
                    (L"项目保存失败。\n\n" + error).c_str(), L"保存项目",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    const bool pathChanged =
        _wcsicmp(projectPath_.c_str(), path.c_str()) != 0;
    projectPath_ = mdge::GetAbsolutePath(path);
    markdownPath_.clear();
    dirty_ = false;
    if (pathChanged) {
        imageCache_.clear();
        InvalidatePage();
        InvalidateSourceLayout();
    }
    UpdateWindowTitle();
    UpdateWindowIconFromProject();
    ShowToast(L"项目已保存。");
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void App::ExportMarkdown() {
    CurrentMarkdownSource();
    const std::wstring picked =
        PickMarkdownExportPath(window_, DefaultMarkdownName(document_.title));
    if (picked.empty()) {
        return;
    }
    const std::wstring path = EnsureExtension(picked, L".md");
    std::wstring error;
    if (!mdge::WriteUtf8TextFile(path, mdge::Utf8Encode(markdownSource_), error)) {
        MessageBoxW(window_,
                    (L"Markdown 导出失败。\n\n" + error).c_str(), L"导出 Markdown",
                    MB_OK | MB_ICONERROR);
        return;
    }
    ShowToast(L"Markdown 已导出。");
    SetForegroundWindow(window_);
}

bool App::MaybeSaveBeforeDestructiveAction() {
    if (!dirty_) {
        return true;
    }
    const int choice = MessageBoxW(
        window_,
        L"当前项目有尚未保存的修改。\n\n是否在继续之前保存项目？",
        L"MDGE Studio", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDYES) {
        return SaveProject(false);
    }
    return choice == IDNO;
}

std::wstring App::ProjectDirectory() const {
    return mdge::ProjectDirectoryFor(BaseDocumentPath());
}

std::wstring App::BaseDocumentPath() const {
    return projectPath_.empty() ? markdownPath_ : projectPath_;
}

void App::ShowDocumentSettings() {
    DocumentSettings current;
    current.title = document_.title;
    current.author = document_.author;
    current.iconPath = document_.iconPath;
    DocumentSettings updated;
    if (!ShowDocumentSettingsDialog(window_, current, BaseDocumentPath(),
                                    updated)) {
        SetFocus(window_);
        return;
    }
    if (updated.title == document_.title && updated.author == document_.author &&
        updated.iconPath == document_.iconPath) {
        SetFocus(window_);
        return;
    }
    PushUndo(L"document-settings", false);
    document_.title = updated.title;
    document_.author = updated.author;
    document_.iconPath = updated.iconPath;
    document_.modified = NowUnixSeconds();
    MarkDocumentDirty();
    imageCache_.clear();
    InvalidatePage();
    UpdateWindowTitle();
    UpdateWindowIconFromProject();
    SetFocus(window_);
    InvalidateRect(window_, nullptr, FALSE);
    ShowToast(L"文档资料已更新。");
}

std::wstring App::StoreImagePath(const std::wstring& pickedPath) const {
    const std::wstring cleaned = mdge::CleanPathInput(pickedPath);
    if (cleaned.empty()) {
        return {};
    }
    const std::wstring absolute = mdge::GetAbsolutePath(cleaned);
    if (projectPath_.empty()) {
        return absolute;
    }
    std::wstring relativePath;
    if (mdge::TryMakeRelativePath(ProjectDirectory(), absolute, relativePath)) {
        return relativePath;
    }
    return absolute;
}

void App::AddImageBlockAt(int atIndex, const std::wstring& pickedPath) {
    const std::wstring storedPath = StoreImagePath(pickedPath);
    if (storedPath.empty()) {
        ShowToast(L"没有可用的图片路径。", true);
        return;
    }
    int index = std::max(0, std::min(atIndex, static_cast<int>(document_.blocks.size())));
    mdge::Block block = mdge::MakeBlock(mdge::BlockType::Image);
    block.imagePath = storedPath;
    block.caption = mdge::FileStem(pickedPath);
    document_.blocks.insert(document_.blocks.begin() + index, std::move(block));
    selection_.block = index;
    selection_.caret = 0;
    selection_.anchor = 0;
    document_.modified = NowUnixSeconds();
    MarkDocumentDirty();
    InvalidatePage();
    InvalidateSourceLayout();
    markdownDirty_ = true;
    ClampScrollPositions();
    EnsureCaretVisible();
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void App::InsertImageBlockFromDialog(int atIndex) {
    const std::wstring picked = PickImageFile(window_, L"插入图片");
    if (picked.empty()) {
        SetFocus(window_);
        return;
    }
    if (!mdge::HasImageExtension(picked)) {
        MessageBoxW(window_, L"请选择 PNG、JPEG、BMP、GIF 或 ICO 图片。",
                    L"插入图片", MB_OK | MB_ICONINFORMATION);
        return;
    }
    PushUndo(L"insert-image", false);
    AddImageBlockAt(atIndex, picked);
    SetFocus(window_);
    ShowToast(L"图片已插入。");
}

void App::InsertDroppedFiles(const std::vector<std::wstring>& files, float x,
                             float y) {
    if (files.empty()) {
        return;
    }
    bool hasProject = false;
    for (const std::wstring& file : files) {
        if (mdge::FileExtensionLower(file) == L".mdge") {
            hasProject = true;
            break;
        }
    }
    if (hasProject) {
        if (!MaybeSaveBeforeDestructiveAction()) {
            return;
        }
        if (OpenProjectFile(files.front(), true, true)) {
            ShowToast(L"项目已打开。");
        }
        return;
    }

    std::vector<std::wstring> images;
    for (const std::wstring& file : files) {
        if (mdge::HasImageExtension(file) && mdge::IsFileExisting(file)) {
            images.push_back(file);
        }
    }
    if (images.empty()) {
        ShowToast(L"请拖入图片文件或 .mdge 项目文件。", true);
        return;
    }

    EnsurePageLayout();
    int insertIndex = static_cast<int>(document_.blocks.size());
    if (ui_.canvas.Contains(x, y)) {
        insertIndex = HitDropIndex(y);
    } else if (selection_.block >= 0) {
        insertIndex = std::min(static_cast<int>(document_.blocks.size()),
                               selection_.block + 1);
    }
    PushUndo(L"drop-images", false);
    for (const std::wstring& image : images) {
        AddImageBlockAt(insertIndex, image);
        ++insertIndex;
    }
    SetFocus(window_);
    ShowToast(L"图片已插入。");
}

void App::ReleaseProjectWindowIcon() {
    if (projectIconLarge_) {
        DestroyIcon(projectIconLarge_);
        projectIconLarge_ = nullptr;
    }
    if (projectIconSmall_) {
        DestroyIcon(projectIconSmall_);
        projectIconSmall_ = nullptr;
    }
    if (window_) {
        SendMessageW(window_, WM_SETICON, ICON_BIG, 0);
        SendMessageW(window_, WM_SETICON, ICON_SMALL, 0);
    }
}

void App::ApplyProjectWindowIcon() {
    UpdateWindowIconFromProject();
}

void App::UpdateWindowIconFromProject() {
    if (!window_) {
        return;
    }
    ReleaseProjectWindowIcon();

    HICON fallbackLarge = reinterpret_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32,
                   LR_DEFAULTCOLOR | LR_SHARED));
    HICON fallbackSmall = reinterpret_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16,
                   LR_DEFAULTCOLOR | LR_SHARED));

    const mdge::ResolvedAssetPath resolved =
        mdge::ResolveAssetPath(document_.iconPath, BaseDocumentPath());
    if (resolved.exists) {
        projectIconLarge_ = imageLoader_.CreateHIcon(resolved.resolvedPath, 32);
        projectIconSmall_ = imageLoader_.CreateHIcon(resolved.resolvedPath, 16);
    }

    if (projectIconLarge_) {
        SendMessageW(window_, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(projectIconLarge_));
    } else if (fallbackLarge) {
        SendMessageW(window_, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(fallbackLarge));
    }
    if (projectIconSmall_) {
        SendMessageW(window_, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(projectIconSmall_));
    } else if (fallbackSmall) {
        SendMessageW(window_, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(fallbackSmall));
    }
}

void App::UpdateWindowTitle() {
    if (!window_) {
        return;
    }
    std::wstring title = L"MDGE Studio";
    if (!document_.title.empty()) {
        title += L" - ";
        title += document_.title;
    }
    if (dirty_) {
        title += L" *";
    }
    SetWindowTextW(window_, title.c_str());
}

}  // namespace mdgeui
