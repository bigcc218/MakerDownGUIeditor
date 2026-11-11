#include "association_diagnostics.h"

#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mdgeui {
namespace {

std::string Utf8Encode(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                          static_cast<int>(text.size()), nullptr,
                                          0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string encoded(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        encoded.data(), bytes, nullptr, nullptr);
    return encoded;
}

bool ProcessExists(const wchar_t* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

std::wstring KnownProgramFilesPath(const wchar_t* relativePath) {
    wchar_t programFiles[MAX_PATH]{};
    const UINT length = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles,
                                                MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const std::wstring candidate =
            std::wstring(programFiles) + L"\\" + relativePath;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return candidate;
        }
    }
    const UINT normalLength =
        GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
    if (normalLength > 0 && normalLength < MAX_PATH) {
        const std::wstring candidate =
            std::wstring(programFiles) + L"\\" + relativePath;
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return candidate;
        }
    }
    return {};
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const bool elevated =
        GetTokenInformation(token, TokenElevation, &elevation,
                            sizeof(elevation), &bytes) &&
        elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

}  // namespace

std::wstring AssociationDataDirectory() {
    PWSTR localAppData = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                       nullptr, &localAppData))) {
        base = localAppData;
        CoTaskMemFree(localAppData);
    }
    if (base.empty()) {
        wchar_t temp[MAX_PATH]{};
        const DWORD length = GetTempPathW(MAX_PATH, temp);
        if (length > 0 && length < MAX_PATH) {
            base = temp;
        }
    }
    if (base.empty()) {
        return {};
    }
    const std::wstring directory = base + L"\\MDGE Studio";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

std::wstring AssociationLogPath() {
    const std::wstring directory = AssociationDataDirectory();
    if (directory.empty()) {
        return {};
    }
    return directory + L"\\association.log";
}

void AppendAssociationLog(const std::wstring& message) {
    const std::wstring path = AssociationLogPath();
    if (path.empty()) {
        return;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t stamp[64]{};
    swprintf_s(stamp, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
               time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
               time.wSecond);
    const std::wstring line = std::wstring(stamp) + message + L"\r\n";
    const std::string encoded = Utf8Encode(line);
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, encoded.data(), static_cast<DWORD>(encoded.size()), &written,
              nullptr);
    CloseHandle(file);
}

bool IsHuorongDetected() {
    if (ProcessExists(L"HipsDaemon.exe") ||
        ProcessExists(L"HipsTray.exe")) {
        return true;
    }
    return !HuorongInstallDirectory().empty();
}

std::wstring HuorongInstallDirectory() {
    const std::wstring tray = KnownProgramFilesPath(
        L"Huorong\\Sysdiag\\bin\\HipsTray.exe");
    if (!tray.empty()) {
        const std::size_t slash = tray.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : tray.substr(0, slash);
    }
    return {};
}

bool OpenHuorong() {
    const std::wstring tray = KnownProgramFilesPath(
        L"Huorong\\Sysdiag\\bin\\HipsTray.exe");
    if (!tray.empty()) {
        const HINSTANCE result =
            ShellExecuteW(nullptr, L"open", tray.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) > 32) {
            return true;
        }
    }
    const std::wstring directory = HuorongInstallDirectory();
    if (!directory.empty()) {
        ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return true;
    }
    return false;
}

bool CopyTextToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) {
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
    memcpy(target, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

std::wstring BuildAssociationDiagnosticSummary(
    const std::wstring& executablePath) {
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    GetVersionExW(&version);
    wchar_t line[512]{};
    swprintf_s(line,
               L"OS=%u.%u.%u exe=%ls elevated=%ls huorong=%ls data=%ls",
               version.dwMajorVersion, version.dwMinorVersion,
               version.dwBuildNumber, executablePath.c_str(),
               IsElevated() ? L"yes" : L"no",
               IsHuorongDetected() ? L"yes" : L"no",
               AssociationDataDirectory().c_str());
    return line;
}

}  // namespace mdgeui
