#include "app.h"
#include "file_association_core.h"
#include "file_associations.h"
#include "resource.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kSingleInstanceMutex[] =
    L"Local\\MDGEStudio.SingleInstance.v1";

bool SendPathToExistingInstance(HWND window, const std::wstring& path) {
    if (path.empty()) {
        return true;
    }
    std::vector<unsigned char> buffer(
        sizeof(DWORD) + (path.size() + 1) * sizeof(wchar_t));
    const DWORD magic = mdgeui::kOpenFileCopyDataMagic;
    std::memcpy(buffer.data(), &magic, sizeof(magic));
    std::memcpy(buffer.data() + sizeof(magic), path.c_str(),
                (path.size() + 1) * sizeof(wchar_t));
    COPYDATASTRUCT data{};
    data.dwData = 0;
    data.cbData = static_cast<DWORD>(buffer.size());
    data.lpData = buffer.data();
    DWORD_PTR ignored = 0;
    const LRESULT result = SendMessageTimeoutW(
        window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
        SMTO_ABORTIFHUNG, 5000, &ignored);
    return result != 0;
}

void ActivateExistingInstance(HWND window, const std::wstring& path) {
    if (!window) {
        return;
    }
    if (!path.empty()) {
        SendPathToExistingInstance(window, path);
    }
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance,
                    PWSTR commandLine, int showCommand) {
    (void)previousInstance;
    (void)commandLine;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::vector<std::wstring> arguments;
    std::wstring initialPath;
    int argumentCount = 0;
    LPWSTR* rawArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (rawArguments) {
        for (int index = 1; index < argumentCount; ++index) {
            arguments.emplace_back(rawArguments[index]);
        }
        if (!arguments.empty()) {
            initialPath = arguments.front();
            if (GetFileAttributesW(initialPath.c_str()) ==
                INVALID_FILE_ATTRIBUTES) {
                initialPath.clear();
            }
        }
        LocalFree(rawArguments);
    }

    bool helperMode = false;
    for (const std::wstring& argument : arguments) {
        if (argument.rfind(L"--register-file-associations=", 0) == 0) {
            helperMode = true;
            break;
        }
    }
    if (helperMode) {
        return mdgeui::RunFileAssociationHelper(arguments);
    }

    SetLastError(ERROR_SUCCESS);
    HANDLE singleInstance =
        CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (singleInstance &&
        GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = nullptr;
        for (int attempt = 0; attempt < 100; ++attempt) {
            existing = FindWindowW(mdgeui::kMainWindowClassName, nullptr);
            if (existing) {
                break;
            }
            Sleep(50);
        }
        ActivateExistingInstance(existing, initialPath);
        CloseHandle(singleInstance);
        return 0;
    }

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),
                                  ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);

    mdgeui::App app;
    if (!app.Create(instance, initialPath)) {
        if (singleInstance) {
            CloseHandle(singleInstance);
        }
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 1;
    }

    ShowWindow(app.Window(), showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(app.Window());
    SetForegroundWindow(app.Window());
    SetFocus(app.Window());

    std::wstring executablePath(32768, L'\0');
    const DWORD executableLength =
        GetModuleFileNameW(nullptr, executablePath.data(),
                           static_cast<DWORD>(executablePath.size()));
    if (executableLength > 0) {
        executablePath.resize(executableLength);
        mdgeui::InitializeFileAssociations(app.Window(), executablePath);
    }

    const int exitCode = app.RunMessageLoop();
    if (singleInstance) {
        CloseHandle(singleInstance);
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return exitCode;
}
