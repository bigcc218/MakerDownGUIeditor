// MDGE Studio - native dialogs and file pickers
#pragma once

#include <windows.h>

#include <string>

namespace mdgeui {

struct DocumentSettings {
    std::wstring title;
    std::wstring author;
    std::wstring iconPath;
};

bool ShowDocumentSettingsDialog(HWND owner, const DocumentSettings& current,
                                const std::wstring& projectPath,
                                DocumentSettings& updated);

struct LinkDialogResult {
    bool accepted = false;
    std::wstring href;
};

LinkDialogResult ShowLinkDialog(HWND owner, const std::wstring& selectedText,
                                const std::wstring& currentHref);

std::wstring PickProjectToOpen(HWND owner);
std::wstring PickProjectSavePath(HWND owner, const std::wstring& defaultName);
std::wstring PickMarkdownExportPath(HWND owner, const std::wstring& defaultName);
std::wstring PickImageFile(HWND owner, const std::wstring& title);

}  // namespace mdgeui
