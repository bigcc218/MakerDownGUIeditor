@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build

rem Keep this file ASCII-only and CRLF-terminated: cmd.exe parses batch files
rem using the OEM code page, so non-ASCII comments can corrupt the parser.
rem --codepage=65001 makes windres read app.rc as UTF-8; --language=0x0804
rem pins resource strings to the zh-CN block instead of relying on defaults.
set RESFLAGS=--codepage=65001 --language=0x0804 -Iresources

echo [1/4] Building resources...
windres %RESFLAGS% resources\app.rc -O coff -o build\app.res.o
if errorlevel 1 goto :failed
if not exist build\app.res.o goto :failed

echo [2/4] Compiling MDGE Studio...
g++ -std=c++20 -O2 -Wall -Wextra -municode -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE -Isrc -Iresources ^
  src\app\main.cpp src\app\app.cpp src\app\app_draw.cpp src\app\app_edit.cpp src\app\app_input.cpp src\app\app_source.cpp src\app\app_files.cpp src\app\file_associations.cpp src\app\file_association_core.cpp src\app\association_diagnostics.cpp src\app\association_state_store.cpp src\app\dialogs.cpp src\app\ui_util.cpp src\app\image_loader.cpp ^
  src\core\model.cpp src\core\utf8.cpp src\core\file_utils.cpp src\core\path_utils.cpp src\core\markdown.cpp src\core\project_io.cpp ^
  build\app.res.o -o build\MDGEStudio.exe ^
  -static -static-libgcc -static-libstdc++ -s ^
  -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lshell32 -lshlwapi -limm32 -lcomdlg32 -lgdi32 -luser32 -loleaut32 -ldwmapi -lcomctl32 -ladvapi32
if errorlevel 1 goto :failed

echo [3/4] Building tests...
g++ -std=c++20 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE -Isrc ^
  tests\core_tests.cpp src\core\model.cpp src\core\utf8.cpp src\core\file_utils.cpp src\core\path_utils.cpp src\core\markdown.cpp src\core\project_io.cpp ^
  -o build\core_tests.exe -static -static-libgcc -static-libstdc++ -lshlwapi
if errorlevel 1 goto :failed
g++ -std=c++20 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE -Isrc ^
  tests\association_tests.cpp src\app\file_association_core.cpp src\app\association_state_store.cpp src\app\association_diagnostics.cpp ^
  -o build\association_tests.exe -static -static-libgcc -static-libstdc++ -lshell32 -lole32 -luuid
if errorlevel 1 goto :failed

build\core_tests.exe
if errorlevel 1 goto :failed
build\association_tests.exe
if errorlevel 1 goto :failed

echo [4/4] Build complete.

echo.
echo Build complete: build\MDGEStudio.exe
echo.
rem CI runs "build.bat --ci": no pause, and the error level is propagated.
if /i "%~1"=="--ci" exit /b 0
pause
exit /b 0

:failed
echo.
echo Build failed.
echo.
if /i "%~1"=="--ci" exit /b 1
pause
exit /b 1
