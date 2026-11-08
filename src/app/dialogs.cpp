#include "dialogs.h"

#include "core/path_utils.h"
#include "image_loader.h"
#include "resource.h"
#include "ui_util.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace mdgeui {
namespace {

std::wstring GetControlText(HWND dialog, int id) {
    HWND control = GetDlgItem(dialog, id);
    if (!control) {
        return {};
    }
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void SetControlText(HWND dialog, int id, const std::wstring& text) {
    SetDlgItemTextW(dialog, id, text.c_str());
}

void CenterDialogOnOwner(HWND dialog, HWND owner) {
    RECT ownerRect{};
    RECT dialogRect{};
    GetWindowRect(owner, &ownerRect);
    GetWindowRect(dialog, &dialogRect);
    const int width = dialogRect.right - dialogRect.left;
    const int height = dialogRect.bottom - dialogRect.top;
    const int x = ownerRect.left + (ownerRect.right - ownerRect.left - width) / 2;
    const int y = ownerRect.top + (ownerRect.bottom - ownerRect.top - height) / 2;
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

struct SettingsDialogContext {
    DocumentSettings current;
    DocumentSettings result;
    std::wstring projectPath;
    ImageLoader loader;
    bool loaderReady = false;
    HICON previewIcon = nullptr;
    bool accepted = false;
};

void UpdateIconStatus(HWND dialog, SettingsDialogContext* context) {
    const std::wstring stored = mdge::CleanPathInput(GetControlText(dialog, IDC_SET_ICON));
    if (stored.empty()) {
        SetControlText(dialog, IDC_ICON_STATUS,
                       L"未设置图标。\n将使用 MDGE Studio 的内置标记。");
        return;
    }
    const mdge::ResolvedAssetPath resolved =
        mdge::ResolveAssetPath(stored, context->projectPath);
    std::wstring status;
    status += L"路径类型：";
    status += resolved.originLabel;
    status += L"\n";
    if (resolved.exists) {
        status += L"已找到：";
        status += EllipsizeMiddle(resolved.resolvedPath, 54);
    } else {
        status += L"文件未找到：";
        status += EllipsizeMiddle(
            resolved.resolvedPath.empty() ? stored : resolved.resolvedPath, 50);
        if (context->projectPath.empty() && !mdge::IsAbsolutePath(stored)) {
            status += L"\n保存项目后，相对路径将以项目目录为基准。";
        }
    }
    SetControlText(dialog, IDC_ICON_STATUS, status);
    (void)dialog;
}

void SetPreviewIcon(HWND dialog, SettingsDialogContext* context, HICON icon) {
    HWND preview = GetDlgItem(dialog, IDC_ICON_PREVIEW);
    if (!preview) {
        if (icon) {
            DestroyIcon(icon);
        }
        return;
    }
    if (context->previewIcon && context->previewIcon != icon) {
        DestroyIcon(context->previewIcon);
    }
    context->previewIcon = icon;
    SendMessageW(preview, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
    InvalidateRect(preview, nullptr, TRUE);
}

void UpdateIconPreview(HWND dialog, SettingsDialogContext* context) {
    const std::wstring stored = mdge::CleanPathInput(GetControlText(dialog, IDC_SET_ICON));
    HICON icon = nullptr;
    if (!stored.empty()) {
        const mdge::ResolvedAssetPath resolved =
            mdge::ResolveAssetPath(stored, context->projectPath);
        if (resolved.exists && context->loaderReady) {
            icon = context->loader.CreateHIcon(resolved.resolvedPath, 48);
        }
    }
    if (!icon) {
        HICON fallback = reinterpret_cast<HICON>(
            LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
                       IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        if (!fallback) {
            fallback = LoadIconW(GetModuleHandleW(nullptr),
                                 MAKEINTRESOURCEW(IDI_APP_ICON));
            if (fallback) {
                fallback = CopyIcon(fallback);
            }
        }
        icon = fallback;
    }
    SetPreviewIcon(dialog, context, icon);
}

void HandleMakeRelative(HWND dialog, SettingsDialogContext* context) {
    const std::wstring stored = mdge::CleanPathInput(GetControlText(dialog, IDC_SET_ICON));
    if (stored.empty()) {
        MessageBoxW(dialog, L"当前没有设置图标路径。", L"设为相对路径",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (context->projectPath.empty()) {
        MessageBoxW(dialog,
                    L"项目尚未保存。请先在主窗口保存 .mdge 项目，再使用“设为相对路径”。",
                    L"设为相对路径", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const mdge::ResolvedAssetPath resolved =
        mdge::ResolveAssetPath(stored, context->projectPath);
    if (resolved.resolvedPath.empty()) {
        MessageBoxW(dialog, L"无法解析图标路径。", L"设为相对路径",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring relative;
    if (!mdge::TryMakeRelativePath(mdge::DirectoryName(context->projectPath),
                                   resolved.resolvedPath, relative)) {
        MessageBoxW(dialog,
                    L"无法生成相对路径（项目与图标可能位于不同磁盘）。",
                    L"设为相对路径", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::replace(relative.begin(), relative.end(), L'/', L'\\');
    SetControlText(dialog, IDC_SET_ICON, relative);
    UpdateIconStatus(dialog, context);
    UpdateIconPreview(dialog, context);
}

INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM wparam,
                                    LPARAM lparam) {
    auto* context = reinterpret_cast<SettingsDialogContext*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<SettingsDialogContext*>(lparam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(context));
        if (!context) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        CenterDialogOnOwner(dialog, GetParent(dialog));
        SetWindowTextW(dialog, L"文档资料");
        SetControlText(dialog, IDC_SET_TITLE, context->current.title);
        SetControlText(dialog, IDC_SET_AUTHOR, context->current.author);
        SetControlText(dialog, IDC_SET_ICON, context->current.iconPath);
        std::wstring error;
        context->loaderReady = context->loader.Initialize(error);
        UpdateIconStatus(dialog, context);
        UpdateIconPreview(dialog, context);
        SendDlgItemMessageW(dialog, IDC_SET_TITLE, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dialog, IDC_SET_TITLE));
        return FALSE;
    }
    if (!context) {
        return FALSE;
    }

    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (id == IDC_SET_ICON && code == EN_CHANGE) {
                UpdateIconStatus(dialog, context);
                return TRUE;
            }
            if (id == IDC_BROWSE_ICON) {
                const std::wstring path = PickImageFile(dialog, L"选择项目图标");
                if (!path.empty()) {
                    SetControlText(dialog, IDC_SET_ICON, path);
                    UpdateIconStatus(dialog, context);
                    UpdateIconPreview(dialog, context);
                }
                return TRUE;
            }
            if (id == IDC_MAKE_RELATIVE) {
                HandleMakeRelative(dialog, context);
                return TRUE;
            }
            if (id == IDC_REFRESH_ICON) {
                UpdateIconStatus(dialog, context);
                UpdateIconPreview(dialog, context);
                return TRUE;
            }
            if (id == IDOK) {
                context->result.title = mdge::TrimText(GetControlText(dialog, IDC_SET_TITLE));
                if (context->result.title.empty()) {
                    context->result.title = L"未命名文档";
                }
                context->result.author =
                    mdge::TrimText(GetControlText(dialog, IDC_SET_AUTHOR));
                context->result.iconPath =
                    mdge::CleanPathInput(GetControlText(dialog, IDC_SET_ICON));
                context->accepted = true;
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

struct LinkDialogContext {
    std::wstring selectedText;
    std::wstring currentHref;
    LinkDialogResult result;
};

INT_PTR CALLBACK LinkDialogProc(HWND dialog, UINT message, WPARAM wparam,
                                LPARAM lparam) {
    auto* context = reinterpret_cast<LinkDialogContext*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        context = reinterpret_cast<LinkDialogContext*>(lparam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(context));
        if (!context) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        CenterDialogOnOwner(dialog, GetParent(dialog));
        std::wstring info = L"为选中的文字设置链接：";
        std::wstring excerpt = context->selectedText;
        std::replace(excerpt.begin(), excerpt.end(), L'\r', L' ');
        std::replace(excerpt.begin(), excerpt.end(), L'\n', L' ');
        if (excerpt.size() > 24) {
            excerpt.resize(24);
            excerpt += L"...";
        }
        info += excerpt;
        SetControlText(dialog, IDC_LINK_INFO, info);
        SetControlText(dialog, IDC_LINK_URL, context->currentHref);
        SendDlgItemMessageW(dialog, IDC_LINK_URL, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dialog, IDC_LINK_URL));
        return FALSE;
    }
    if (!context) {
        return FALSE;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wparam);
        if (id == IDOK) {
            context->result.href =
                mdge::TrimText(GetControlText(dialog, IDC_LINK_URL));
            context->result.accepted = true;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

std::wstring GetShellItemPath(IFileDialog* dialog) {
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.GetAddressOf())) || !item) {
        return {};
    }
    wchar_t* path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
        return {};
    }
    std::wstring result = path;
    CoTaskMemFree(path);
    return result;
}

std::wstring ShowFileDialog(HWND owner, bool save, const std::wstring& title,
                            const COMDLG_FILTERSPEC* filters, UINT filterCount,
                            const std::wstring& defaultName,
                            const std::wstring& defaultExtension) {
    ComPtr<IFileOpenDialog> openDialog;
    ComPtr<IFileSaveDialog> saveDialog;
    IFileDialog* dialog = nullptr;
    HRESULT result = S_OK;
    if (save) {
        result = CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(saveDialog.GetAddressOf()));
        dialog = saveDialog.Get();
    } else {
        result = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(openDialog.GetAddressOf()));
        dialog = openDialog.Get();
    }
    if (FAILED(result) || !dialog) {
        MessageBoxW(owner, L"无法打开系统文件对话框。", L"MDGE Studio",
                    MB_OK | MB_ICONERROR);
        return {};
    }

    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    if (save) {
        options |= FOS_OVERWRITEPROMPT;
    } else {
        options |= FOS_FILEMUSTEXIST;
    }
    dialog->SetOptions(options);
    dialog->SetTitle(title.c_str());
    if (filters && filterCount > 0) {
        dialog->SetFileTypes(filterCount, filters);
        dialog->SetFileTypeIndex(1);
    }
    if (!defaultExtension.empty()) {
        dialog->SetDefaultExtension(defaultExtension.c_str());
    }
    if (save && !defaultName.empty()) {
        dialog->SetFileName(defaultName.c_str());
    }

    result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return {};
    }
    if (FAILED(result)) {
        MessageBoxW(owner, L"文件对话框打开失败。", L"MDGE Studio",
                    MB_OK | MB_ICONERROR);
        return {};
    }
    return GetShellItemPath(dialog);
}

std::vector<COMDLG_FILTERSPEC> ProjectFilters() {
    return {
        {L"MDGE 项目文件", L"*.mdge"},
        {L"所有文件", L"*.*"},
    };
}

std::vector<COMDLG_FILTERSPEC> MarkdownFilters() {
    return {
        {L"Markdown 文档", L"*.md"},
        {L"所有文件", L"*.*"},
    };
}

std::vector<COMDLG_FILTERSPEC> ImageFilters() {
    return {
        {L"图像文件", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.ico"},
        {L"所有文件", L"*.*"},
    };
}

}  // namespace

bool ShowDocumentSettingsDialog(HWND owner, const DocumentSettings& current,
                                const std::wstring& projectPath,
                                DocumentSettings& updated) {
    SettingsDialogContext context;
    context.current = current;
    context.projectPath = projectPath;
    const INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DOCUMENT_SETTINGS), owner,
        SettingsDialogProc, reinterpret_cast<LPARAM>(&context));
    if (context.previewIcon) {
        DestroyIcon(context.previewIcon);
        context.previewIcon = nullptr;
    }
    if (result == -1) {
        MessageBoxW(owner, L"无法创建文档资料对话框。", L"MDGE Studio",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    if (result == IDOK && context.accepted) {
        updated = context.result;
        return true;
    }
    return false;
}

LinkDialogResult ShowLinkDialog(HWND owner, const std::wstring& selectedText,
                                const std::wstring& currentHref) {
    LinkDialogContext context;
    context.selectedText = selectedText;
    context.currentHref = currentHref;
    const INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_LINK), owner,
        LinkDialogProc, reinterpret_cast<LPARAM>(&context));
    if (result == -1) {
        MessageBoxW(owner, L"无法创建链接对话框。", L"MDGE Studio",
                    MB_OK | MB_ICONERROR);
        return {};
    }
    if (result == IDOK) {
        return context.result;
    }
    return {};
}

std::wstring PickProjectToOpen(HWND owner) {
    const std::vector<COMDLG_FILTERSPEC> filters = ProjectFilters();
    return ShowFileDialog(owner, false, L"打开 MDGE 项目", filters.data(),
                          static_cast<UINT>(filters.size()), {}, {});
}

std::wstring PickProjectSavePath(HWND owner, const std::wstring& defaultName) {
    const std::vector<COMDLG_FILTERSPEC> filters = ProjectFilters();
    return ShowFileDialog(owner, true, L"保存 MDGE 项目", filters.data(),
                          static_cast<UINT>(filters.size()), defaultName, L"mdge");
}

std::wstring PickMarkdownExportPath(HWND owner, const std::wstring& defaultName) {
    const std::vector<COMDLG_FILTERSPEC> filters = MarkdownFilters();
    return ShowFileDialog(owner, true, L"导出 Markdown", filters.data(),
                          static_cast<UINT>(filters.size()), defaultName, L"md");
}

std::wstring PickImageFile(HWND owner, const std::wstring& title) {
    const std::vector<COMDLG_FILTERSPEC> filters = ImageFilters();
    return ShowFileDialog(owner, false, title, filters.data(),
                          static_cast<UINT>(filters.size()), {}, {});
}

}  // namespace mdgeui
