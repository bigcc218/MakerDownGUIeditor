@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build

echo [1/3] Building resources...
windres --codepage=65001 -Iresources resources\app.rc -O coff -o build\app.res.o
if errorlevel 1 goto :failed

echo [2/3] Compiling MDGE Studio...
g++ -std=c++20 -O2 -municode -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE -Isrc -Iresources ^
  src\app\main.cpp src\app\app.cpp src\app\app_draw.cpp src\app\app_edit.cpp src\app\app_input.cpp src\app\app_source.cpp src\app\app_files.cpp src\app\file_associations.cpp src\app\dialogs.cpp src\app\ui_util.cpp src\app\image_loader.cpp ^
  src\core\model.cpp src\core\utf8.cpp src\core\file_utils.cpp src\core\path_utils.cpp src\core\markdown.cpp src\core\project_io.cpp ^
  build\app.res.o -o build\MDGEStudio.exe ^
  -static -static-libgcc -static-libstdc++ -s ^
  -ld2d1 -ldwrite -lwindowscodecs -lole32 -luuid -lshell32 -lshlwapi -limm32 -lcomdlg32 -lgdi32 -luser32 -loleaut32 -ldwmapi -lcomctl32 -ladvapi32
if errorlevel 1 goto :failed

echo [3/3] Building tests...
g++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE -Isrc ^
  tests\core_tests.cpp src\core\model.cpp src\core\utf8.cpp src\core\file_utils.cpp src\core\path_utils.cpp src\core\markdown.cpp src\core\project_io.cpp ^
  -o build\core_tests.exe -static -static-libgcc -static-libstdc++ -lshlwapi
if errorlevel 1 goto :failed

build\core_tests.exe
if errorlevel 1 goto :failed

echo.
echo Build complete: build\MDGEStudio.exe
echo.
pause
exit /b 0

:failed
echo.
echo Build failed.
echo.
pause
exit /b 1
