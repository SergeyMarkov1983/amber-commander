// Тест движка копирования: 1 ГБ файл (тот же диск и другой диск) + 5000 мелких.
// Сборка: clang++ -std=c++20 -O2 <флаги> src\core\pathutil.cpp src\core\fsenum.cpp
//         src\core\fileops.cpp tests\test_copy.cpp -luser32 -lshell32 -o tests\test_copy.exe
#include "common.h"
#include "fileops.h"
#include "pathutil.h"
#include "fsenum.h"
#include <cwchar>
#include <cstdarg>
#include <cstdio>

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

static uint64_t FileSizeOf(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(LongPath(p).c_str(), GetFileExInfoStandard, &d)) return ~0ull;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

// Генерация файла псевдослучайным потоком (несжимаемым) — честный бенч.
static bool WriteBigFile(const std::wstring& path, uint64_t total, uint64_t* written) {
    HANDLE h = CreateFileW(LongPath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const DWORD kChunk = 4u << 20;
    std::vector<char> buf(kChunk);
    uint32_t x = 0x12345678u;
    for (auto& c : buf) { x = x * 1664525u + 1013904223u; c = (char)(x >> 24); }
    uint64_t left = total;
    while (left) {
        DWORD n = (DWORD)(left < kChunk ? left : kChunk);
        DWORD wr = 0;
        if (!WriteFile(h, buf.data(), n, &wr, nullptr) || wr != n) {
            CloseHandle(h);
            return false;
        }
        left -= wr;
        if (written) *written += wr;
    }
    CloseHandle(h);
    return true;
}

static bool ReadAll(const std::wstring& path, std::vector<char>& out) {
    HANDLE h = CreateFileW(LongPath(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    bool ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size();
    CloseHandle(h);
    return ok;
}

int main() {
    PrintLn(L"=== Тест движка копирования (fileops) ===");
    std::atomic<bool> cancel{false}, pause{false};
    const uint64_t kBig = 1024ull * 1024ull * 1024ull;  // 1 ГБ

    wchar_t tmp[512];
    GetTempPathW(512, tmp);
    std::wstring root = PathNormalize(std::wstring(tmp)) + L"\\amber_bench";
    DeleteDirect({ root });
    CreateDirectoryW(LongPath(root).c_str(), nullptr);

    // (a1) 1 ГБ, тот же диск
    std::wstring bigSrc = PathJoin(root, L"big_1g.bin");
    PrintLn(L"[1/4] Генерация файла 1 ГБ...");
    auto g0 = GetTickCount64();
    if (!WriteBigFile(bigSrc, kBig, nullptr)) {
        PrintLn(L"ОШИБКА генерации (нет свободного места?)");
        return 1;
    }
    auto g1 = GetTickCount64();
    double gsec = double(g1 - g0) / 1000.0;
    PrintLn(Fmt(L"      сгенерирован за %.2f с (%.0f МБ/с записи)",
                gsec, double(kBig) / (1u << 20) / gsec));

    std::wstring dstSame = PathJoin(root, L"copy_same.bin");
    CopyJob job{ bigSrc, dstSame };
    ProgressFn prog = [](const CopyProgress&) {};
    auto t0 = GetTickCount64();
    CopyOutcome res = RunCopy({ job }, CopyOptions{}, prog, nullptr, cancel, pause);
    auto t1 = GetTickCount64();
    double sec = double(t1 - t0) / 1000.0;
    for (auto& e : res.errors) PrintLn(L"      ошибка: " + e);
    PrintLn(Fmt(L"[2/4] Копия 1 ГБ (тот же диск): %s, %.2f с  ->  %.0f МБ/с",
                res.ok ? L"OK" : L"ОШИБКА", sec,
                double(kBig) / (1u << 20) / sec));
    PrintLn(Fmt(L"      проверка размера: %s",
                FileSizeOf(dstSame) == kBig ? L"совпадает" : L"НЕ СОВПАДАЕТ"));

    // (a2) 1 ГБ на другой диск (D:), если есть
    if (GetDriveTypeW(L"D:\\") != DRIVE_NO_ROOT_DIR) {
        std::wstring cross = L"D:\\amber_bench_cross";
        DeleteDirect({ cross });
        if (CreateDirectoryW(LongPath(cross).c_str(), nullptr)) {
            std::wstring dstCross = PathJoin(cross, L"copy_cross.bin");
            CopyJob job2{ bigSrc, dstCross };
            auto c0 = GetTickCount64();
            CopyOutcome res2 = RunCopy({ job2 }, CopyOptions{}, prog, nullptr, cancel, pause);
            auto c1 = GetTickCount64();
            double sec2 = double(c1 - c0) / 1000.0;
            PrintLn(Fmt(L"[3/4] Копия 1 ГБ (на D:): %s, %.2f с  ->  %.0f МБ/с",
                        res2.ok ? L"OK" : L"ОШИБКА", sec2,
                        double(kBig) / (1u << 20) / sec2));
            PrintLn(Fmt(L"      проверка размера: %s",
                        FileSizeOf(dstCross) == kBig ? L"совпадает" : L"НЕ СОВПАДАЕТ"));
            DeleteDirect({ cross });
        } else {
            PrintLn(L"[3/4] Диск D: есть, но недоступен для записи — пропуск");
        }
    } else {
        PrintLn(L"[3/4] Диска D: нет — пропуск");
    }

    // (б) 5000 мелких файлов по 10 КБ
    std::wstring smallSrc = PathJoin(root, L"small_src");
    CreateDirectoryW(LongPath(smallSrc).c_str(), nullptr);
    const int kNFiles = 5000;
    const DWORD kFileSize = 10 * 1024;
    PrintLn(Fmt(L"[4/4] Генерация %d файлов по %u КБ...", kNFiles, kFileSize / 1024));
    std::vector<char> smallBuf(kFileSize, 'x');
    for (int i = 0; i < kNFiles; i++) {
        wchar_t name[64];
        swprintf(name, 64, L"f%05d.dat", i);
        HANDLE h = CreateFileW(LongPath(PathJoin(smallSrc, name)).c_str(), GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { PrintLn(L"ОШИБКА генерации мелких файлов"); return 1; }
        DWORD wr = 0;
        WriteFile(h, smallBuf.data(), kFileSize, &wr, nullptr);
        CloseHandle(h);
    }
    std::wstring smallDst = PathJoin(root, L"small_dst");
    CopyJob job3{ smallSrc, smallDst };
    auto s0 = GetTickCount64();
    CopyOutcome res3 = RunCopy({ job3 }, CopyOptions{}, prog, nullptr, cancel, pause);
    auto s1 = GetTickCount64();
    double sec3 = double(s1 - s0) / 1000.0;
    PrintLn(Fmt(L"      Копия каталога из %d файлов: %s, %.2f с  ->  %.0f файлов/с",
                kNFiles, res3.ok ? L"OK" : L"ОШИБКА", sec3, kNFiles / sec3));
    auto check = EnumerateDir(smallDst);
    PrintLn(Fmt(L"      проверка: в приёмнике %llu файлов %s",
                (unsigned long long)check.size(),
                check.size() == (size_t)kNFiles ? L"(совпадает)" : L"(НЕ СОВПАДАЕТ)"));

    // (в) корректность: побайтовое сравнение копии (~1.2 МБ)
    std::wstring cSrc = PathJoin(root, L"check.bin");
    uint64_t cSize = 1234567;
    WriteBigFile(cSrc, cSize, nullptr);
    std::wstring cDst = PathJoin(root, L"check_out.bin");
    CopyJob job4{ cSrc, cDst };
    CopyOutcome res4 = RunCopy({ job4 }, CopyOptions{}, prog, nullptr, cancel, pause);
    for (auto& e : res4.errors) PrintLn(L"      ошибка: " + e);
    std::vector<char> a, b;
    bool same = ReadAll(cSrc, a) && ReadAll(cDst, b) && a == b;
    PrintLn(Fmt(L"      Контроль содержимого (%u байт): %s",
                (unsigned)cSize, same ? L"байт-в-байт совпадает" : L"РАСХОЖДЕНИЕ"));

    DeleteDirect({ root });
    PrintLn(L"=== Готово ===");
    return 0;
}
