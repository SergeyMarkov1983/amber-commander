@echo off
setlocal
rem ===== Amber Commander build script (LLVM-MinGW clang++) =====
set CC=clang++
set FLAGS=-std=c++20 -O2 -municode -mwindows -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -Isrc -Isrc\ui -Isrc\core
set LIBS=-luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -luuid -ld2d1 -ldwrite -lwindowscodecs -ladvapi32 -ldwmapi

if not exist res\amber.ico powershell -NoProfile -ExecutionPolicy Bypass -File make_icons.ps1
windres res\amber.rc res\amber_res.o || exit /b 1

%CC% %FLAGS% ^
  src\main.cpp src\app.cpp src\app_commands.cpp src\dialogs.cpp src\config.cpp src\theme.cpp src\icons.cpp ^
  src\core\pathutil.cpp src\core\wildcard.cpp src\core\fsenum.cpp src\core\fileops.cpp src\core\search.cpp src\core\volumes.cpp src\core\mftscan.cpp src\core\folderidx.cpp src\core\shellmenu.cpp ^
  src\ui\render.cpp src\ui\dialog.cpp src\ui\panel.cpp src\ui\window.cpp src\ui\viewer.cpp ^
  res\amber_res.o %LIBS% -o amber.exe || exit /b 1

echo.
echo Build OK: amber.exe
