#include "file_associations.h"

#include "association_diagnostics.h"
#include "association_state_store.h"
#include "file_association_core.h"
#include "resource.h"

#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <sddl.h>

#include <chrono>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace mdgeui {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\MDGE Studio";
constexpr wchar_t kSetupVersionValue[] = L"AssociationSetupVersion";
constexpr wchar_t kProjectReadyValue[] = L"ProjectAssociationReady";
constexpr wchar_t kMarkdownPromptDoneValue[] = L"MarkdownPromptDone";
constexpr wchar_t kMarkdownRequestedValue[] = L"MarkdownAssociationRequested";
constexpr wchar_t kLastErrorValue[] = L"LastAssociationError";
constexpr wchar_t kLastRecoveryVersionValue[] = L"LastRecoveryPromptVersion";
constexpr wchar_t kLastRecoveryTimeValue[] = L"LastRecoveryPromptTime";
constexpr wchar_t kLegacySetupDoneValue[] = L"FileAssociationSetupDone";
constexpr wchar_t kBackupKey[] = L"Software\\MDGE Studio\\AssociationBackup";
constexpr wchar_t kBackupValidValue[] = L"MarkdownUserChoiceBackupValid";
constexpr wchar_t kBackupExistsValue[] = L"MarkdownUserChoiceExisted";
constexpr wchar_t kBackupProgIdValue[] = L"MarkdownUserChoiceProgId";
constexpr wchar_t kBackupHashValue[] = L"MarkdownUserChoiceHash";
constexpr wchar_t kUserChoicePath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.md\\UserChoice";
constexpr std::uint32_t kCurrentRecoveryPromptVersion = 2;
constexpr std::uint64_t kRecoveryPromptThrottleSeconds = 24u * 60u * 60u;
constexpr DWORD kUserChoiceRestoreFailed = 88;

std::uint64_t NowUnixSeconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

bool ReadDword(const std::wstring& subkey, const wchar_t* valueName,
               std::uint32_t& value) {
    DWORD size = sizeof(DWORD);
    DWORD type = 0;
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, subkey.c_str(), valueName, RRF_RT_REG_DWORD, &type,
        &value, &size);
    return status == ERROR_SUCCESS;
}

bool ReadQword(const std::wstring& subkey, const wchar_t* valueName,
               std::uint64_t& value) {
    ULONGLONG data = 0;
    DWORD size = sizeof(data);
    DWORD type = 0;
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, subkey.c_str(), valueName, RRF_RT_REG_QWORD, &type,
        &data, &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    value = static_cast<std::uint64_t>(data);
    return true;
}

bool ReadString(const std::wstring& subkey, const wchar_t* valueName,
                std::wstring& value) {
    DWORD bytes = 0;
    DWORD type = 0;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), valueName,
                                  RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return false;
    }
    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    status = RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), valueName,
                          RRF_RT_REG_SZ, &type, buffer.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    value = std::move(buffer);
    return true;
}

bool WriteDword(HKEY root, const std::wstring& subkey,
                const wchar_t* valueName, std::uint32_t value,
                DWORD& errorCode) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0,
                                     KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        errorCode = static_cast<DWORD>(status);
        return false;
    }
    status = RegSetValueExW(
        key, valueName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        errorCode = static_cast<DWORD>(status);
        return false;
    }
    errorCode = ERROR_SUCCESS;
    return true;
}

bool WriteQword(HKEY root, const std::wstring& subkey,
                const wchar_t* valueName, std::uint64_t value,
                DWORD& errorCode) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0,
                                     KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        errorCode = static_cast<DWORD>(status);
        return false;
    }
    const ULONGLONG data = value;
    status = RegSetValueExW(
        key, valueName, 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        errorCode = static_cast<DWORD>(status);
        return false;
    }
    errorCode = ERROR_SUCCESS;
    return true;
}

bool WriteStringValue(HKEY root, const std::wstring& subkey,
                      const wchar_t* valueName, const std::wstring& value,
                      DWORD& errorCode) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0,
                                     KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        errorCode = static_cast<DWORD>(status);
        return false;
    }
    status = RegSetValueExW(
        key, valueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        errorCode = static_cast<DWORD>(status);
        return false;
    }
    errorCode = ERROR_SUCCESS;
    return true;
}

bool ApplyRegistryEntries(const std::vector<RegistryEntry>& entries,
                          DWORD& errorCode, std::wstring* failedPath) {
    for (const RegistryEntry& entry : entries) {
        HKEY key = nullptr;
        LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER,
                                         entry.subkey.c_str(), 0, nullptr, 0,
                                         KEY_WRITE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) {
            errorCode = static_cast<DWORD>(status);
            if (failedPath) {
                *failedPath = entry.subkey;
            }
            return false;
        }
        const wchar_t* valueName =
            entry.valueName.empty() ? nullptr : entry.valueName.c_str();
        if (entry.type == RegistryValueType::String) {
            status = RegSetValueExW(
                key, valueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(entry.stringValue.c_str()),
                static_cast<DWORD>((entry.stringValue.size() + 1) *
                                   sizeof(wchar_t)));
        } else if (entry.type == RegistryValueType::Dword) {
            status = RegSetValueExW(
                key, valueName, 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&entry.dwordValue),
                sizeof(entry.dwordValue));
        } else {
            status = RegSetValueExW(key, valueName, 0, REG_NONE, nullptr, 0);
        }
        RegCloseKey(key);
        if (status != ERROR_SUCCESS) {
            errorCode = static_cast<DWORD>(status);
            if (failedPath) {
                *failedPath = entry.subkey;
            }
            return false;
        }
    }
    errorCode = ERROR_SUCCESS;
    return true;
}

