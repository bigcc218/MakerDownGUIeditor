#include "file_associations.h"

#include <shlobj_core.h>

#include "resource.h"

#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace mdgeui {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\MDGE Studio";
constexpr wchar_t kSetupMarker[] = L"FileAssociationSetupDone";
constexpr wchar_t kClassesKey[] = L"Software\\Classes";
constexpr wchar_t kProjectExtension[] = L".mdge";
constexpr wchar_t kMarkdownExtension[] = L".md";
constexpr wchar_t kProjectProgId[] = L"MDGEStudio.Project";
constexpr wchar_t kMarkdownProgId[] = L"MDGEStudio.Markdown";
constexpr wchar_t kProjectTypeName[] = L"MDGE Studio 项目";
constexpr wchar_t kMarkdownTypeName[] = L"Markdown 文档";

bool RegistryReadString(HKEY root, const std::wstring& subkey,
                        const wchar_t* valueName, std::wstring& value) {
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = RegGetValueW(root, subkey.c_str(), valueName, RRF_RT_REG_SZ,
                                  &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return false;
    }
    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    status = RegGetValueW(root, subkey.c_str(), valueName, RRF_RT_REG_SZ, &type,
                          buffer.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    value = std::move(buffer);
    return true;
}

bool SetRegistryString(HKEY root, const std::wstring& subkey,
                       const wchar_t* valueName, const std::wstring& value,
                       std::wstring& error) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0,
                                     KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        error = L"无法创建注册表项：" + subkey;
        return false;
    }
    status = RegSetValueExW(
        key, valueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        error = L"无法写入注册表值：" + subkey;
        return false;
    }
    return true;
}

bool IsSetupComplete() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kSetupMarker,
                     RRF_RT_REG_DWORD, &type, &value, &size);
    return status == ERROR_SUCCESS && value != 0;
}

void MarkSetupComplete() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    const DWORD enabled = 1;
    RegSetValueExW(key, kSetupMarker, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&enabled), sizeof(enabled));
    RegCloseKey(key);
}

std::wstring QueryAssociationExecutable(const wchar_t* extension) {
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    const HRESULT result =
        AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, extension,
                          nullptr, buffer.data(), &length);
    if (FAILED(result) || length == 0 || buffer[0] == L'\0') {
        return {};
    }
    return std::wstring(buffer.data());
}

bool SamePath(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) {
        return false;
    }
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

std::wstring QuotedExecutable(const std::wstring& executablePath) {
    return L"\"" + executablePath + L"\"";
}

bool RegisterAssociation(const wchar_t* extension, const wchar_t* progId,
                         const wchar_t* typeName, int iconResourceId,
                         const std::wstring& executablePath,
                         std::wstring& error) {
    error.clear();
    const std::wstring classes = kClassesKey;
    const std::wstring extensionKey = classes + L"\\" + extension;
    const std::wstring progIdKey = classes + L"\\" + progId;

    if (!SetRegistryString(HKEY_CURRENT_USER, extensionKey, nullptr, progId,
                           error)) {
        return false;
    }
    if (!SetRegistryString(HKEY_CURRENT_USER, progIdKey, nullptr, typeName,
                           error)) {
        return false;
    }
    if (!SetRegistryString(HKEY_CURRENT_USER, progIdKey, L"FriendlyTypeName",
                           typeName, error)) {
        return false;
    }
    const std::wstring defaultIcon =
        QuotedExecutable(executablePath) + L",-" +
        std::to_wstring(iconResourceId);
    if (!SetRegistryString(HKEY_CURRENT_USER, progIdKey + L"\\DefaultIcon",
                           nullptr, defaultIcon, error)) {
        return false;
    }
    const std::wstring command =
        QuotedExecutable(executablePath) + L" \"%1\"";
    if (!SetRegistryString(HKEY_CURRENT_USER,
                           progIdKey + L"\\shell\\open\\command", nullptr,
                           command, error)) {
        return false;
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

}  // namespace

void InitializeFileAssociations(HWND owner, const std::wstring& executablePath) {
    if (executablePath.empty() || IsSetupComplete()) {
        return;
    }

    const std::wstring projectAssociation =
        QueryAssociationExecutable(kProjectExtension);
    bool projectAssociated = !projectAssociation.empty();
    std::wstring projectError;
    bool projectRegistered = false;

    if (!projectAssociated) {
        projectRegistered = RegisterAssociation(
            kProjectExtension, kProjectProgId, kProjectTypeName,
            IDI_MDGE_FILE_ICON, executablePath, projectError);
        projectAssociated = projectRegistered;
    }

    const std::wstring markdownAssociation =
        QueryAssociationExecutable(kMarkdownExtension);
    const bool markdownIsOurs =
        SamePath(markdownAssociation, executablePath);
    if (!markdownIsOurs) {
        std::wstring message;
        if (projectRegistered) {
            message += L"MDGE Studio 已自动关联 .mdge 项目文件。\n\n";
        } else if (!projectAssociated && !projectError.empty()) {
            message += L"无法自动关联 .mdge 文件：\n";
            message += projectError;
            message += L"\n\n";
        }
        if (!markdownAssociation.empty()) {
            message += L"当前 .md 文件由其他程序打开。\n\n";
        }
        message +=
            L"是否将 .md 文件关联到 MDGE Studio？\n\n"
            L"选择“是”后，双击 .md 文件会使用 MDGE Studio 打开。";
        const int choice = MessageBoxW(
            owner, message.c_str(), L"文件关联",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
        if (choice == IDYES) {
            std::wstring markdownError;
            if (!RegisterAssociation(kMarkdownExtension, kMarkdownProgId,
                                     kMarkdownTypeName, IDI_MD_FILE_ICON,
                                     executablePath, markdownError)) {
                MessageBoxW(owner,
                            (L"无法写入 .md 文件关联：\n\n" + markdownError).c_str(),
                            L"文件关联", MB_OK | MB_ICONWARNING);
            }
        }
    } else if (!projectAssociated && !projectError.empty()) {
        MessageBoxW(owner,
                    (L"无法自动关联 .mdge 文件：\n\n" + projectError).c_str(),
                    L"文件关联", MB_OK | MB_ICONWARNING);
    }

    MarkSetupComplete();
}

}  // namespace mdgeui
