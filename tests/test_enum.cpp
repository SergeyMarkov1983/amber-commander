// Тест перечисления каталогов: C:\Windows\System32 и весь диск C: рекурсивно.
// Сборка: clang++ -std=c++20 -O2 <флаги> src\core\pathutil.cpp src\core\fsenum.cpp
//         tests\test_enum.cpp -luser32 -lshell32 -o tests\test_enum.exe
#include "common.h"
#include "fsenum.h"
#include "pathutil.h"
#include <cwchar>
#include <cstdarg>
#include <cstdio>

// Доступность NT-пути (определена в fsenum.cpp, в заголовок не выносится).
bool FsEnumNtAvailable();

// Вывод: если stdout — консоль, пишем через WriteConsoleW (юникод),
// если перенаправлен — UTF-8 байтами в поток.
static void Print(const std::wstring& s) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out && out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
        DWORD w = 0;
        WriteConsoleW(out, s.c_str(), (DWORD)s.size(), &w, nullptr);
        return;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string u8((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), u8.data(), n, nullptr, nullptr);
    fwrite(u8.data(), 1, u8.size(), stdout);
    fflush(stdout);
}

static void PrintLn(const std::wstring& s) { Print(s + L"\r\n"); }

static std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 512, _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
}

struct Stats {
    uint64_t files = 0, dirs = 0, entries = 0;
};

// Своя простая рекурсия (junction'ы пропускаем — иначе циклы на C:).
void Walk(const std::wstring& dir, std::atomic<bool>* cancel, Stats& s) {
    auto items = EnumerateDir(dir, cancel);
    s.entries += items.size();
    for (auto& e : items) {
        if (cancel->load()) return;
        if (e.isDir) {
            if (e.attrs & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            s.dirs++;
            Walk(PathJoin(dir, e.name), cancel, s);
        } else {
            s.files++;
        }
    }
}

int main() {
    PrintLn(L"=== Тест перечисления (fsenum) ===");
    PrintLn(Fmt(L"Движок NT (NtQueryDirectoryFile): %s",
                FsEnumNtAvailable() ? L"доступен" : L"НЕ доступен (работает fallback)"));

    // 1) System32
    std::atomic<bool> cancel{false};
    auto t0 = GetTickCount64();
    auto items = EnumerateDir(L"C:\\Windows\\System32", &cancel);
    auto t1 = GetTickCount64();
    double sec = double(t1 - t0) / 1000.0;
    PrintLn(Fmt(L"1) C:\\Windows\\System32: %llu записей за %s  ->  %.0f записей/с",
                (unsigned long long)items.size(),
                sec < 1.0 ? Fmt(L"%.1f мс", sec * 1000.0).c_str() : Fmt(L"%.2f с", sec).c_str(),
                sec > 0 ? items.size() / sec : 0.0));

    // Проверка семантики: без . и .., имена без пути, непустые.
    int bad = 0;
    for (auto& e : items) {
        if (e.name == L"." || e.name == L"..") bad++;
        if (e.name.find(L'\\') != std::wstring::npos) bad++;
        if (e.name.empty()) bad++;
    }
    PrintLn(Fmt(L"   Семантика: %s (нет . / .., имена без пути, непустые)",
                bad == 0 ? L"OK" : L"НАРУШЕНА"));

    // 2) Весь диск C: рекурсивно (своя рекурсия).
    PrintLn(L"2) Рекурсивный обход всего диска C: (может занять время)");
    Stats s;
    auto c0 = GetTickCount64();
    Walk(L"C:\\", &cancel, s);
    auto c1 = GetTickCount64();
    double sec2 = double(c1 - c0) / 1000.0;
    PrintLn(Fmt(L"   файлов: %llu, каталогов: %llu, записей: %llu за %.2f с  ->  %.0f записей/с",
                (unsigned long long)s.files, (unsigned long long)s.dirs,
                (unsigned long long)s.entries, sec2,
                sec2 > 0 ? s.entries / sec2 : 0.0));

    // 3) Недоступный каталог — не должен ронять процесс.
    auto denied = EnumerateDir(L"C:\\System Volume Information");
    PrintLn(Fmt(L"3) Недоступный каталог: вернулось %llu записей (без падения)",
                (unsigned long long)denied.size()));

    PrintLn(L"=== Готово ===");
    return 0;
}
