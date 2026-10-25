#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
// Макросы user32, конфликтующие с Direct2D/DirectWrite
#ifdef DrawText
#undef DrawText
#endif
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>

// Запись каталога (только имя, без пути)
struct DirEntry {
    std::wstring name;
    uint64_t size = 0;
    FILETIME mtime{};
    DWORD attrs = 0;
    bool isDir = false;
};

// Локально-независимое приведение к нижнему регистру (кириллица + латиница).
// towlower в C-локали не сворачивает кириллицу — отсюда «к» не находила «КНИГИ».
inline wchar_t LowerChar(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
    if (c >= L'А' && c <= L'Я') return (wchar_t)(c + 32);
    if (c == L'Ё') return L'ё';
    return (wchar_t)towlower(c);
}

// Регистронезависимое сравнение строк, не зависящее от локали.
inline int WcsICmp(const wchar_t* a, const wchar_t* b) {
    while (*a && *b) {
        wchar_t la = LowerChar(*a), lb = LowerChar(*b);
        if (la != lb) return la < lb ? -1 : 1;
        a++;
        b++;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

// Кастомные сообщения окна
constexpr UINT WM_APP_DIRLOADED    = WM_APP + 10; // wParam=panel idx, lParam=LoadMsg*
constexpr UINT WM_APP_ASKOVERWRITE = WM_APP + 11; // lParam=AskCtx* (определён в app_commands.cpp)
constexpr UINT WM_APP_COPYDONE     = WM_APP + 12; // lParam=CopyCtx*
constexpr UINT WM_APP_COPYTICK     = WM_APP + 13; // обновление прогресса
constexpr UINT WM_APP_SEARCHHIT    = WM_APP + 14; // найден результат
constexpr UINT WM_APP_SEARCHDONE   = WM_APP + 15; // поиск завершён
constexpr UINT WM_APP_DRIVEHOTKEY  = WM_APP + 16; // Alt+F1/F2 через LL-хук (wParam: 0=левая, 1=правая)
constexpr UINT WM_APP_SIZEREADY    = WM_APP + 17; // готовы размеры каталогов (lParam=SizeMsg*)
constexpr UINT WM_APP_QVREADY      = WM_APP + 18; // готово превью быстрого просмотра (wParam=gen, lParam=QvData*)

struct LoadMsg {
    uint64_t gen;
    std::vector<DirEntry>* items;
};
