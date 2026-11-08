#include "app.h"
#include "file_associations.h"
#include "resource.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance,
                    PWSTR commandLine, int showCommand) {
    (void)previousInstance;
    (void)commandLine;

    // The manifest also declares PerMonitorV2 awareness; the runtime call keeps
    // behavior correct when the executable is launched by older hosts.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),
                                  ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);

    std::wstring initialPath;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments) {
        if (argumentCount >= 2) {
            initialPath = arguments[1];
            if (GetFileAttributesW(initialPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                initialPath.clear();
            }
        }
        LocalFree(arguments);
    }

    mdgeui::App app;
    if (!app.Create(instance, initialPath)) {
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
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return exitCode;
}