AssociationSettings LoadSettings() {
    AssociationSettings settings;
    std::uint32_t value = 0;
    settings.version2 = ReadDword(kSettingsKey, kSetupVersionValue, value) &&
                        value >= 2;
    settings.legacySetupDone = ReadDword(kSettingsKey, kLegacySetupDoneValue, value) &&
                               value != 0;
    settings.projectReady = ReadDword(kSettingsKey, kProjectReadyValue, value) &&
                            value != 0;
    settings.markdownPromptDone =
        ReadDword(kSettingsKey, kMarkdownPromptDoneValue, value) && value != 0;
    settings.markdownRequested =
        ReadDword(kSettingsKey, kMarkdownRequestedValue, value) && value != 0;
    if (ReadDword(kSettingsKey, kLastErrorValue, value)) {
        settings.lastError = value;
    }
    if (ReadDword(kSettingsKey, kLastRecoveryVersionValue, value)) {
        settings.lastRecoveryPromptVersion = value;
    }
    ReadQword(kSettingsKey, kLastRecoveryTimeValue,
              settings.lastRecoveryPromptTime);

    AssociationFileState fileState;
    if (LoadAssociationFileState(fileState)) {
        settings.version2 = fileState.version2;
        settings.projectReady = fileState.projectReady;
        settings.markdownPromptDone = fileState.markdownPromptDone;
        settings.markdownRequested = fileState.markdownRequested;
        settings.lastError = fileState.lastError;
        settings.lastRecoveryPromptVersion =
            fileState.lastRecoveryPromptVersion;
        settings.lastRecoveryPromptTime = fileState.lastRecoveryPromptTime;
    }
    ApplyLegacyMigration(settings);
    return settings;
}

void PersistSettings(const AssociationSettings& settings) {
    AssociationFileState fileState;
    fileState.valid = true;
    fileState.version2 = settings.version2;
    fileState.projectReady = settings.projectReady;
    fileState.markdownPromptDone = settings.markdownPromptDone;
    fileState.markdownRequested = settings.markdownRequested;
    fileState.lastError = settings.lastError;
    fileState.lastRecoveryPromptVersion = settings.lastRecoveryPromptVersion;
    fileState.lastRecoveryPromptTime = settings.lastRecoveryPromptTime;
    SaveAssociationFileState(fileState);

    DWORD ignored = 0;
    WriteDword(HKEY_CURRENT_USER, kSettingsKey, kSetupVersionValue, 2, ignored);
    WriteDword(HKEY_CURRENT_USER, kSettingsKey, kProjectReadyValue,
               settings.projectReady ? 1u : 0u, ignored);
    WriteDword(HKEY_CURRENT_USER, kSettingsKey, kMarkdownPromptDoneValue,
               settings.markdownPromptDone ? 1u : 0u, ignored);
    WriteDword(HKEY_CURRENT_USER, kSettingsKey, kMarkdownRequestedValue,
               settings.markdownRequested ? 1u : 0u, ignored);
    WriteDword(HKEY_CURRENT_USER, kSettingsKey, kLastErrorValue,
               settings.lastError, ignored);
    WriteDword(HKEY_CURRENT_USER, kSettingsKey, kLastRecoveryVersionValue,
               settings.lastRecoveryPromptVersion, ignored);
    WriteQword(HKEY_CURRENT_USER, kSettingsKey, kLastRecoveryTimeValue,
               settings.lastRecoveryPromptTime, ignored);
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

std::wstring QueryAssociationCommand(const wchar_t* extension) {
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    const HRESULT result =
        AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_COMMAND, extension,
                          nullptr, buffer.data(), &length);
    if (FAILED(result) || length == 0 || buffer[0] == L'\0') {
        return {};
    }
    return std::wstring(buffer.data());
}

std::wstring NormalizePathForCompare(std::wstring path) {
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') {
        path = path.substr(1, path.size() - 2);
    }
    std::vector<wchar_t> fullPath(32768, L'\0');
    const DWORD fullLength = GetFullPathNameW(
        path.c_str(), static_cast<DWORD>(fullPath.size()), fullPath.data(),
        nullptr);
    if (fullLength > 0 && fullLength < fullPath.size()) {
        path.assign(fullPath.data(), fullLength);
    }
    std::vector<wchar_t> longPath(32768, L'\0');
    const DWORD longLength = GetLongPathNameW(
        path.c_str(), longPath.data(), static_cast<DWORD>(longPath.size()));
    if (longLength > 0 && longLength < longPath.size()) {
        path.assign(longPath.data(), longLength);
    }
    while (!path.empty() && (path.back() == L' ' || path.back() == L'\t')) {
        path.pop_back();
    }
    return path;
}

bool CommandTargetsExecutable(const std::wstring& command,
                              const std::wstring& executablePath) {
    if (command.empty()) {
        return false;
    }
    std::wstring target;
    if (command.front() == L'"') {
        const std::size_t closing = command.find(L'"', 1);
        if (closing != std::wstring::npos) {
            target = command.substr(1, closing - 1);
        }
    }
    if (target.empty()) {
        const std::size_t space = command.find(L' ');
        target = command.substr(0, space);
    }
    return PathsEqual(NormalizePathForCompare(target),
                      NormalizePathForCompare(executablePath));
}

