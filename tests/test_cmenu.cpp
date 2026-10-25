// tests/test_cmenu.cpp — проверка ShowShellContextMenu
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         -D_WIN32_WINNT=0x0A00 -Isrc -Isrc\core tests\test_cmenu.cpp
//         src\core\shellmenu.cpp -luser32 -lshell32 -lole32 -static -o tests\test_cmenu.exe
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <string>
#include "shellmenu.h"

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring path = L"C:\\Windows\\notepad.exe";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        path = L"C:\\Windows\\System32\\notepad.exe";
    printf("Путь: %ls\n", path.c_str());
    bool ok = ShowShellContextMenu(nullptr, 400, 400, { path });
    printf("Меню показано и обработано, выполнена команда: %s\n", ok ? "да" : "нет");
    CoUninitialize();
    return 0;
}