AssociationObservation ObserveAssociations(const std::wstring& executablePath) {
    AssociationObservation observed;
    const std::wstring project = QueryAssociationExecutable(L".mdge");
    const std::wstring markdown = QueryAssociationExecutable(L".md");
    const std::wstring projectCommand = QueryAssociationCommand(L".mdge");
    const std::wstring markdownCommand = QueryAssociationCommand(L".md");
    observed.projectUsesOpenWithFallback =
        IsOpenWithFallbackExecutable(project);
    observed.markdownUsesOpenWithFallback =
        IsOpenWithFallbackExecutable(markdown);
    observed.projectAssociated =
        !project.empty() && !observed.projectUsesOpenWithFallback;
    observed.markdownAssociated =
        !markdown.empty() && !observed.markdownUsesOpenWithFallback;
    const std::wstring normalizedExecutable =
        NormalizePathForCompare(executablePath);
    const bool projectPathMatches =
        observed.projectAssociated &&
        PathsEqual(NormalizePathForCompare(project), normalizedExecutable);
    const bool markdownPathMatches =
        observed.markdownAssociated &&
        PathsEqual(NormalizePathForCompare(markdown), normalizedExecutable);
    observed.projectIsOurs =
        projectPathMatches &&
        (projectCommand.empty() ||
         CommandTargetsExecutable(projectCommand, executablePath));
    observed.markdownIsOurs =
        markdownPathMatches &&
        (markdownCommand.empty() ||
         CommandTargetsExecutable(markdownCommand, executablePath));
    return observed;
}

std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0) {
        CloseHandle(token);
        return {};
    }
    std::vector<unsigned char> buffer(bytes);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
        CloseHandle(token);
        return {};
    }
    auto* tokenUser = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR sidString = nullptr;
    std::wstring result;
    if (ConvertSidToStringSidW(tokenUser->User.Sid, &sidString)) {
        result = sidString;
        LocalFree(sidString);
    }
    CloseHandle(token);
    return result;
}

std::wstring ProcessUserSid(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0) {
        CloseHandle(token);
        return {};
    }
    std::vector<unsigned char> buffer(bytes);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
        CloseHandle(token);
        return {};
    }
    auto* tokenUser = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR sidString = nullptr;
    std::wstring result;
    if (ConvertSidToStringSidW(tokenUser->User.Sid, &sidString)) {
        result = sidString;
        LocalFree(sidString);
    }
    CloseHandle(token);
    return result;
}

void OpenDefaultAppsSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr,
                  SW_SHOWNORMAL);
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }
    return std::wstring(path.data(), length);
}

bool RegisterProjectAssociation(const std::wstring& executablePath,
                                DWORD& errorCode) {
    std::wstring failedPath;
    const std::vector<RegistryEntry> entries =
        BuildRegistrationEntries(executablePath, true, false);
    if (!ApplyRegistryEntries(entries, errorCode, &failedPath)) {
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

UserChoiceBackup QueryUserChoice() {
    UserChoiceBackup backup;
    std::wstring progId;
    std::wstring hash;
    const bool hasProgId = ReadString(kUserChoicePath, L"ProgId", progId);
    const bool hasHash = ReadString(kUserChoicePath, L"Hash", hash);
    if (hasProgId || hasHash) {
        backup.exists = true;
        backup.progId = std::move(progId);
        backup.hash = std::move(hash);
    }
    return backup;
}

bool SaveUserChoiceBackup(const UserChoiceBackup& backup, DWORD& errorCode) {
    if (!WriteDword(HKEY_CURRENT_USER, kBackupKey, kBackupValidValue, 1,
                    errorCode)) {
        return false;
    }
    if (!WriteDword(HKEY_CURRENT_USER, kBackupKey, kBackupExistsValue,
                    backup.exists ? 1u : 0u, errorCode)) {
        return false;
    }
    if (!WriteStringValue(HKEY_CURRENT_USER, kBackupKey, kBackupProgIdValue,
                          backup.progId, errorCode)) {
        return false;
    }
    if (!WriteStringValue(HKEY_CURRENT_USER, kBackupKey, kBackupHashValue,
                          backup.hash, errorCode)) {
        return false;
    }
    return true;
}

bool LoadUserChoiceBackup(UserChoiceBackup& backup) {
    backup = {};
    std::uint32_t valid = 0;
    if (ReadDword(kBackupKey, kBackupValidValue, valid) && valid != 0) {
        std::uint32_t exists = 0;
        ReadDword(kBackupKey, kBackupExistsValue, exists);
        backup.exists = exists != 0;
        if (backup.exists) {
            ReadString(kBackupKey, kBackupProgIdValue, backup.progId);
            ReadString(kBackupKey, kBackupHashValue, backup.hash);
        }
        return true;
    }
    return LoadUserChoiceBackupFile(backup);
}

void ClearUserChoiceBackup() {
    RegDeleteTreeW(HKEY_CURRENT_USER, kBackupKey);
    ClearUserChoiceBackupFile();
}

bool ClearUserChoice(DWORD& errorCode) {
    const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kUserChoicePath);
    if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ||
        status == ERROR_PATH_NOT_FOUND) {
        errorCode = ERROR_SUCCESS;
        return true;
    }
    errorCode = static_cast<DWORD>(status);
    return false;
}

bool RestoreUserChoice(const UserChoiceBackup& backup, DWORD& errorCode) {
    if (!backup.exists) {
        return ClearUserChoice(errorCode);
    }
    if (!WriteStringValue(HKEY_CURRENT_USER, kUserChoicePath, L"ProgId",
                          backup.progId, errorCode)) {
        return false;
    }
    if (!WriteStringValue(HKEY_CURRENT_USER, kUserChoicePath, L"Hash",
                          backup.hash, errorCode)) {
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool EnsureUserChoiceBackup(const UserChoiceBackup& current, DWORD& errorCode) {
    UserChoiceBackup existing;
    if (LoadUserChoiceBackup(existing)) {
        return true;
    }
    if (SaveUserChoiceBackup(current, errorCode)) {
        return true;
    }
    if (SaveUserChoiceBackupFile(current)) {
        errorCode = ERROR_SUCCESS;
        return true;
    }
    return false;
}

bool RegisterMarkdownAssociation(const std::wstring& executablePath,
                                 DWORD& errorCode) {
    const UserChoiceBackup original = QueryUserChoice();
    if (original.exists && !EnsureUserChoiceBackup(original, errorCode)) {
        return false;
    }
    if (!original.exists) {
        EnsureUserChoiceBackup(original, errorCode);
    }

    std::wstring failedPath;
    const std::vector<RegistryEntry> entries =
        BuildRegistrationEntries(executablePath, false, true);
    if (!ApplyRegistryEntries(entries, errorCode, &failedPath)) {
        return false;
    }
    if (!ClearUserChoice(errorCode)) {
        DWORD restoreError = ERROR_SUCCESS;
        bool restoreFailed = false;
        if (ShouldRestoreUserChoiceOnFailure(original)) {
            restoreFailed = !RestoreUserChoice(original, restoreError);
        }
        if (!restoreFailed) {
            ClearUserChoiceBackup();
        } else {
            errorCode = kUserChoiceRestoreFailed;
        }
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    const AssociationObservation observed =
        ObserveAssociations(executablePath);
    if (!observed.markdownIsOurs) {
        DWORD restoreError = ERROR_SUCCESS;
        bool restoreFailed = false;
        if (ShouldRestoreUserChoiceOnFailure(original)) {
            restoreFailed = !RestoreUserChoice(original, restoreError);
        }
        if (!restoreFailed) {
            ClearUserChoiceBackup();
        }
        errorCode = restoreFailed ? kUserChoiceRestoreFailed
                                  : ERROR_ACCESS_DENIED;
        return false;
    }
    ClearUserChoiceBackup();
    return true;
}

bool VerifyRequestedAssociation(const std::wstring& executablePath,
                                const AssociationFileRequest& request,
                                DWORD& errorCode) {
    const AssociationObservation observed =
        ObserveAssociations(executablePath);
    bool ok = true;
    if (request.mdge && !observed.projectIsOurs) {
        ok = false;
    }
    if (request.md && !observed.markdownIsOurs) {
        ok = false;
    }
    if (!ok) {
        errorCode = ERROR_ACCESS_DENIED;
    }
    return ok;
}

bool RegisterSilent(const std::wstring& executablePath,
                    const AssociationFileRequest& request, DWORD& errorCode) {
    UserChoiceBackup original;
    if (request.md) {
        original = QueryUserChoice();
        if (!EnsureUserChoiceBackup(original, errorCode)) {
            return false;
        }
    }
    std::wstring failedPath;
    const std::vector<RegistryEntry> entries =
        BuildRegistrationEntries(executablePath, request.mdge, request.md);
    if (!ApplyRegistryEntries(entries, errorCode, &failedPath)) {
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (request.md) {
        if (!ClearUserChoice(errorCode)) {
            DWORD restoreError = ERROR_SUCCESS;
            bool restoreFailed = false;
            if (ShouldRestoreUserChoiceOnFailure(original)) {
                restoreFailed = !RestoreUserChoice(original, restoreError);
            }
            if (!restoreFailed) {
                ClearUserChoiceBackup();
            } else {
                errorCode = kUserChoiceRestoreFailed;
            }
            return false;
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        const AssociationObservation observed =
            ObserveAssociations(executablePath);
        if (!observed.markdownIsOurs) {
            DWORD restoreError = ERROR_SUCCESS;
            bool restoreFailed = false;
            if (ShouldRestoreUserChoiceOnFailure(original)) {
                restoreFailed = !RestoreUserChoice(original, restoreError);
            }
            if (!restoreFailed) {
                ClearUserChoiceBackup();
            }
            errorCode = restoreFailed ? kUserChoiceRestoreFailed
                                      : ERROR_ACCESS_DENIED;
            return false;
        }
    }
    if (!VerifyRequestedAssociation(executablePath, request, errorCode)) {
        return false;
    }
    ClearUserChoiceBackup();
    return true;
}

DWORD RunSignedOpenWith(const std::wstring& documentPath) {
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return ERROR_PATH_NOT_FOUND;
    }
    const std::wstring openWithPath =
        std::wstring(systemDirectory) + L"\\OpenWith.exe";
    std::wstring command = L"\"" + openWithPath + L"\" \"" + documentPath +
                           L"\"";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(openWithPath.c_str(), commandBuffer.data(), nullptr,
                        nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &process)) {
        return GetLastError();
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, 180000);
    DWORD exitCode = ERROR_TIMEOUT;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exitCode);
    } else {
        TerminateProcess(process.hProcess, ERROR_TIMEOUT);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}

bool TryOpenWithFallback(HWND owner, const std::wstring& executablePath,
                         const AssociationFileRequest& request,
                         DWORD& errorCode) {
    if (!CopyTextToClipboard(executablePath)) {
        AppendAssociationLog(L"Open With: clipboard copy failed");
    }
    const std::wstring instructions =
        L"即将打开 Windows 的“打开方式”窗口。\n\n"
        L"1. 选择 MDGE Studio。\n"
        L"2. 如果列表中没有，点击“更多应用 → 在这台电脑上查找其他应用”；"
        L"如果出现文件选择框，在文件名中粘贴下面的程序路径。\n"
        L"3. 程序路径：\n" + executablePath + L"\n"
        L"4. 勾选“始终使用此应用”（如果有），然后点击确定。\n\n"
        L"程序路径已复制到剪贴板。";
    if (MessageBoxW(owner, instructions.c_str(), L"使用 Windows 打开方式",
                    MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        errorCode = ERROR_CANCELLED;
        return false;
    }

    std::wstring tempDirectory = AssociationDataDirectory();
    if (tempDirectory.empty()) {
        wchar_t fallbackDirectory[MAX_PATH]{};
        const DWORD tempLength =
            GetTempPathW(MAX_PATH, fallbackDirectory);
        if (tempLength == 0 || tempLength >= MAX_PATH) {
            errorCode = ERROR_PATH_NOT_FOUND;
            return false;
        }
        tempDirectory = fallbackDirectory;
    }
    const wchar_t* extensions[] = {request.mdge ? L".mdge" : nullptr,
                                   request.md ? L".md" : nullptr};
    for (const wchar_t* extension : extensions) {
        if (!extension) {
            continue;
        }
        wchar_t filename[96]{};
        swprintf_s(filename, L"MDGE-openwith-%lu-%llu%s",
                   GetCurrentProcessId(),
                   static_cast<unsigned long long>(GetTickCount64()),
                   extension);
        const std::wstring path = tempDirectory + L"\\" + filename;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        } else {
            AppendAssociationLog(
                L"Open With: temp file creation failed error=" +
                std::to_wstring(GetLastError()));
        }
        // SHOpenWithDialog is blocked by some HIPS products and can return
        // before the Windows-owned picker has finished. Use the signed
        // OpenWith.exe process instead and wait for the user.
        const DWORD openWithExit = RunSignedOpenWith(path);
        AppendAssociationLog(
            L"signed OpenWith.exe exit=" +
            std::to_wstring(openWithExit) + L" extension=" + extension);
        DeleteFileW(path.c_str());
        const AssociationObservation observed =
            ObserveAssociations(executablePath);
        const bool extensionOurs =
            (wcscmp(extension, L".mdge") == 0 && observed.projectIsOurs) ||
            (wcscmp(extension, L".md") == 0 && observed.markdownIsOurs);
        if (!extensionOurs) {
            errorCode = ERROR_ACCESS_DENIED;
            continue;
        }
    }
    if (!VerifyRequestedAssociation(executablePath, request, errorCode)) {
        return false;
    }
    ClearUserChoiceBackup();
    errorCode = ERROR_SUCCESS;
    return true;
}

enum class HuorongRecoveryChoice {
    None,
    OpenWith,
    OpenHuorong,
};

constexpr int kRecoveryOpenWithButton = 2002;
constexpr int kRecoveryHuorongButton = 2003;

HuorongRecoveryChoice ShowHuorongRecoveryDialog(
    HWND owner, const std::wstring& executablePath, const std::wstring& operation,
    DWORD errorCode) {
    const std::wstring content =
        L"检测到火绒安全软件正在运行。火绒的文件关联/行为保护可能拦截了"
        L"未签名程序的注册表写入，因此“" + operation +
        L"”没有完成。\n\n"
        L"错误码：" + std::to_wstring(errorCode) + L"\n"
        L"程序：" + executablePath + L"\n"
        L"日志：" + AssociationLogPath() +
        L"\n\n"
        L"推荐先把上述程序加入火绒信任区，再点击侧栏“修复关联”。\n"
        L"也可以尝试“使用 Windows 打开方式”，由 Windows 资源管理器处理关联。";
    TASKDIALOG_BUTTON buttons[] = {
        {kRecoveryHuorongButton, L"打开火绒信任设置"},
        {kRecoveryOpenWithButton, L"使用 Windows 打开方式"},
    };
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.dwFlags =
        TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = L"文件关联失败";
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = L"火绒安全软件阻止了文件关联";
    config.pszContent = content.c_str();
    config.pButtons = buttons;
    config.cButtons = ARRAYSIZE(buttons);
    config.nDefaultButton = kRecoveryHuorongButton;
    int clicked = 0;
    const HRESULT result = TaskDialogIndirect(&config, &clicked, nullptr,
                                              nullptr);
    if (FAILED(result)) {
        const std::wstring fallbackText =
            L"火绒安全软件阻止了文件关联。\n\n"
            L"是否使用 Windows“打开方式”完成关联？\n"
            L"选择“否”打开火绒主界面；选择“取消”退出。";
        const int fallback = MessageBoxW(
            owner, fallbackText.c_str(), L"文件关联失败",
            MB_YESNOCANCEL | MB_ICONWARNING);
        if (fallback == IDYES) {
            return HuorongRecoveryChoice::OpenWith;
        }
        if (fallback == IDNO) {
            return HuorongRecoveryChoice::OpenHuorong;
        }
        return HuorongRecoveryChoice::None;
    }
    if (clicked == kRecoveryOpenWithButton) {
        return HuorongRecoveryChoice::OpenWith;
    }
    if (clicked == kRecoveryHuorongButton) {
        return HuorongRecoveryChoice::OpenHuorong;
    }
    return HuorongRecoveryChoice::None;
}

bool ShowHuorongTrustHelp(HWND owner, const std::wstring& executablePath) {
    CopyTextToClipboard(executablePath);
    const bool opened = OpenHuorong();
    const std::wstring message =
        L"已将程序路径复制到剪贴板。\n\n"
        L"请在火绒中打开“安全设置 → 信任区/系统加固”，"
        L"把以下程序加入信任区：\n" + executablePath +
        L"\n\n"
        L"完成后回到本程序，点击侧栏底部的“修复关联”重试。\n\n"
        L"如果火绒主界面没有自动打开，请从任务栏托盘打开火绒。";
    MessageBoxW(owner, message.c_str(),
                opened ? L"打开火绒信任设置" : L"打开火绒失败",
                MB_OK | (opened ? MB_ICONINFORMATION : MB_ICONWARNING));
    return opened;
}

struct HelperResult {
    DWORD exitCode = ERROR_SUCCESS;
    bool processStarted = false;
};

HelperResult RunElevatedRegistration(const std::wstring& executablePath,
                                     const AssociationFileRequest& request) {
    HelperResult result;
    const std::wstring expectedSid = CurrentUserSid();
    if (expectedSid.empty()) {
        result.exitCode = ERROR_INVALID_PARAMETER;
        return result;
    }
    const std::wstring parameters =
        BuildRecoveryRegistrationArguments(request, expectedSid);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = executablePath.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        result.exitCode = GetLastError();
        return result;
    }
    result.processStarted = true;
    if (!info.hProcess) {
        result.exitCode = ERROR_INVALID_HANDLE;
        return result;
    }
    const std::wstring helperSid = ProcessUserSid(info.hProcess);
    if (!helperSid.empty() && !SidsEqual(helperSid, expectedSid)) {
        result.exitCode = kAssociationExitSidMismatch;
        TerminateProcess(info.hProcess, kAssociationExitSidMismatch);
        WaitForSingleObject(info.hProcess, 2000);
        CloseHandle(info.hProcess);
        return result;
    }
    const DWORD wait = WaitForSingleObject(info.hProcess, 120000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(info.hProcess, ERROR_TIMEOUT);
        CloseHandle(info.hProcess);
        result.exitCode = ERROR_TIMEOUT;
        return result;
    }
    DWORD exitCode = ERROR_SUCCESS;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    result.exitCode = exitCode;
    return result;
}

bool ShowRecoveryDialog(HWND owner, const std::wstring& operation,
                        DWORD errorCode) {
    const std::wstring message =
        L"无法完成“" + operation + L"”文件关联（错误码 " +
        std::to_wstring(errorCode) +
        L"）。\n\n"
        L"选择“是”：以管理员权限再次尝试（可能触发 Windows 安全提示）。\n"
        L"选择“否”：打开 Windows 默认应用设置手动配置。\n"
        L"选择“取消”：暂不处理，下次启动可重试。";
    const int choice = MessageBoxW(owner, message.c_str(), L"文件关联失败",
                                   MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDYES) {
        return true;
    }
    if (choice == IDNO) {
        OpenDefaultAppsSettings();
    }
    return false;
}

bool TryRecovery(HWND owner, const std::wstring& executablePath,
                 const AssociationFileRequest& request,
                 const std::wstring& operation, DWORD errorCode,
                 bool manual, AssociationSettings& settings) {
    const std::uint64_t now = NowUnixSeconds();
    if (!ShouldPromptForRecovery(settings.lastRecoveryPromptVersion,
                                 settings.lastRecoveryPromptTime, now,
                                 kCurrentRecoveryPromptVersion,
                                 kRecoveryPromptThrottleSeconds, manual)) {
        return false;
    }
    settings.lastRecoveryPromptVersion = kCurrentRecoveryPromptVersion;
    settings.lastRecoveryPromptTime = now;
    PersistSettings(settings);
    AppendAssociationLog(
        L"recovery start operation=" + operation + L" error=" +
        std::to_wstring(errorCode) +
        L" huorong=" + (IsHuorongDetected() ? L"yes" : L"no"));

    if (IsHuorongDetected()) {
        const HuorongRecoveryChoice choice =
            ShowHuorongRecoveryDialog(owner, executablePath, operation,
                                      errorCode);
        AppendAssociationLog(L"huorong recovery choice=" +
                             std::to_wstring(static_cast<int>(choice)));
        if (choice == HuorongRecoveryChoice::OpenWith) {
            DWORD openWithError = ERROR_SUCCESS;
            if (TryOpenWithFallback(owner, executablePath, request,
                                    openWithError)) {
                AppendAssociationLog(L"Open With fallback succeeded");
                return true;
            }
            AppendAssociationLog(L"Open With fallback failed error=" +
                                 std::to_wstring(openWithError));
            if (openWithError == ERROR_CANCELLED) {
                return false;
            }
            MessageBoxW(
                owner,
                (L"Windows“打开方式”未完成关联（错误码 " +
                 std::to_wstring(openWithError) +
                 L"）。\n\n请确认选择了 MDGEStudio.exe 并勾选“始终使用此应用”。")
                    .c_str(),
                L"文件关联失败", MB_OK | MB_ICONWARNING);
            return false;
        }
        if (choice == HuorongRecoveryChoice::OpenHuorong) {
            ShowHuorongTrustHelp(owner, executablePath);
            AppendAssociationLog(L"Huorong trust help opened");
            return false;
        }
        return false;
    }

    if (!ShowRecoveryDialog(owner, operation, errorCode)) {
        return false;
    }
    const HelperResult helper =
        RunElevatedRegistration(executablePath, request);
    if (!helper.processStarted || helper.exitCode != kAssociationExitSuccess) {
        DWORD helperError = helper.exitCode == ERROR_SUCCESS
                                ? ERROR_ACCESS_DENIED
                                : helper.exitCode;
        if (helper.exitCode == kAssociationExitSidMismatch) {
            MessageBoxW(
                owner,
                L"提权进程使用了另一个 Windows 账户，无法为当前用户完成关联。\n\n"
                L"请在“设置 > 应用 > 默认应用”中手动选择 MDGE Studio。",
                L"文件关联失败", MB_OK | MB_ICONWARNING);
            OpenDefaultAppsSettings();
        } else if (helperError == ERROR_CANCELLED) {
            // User cancelled UAC; keep state pending for the next launch.
        } else if (helperError == kUserChoiceRestoreFailed) {
            MessageBoxW(
                owner,
                L"无法恢复原来的 .md 默认打开方式。\n\n"
                L"请在 Windows 默认应用设置中重新选择原来的应用，或重新关联 "
                L"MDGE Studio。",
                L"文件关联失败", MB_OK | MB_ICONWARNING);
            OpenDefaultAppsSettings();
        } else {
            MessageBoxW(
                owner,
                (L"以管理员权限重试仍未成功（错误码 " +
                 std::to_wstring(helperError) +
                 L"）。\n\n请在 Windows 默认应用设置中手动配置。")
                    .c_str(),
                L"文件关联失败", MB_OK | MB_ICONWARNING);
            OpenDefaultAppsSettings();
        }
        return false;
    }
    DWORD verifyError = ERROR_SUCCESS;
    if (!VerifyRequestedAssociation(executablePath, request, verifyError)) {
        MessageBoxW(
            owner,
            (L"提权后仍无法验证关联生效（错误码 " +
             std::to_wstring(verifyError) +
             L"）。\n\n请在 Windows 默认应用设置中手动配置。")
                .c_str(),
            L"文件关联失败", MB_OK | MB_ICONWARNING);
        OpenDefaultAppsSettings();
        return false;
    }
    return true;
}

void RecordFailure(AssociationSettings& settings, DWORD errorCode) {
    settings.lastError = errorCode;
    PersistSettings(settings);
}

void WarnIfUserChoiceRestoreFailed(HWND owner, DWORD errorCode, bool manual,
                                   const AssociationSettings& settings) {
    if (errorCode != kUserChoiceRestoreFailed) {
        return;
    }
    if (!manual && settings.lastError == kUserChoiceRestoreFailed) {
        return;
    }
    MessageBoxW(
        owner,
        L"无法恢复原来的 .md 默认打开方式。\n\n"
        L"请到 Windows 默认应用设置中恢复原应用，或重新选择 MDGE Studio。",
        L"文件关联", MB_OK | MB_ICONWARNING);
}

void EnsureApplicationMetadata(const std::wstring& executablePath,
                               DWORD& errorCode) {
    std::wstring failed;
    const std::vector<RegistryEntry> entries =
        BuildRegistrationEntries(executablePath, false, false);
    ApplyRegistryEntries(entries, errorCode, &failed);
}

void RunAssociationSetup(HWND owner, const std::wstring& executablePath,
                         bool manual) {
    AppendAssociationLog(BuildAssociationDiagnosticSummary(executablePath));
    AppendAssociationLog(
        std::wstring(L"setup start manual=") + (manual ? L"1" : L"0"));

    AssociationSettings settings = LoadSettings();
    // Never trust a persisted "ready" bit across a launch. It is recomputed
    // from the real association below, so a stale value cannot suppress a
    // retry after the user or another installer changes the association.
    settings.projectReady = false;
    PersistSettings(settings);

    if (!settings.version2) {
        settings.version2 = true;
        PersistSettings(settings);
    }

    const AssociationObservation initial =
        ObserveAssociations(executablePath);
    DWORD metadataError = ERROR_SUCCESS;
    EnsureApplicationMetadata(executablePath, metadataError);

    if (initial.markdownIsOurs) {
        settings.markdownPromptDone = true;
        settings.markdownRequested = false;
    }

    AssociationObservation observed = initial;
    UserChoiceBackup orphanBackup;
    if (LoadUserChoiceBackup(orphanBackup) && !observed.markdownIsOurs &&
        !settings.markdownRequested) {
        DWORD ignored = 0;
        if (RestoreUserChoice(orphanBackup, ignored)) {
            ClearUserChoiceBackup();
        } else {
            settings.lastError = kUserChoiceRestoreFailed;
            PersistSettings(settings);
        }
    }

    bool projectReady = observed.projectIsOurs || observed.projectAssociated;
    if (!projectReady) {
        DWORD errorCode = ERROR_SUCCESS;
        RegisterProjectAssociation(executablePath, errorCode);
        observed = ObserveAssociations(executablePath);
        projectReady = observed.projectIsOurs || observed.projectAssociated;
        if (!projectReady) {
            if (errorCode == ERROR_SUCCESS) {
                errorCode = ERROR_ACCESS_DENIED;
            }
            AppendAssociationLog(
                L"direct .mdge association failed error=" +
                std::to_wstring(errorCode));
            const bool recovered = TryRecovery(
                owner, executablePath, AssociationFileRequest{true, false},
                L".mdge 项目", errorCode, manual, settings);
            if (!recovered) {
                RecordFailure(settings, errorCode);
                return;
            }
            observed = ObserveAssociations(executablePath);
            projectReady = observed.projectIsOurs || observed.projectAssociated;
        }
    }

    settings.projectReady = projectReady;
    if (!projectReady) {
        RecordFailure(settings, ERROR_ACCESS_DENIED);
        return;
    }
    PersistSettings(settings);

    if (!observed.markdownIsOurs) {
        if (settings.markdownRequested) {
            DWORD errorCode = ERROR_SUCCESS;
            bool ok = RegisterMarkdownAssociation(executablePath, errorCode);
            observed = ObserveAssociations(executablePath);
            if (ok && observed.markdownIsOurs) {
                settings.markdownPromptDone = true;
                settings.markdownRequested = false;
                settings.lastError = ERROR_SUCCESS;
                PersistSettings(settings);
            } else {
                AppendAssociationLog(
                    L"direct .md association failed error=" +
                    std::to_wstring(errorCode));
                WarnIfUserChoiceRestoreFailed(owner, errorCode, manual,
                                              settings);
                if (manual) {
                    if (TryRecovery(owner, executablePath,
                                    AssociationFileRequest{false, true},
                                    L".md 文档", errorCode, manual, settings)) {
                        observed = ObserveAssociations(executablePath);
                        if (observed.markdownIsOurs) {
                            settings.markdownPromptDone = true;
                            settings.markdownRequested = false;
                            settings.lastError = ERROR_SUCCESS;
                            PersistSettings(settings);
                        }
                    } else {
                        RecordFailure(settings, errorCode);
                    }
                } else {
                    RecordFailure(settings, errorCode);
                }
            }
        } else if (!settings.markdownPromptDone || manual) {
            std::wstring message =
                L"是否将 .md 文件关联到 MDGE Studio？\n\n"
                L"选择“是”后，双击 .md 文件会使用 MDGE Studio 打开。";
            if (observed.markdownAssociated) {
                message = L"当前 .md 文件由其他程序打开。\n\n" + message;
            }
            const int choice = MessageBoxW(
                owner, message.c_str(), L"文件关联",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
            if (choice == IDNO) {
                settings.markdownPromptDone = true;
                settings.markdownRequested = false;
                settings.lastError = ERROR_SUCCESS;
                PersistSettings(settings);
            } else if (choice == IDYES) {
                settings.markdownRequested = true;
                PersistSettings(settings);
                DWORD errorCode = ERROR_SUCCESS;
                bool ok = RegisterMarkdownAssociation(executablePath, errorCode);
                observed = ObserveAssociations(executablePath);
                if (ok && observed.markdownIsOurs) {
                    settings.markdownPromptDone = true;
                    settings.markdownRequested = false;
                    settings.lastError = ERROR_SUCCESS;
                    PersistSettings(settings);
                } else {
                    AppendAssociationLog(
                        L"direct .md association failed error=" +
                        std::to_wstring(errorCode));
                    WarnIfUserChoiceRestoreFailed(owner, errorCode, manual,
                                                  settings);
                    if (TryRecovery(owner, executablePath,
                                    AssociationFileRequest{false, true},
                                    L".md 文档", errorCode, manual, settings)) {
                        observed = ObserveAssociations(executablePath);
                        if (observed.markdownIsOurs) {
                            settings.markdownPromptDone = true;
                            settings.markdownRequested = false;
                            settings.lastError = ERROR_SUCCESS;
                            PersistSettings(settings);
                        }
                    } else {
                        RecordFailure(settings, errorCode);
                    }
                }
            }
        }
    } else {
        settings.markdownPromptDone = true;
        settings.markdownRequested = false;
        PersistSettings(settings);
    }

    if (settings.projectReady && (settings.markdownPromptDone ||
                                  observed.markdownIsOurs)) {
        settings.version2 = true;
        PersistSettings(settings);
    }
}

}  // namespace

int RunFileAssociationHelper(const std::vector<std::wstring>& arguments) {
    AssociationFileRequest request;
    std::wstring expectedSid;
    if (!ParseRecoveryRegistrationArguments(arguments, request, expectedSid)) {
        return kAssociationExitSidMismatch;
    }
    if (!SidsEqual(CurrentUserSid(), expectedSid)) {
        return kAssociationExitSidMismatch;
    }
    const std::wstring executablePath = ExecutablePath();
    if (executablePath.empty()) {
        return ERROR_INVALID_PARAMETER;
    }

    DWORD errorCode = ERROR_SUCCESS;
    if (!RegisterSilent(executablePath, request, errorCode)) {
        if (errorCode == ERROR_SUCCESS) {
            errorCode = ERROR_ACCESS_DENIED;
        }
        return static_cast<int>(errorCode);
    }
    if (!VerifyRequestedAssociation(executablePath, request, errorCode)) {
        return static_cast<int>(errorCode == ERROR_SUCCESS ? ERROR_ACCESS_DENIED
                                                           : errorCode);
    }
    return kAssociationExitSuccess;
}

void InitializeFileAssociations(HWND owner, const std::wstring& executablePath) {
    if (executablePath.empty()) {
        return;
    }
    RunAssociationSetup(owner, executablePath, false);
}

void RepairFileAssociations(HWND owner, const std::wstring& executablePath) {
    if (executablePath.empty()) {
        return;
    }
    RunAssociationSetup(owner, executablePath, true);
    const AssociationObservation observed =
        ObserveAssociations(executablePath);
    if (observed.projectIsOurs && observed.markdownIsOurs) {
        MessageBoxW(owner, L"文件关联已修复。", L"文件关联",
                    MB_OK | MB_ICONINFORMATION);
    } else if (observed.projectIsOurs) {
        MessageBoxW(owner,
                    L".mdge 关联已修复；.md 尚未关联到本程序。\n"
                    L"可以再次点击“修复关联”或在 Windows 默认应用设置中调整。",
                    L"文件关联", MB_OK | MB_ICONWARNING);
    } else if (observed.projectAssociated) {
        MessageBoxW(owner,
                    L".mdge 已由其他程序关联，按设计不覆盖该默认程序；\n"
                    L".md 可在 Windows 默认应用设置中调整，或再次点击“修复关联”。",
                    L"文件关联", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(owner,
                    L"仍未完成文件关联。请查看错误信息，或使用 Windows "
                    L"默认应用设置手动关联。",
                    L"文件关联", MB_OK | MB_ICONWARNING);
    }
}

}  // namespace mdgeui
