// tests/test_mv.cpp — замер скорости перемещения (RunCopy с move=true)
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         -D_WIN32_WINNT=0x0A00 -Isrc -Isrc\core tests\test_mv.cpp
//         src\core\fileops.cpp src\core\fsenum.cpp src\core\wildcard.cpp src\core\pathutil.cpp
//         -luser32 -lshell32 -static -o tests\test_mv.exe
#include <windows.h>
#include <cstdio>
#include <string>
#include <atomic>
#include "fileops.h"
#include "fsenum.h"
#include "pathutil.h"

static std::atomic<bool> g_cancel{ false };
static std::atomic<bool> g_pause{ false };

static bool MakeDir(const std::wstring& p) {
    return CreateDirectoryW(LongPath(p).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool MakeFile(const std::wstring& p, uint64_t bytes) {
    HANDLE h = CreateFileW(LongPath(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<char> buf(1u << 20, 'x');
    uint64_t left = bytes;
    while (left) {
        DWORD n = (DWORD)std::min<uint64_t>(left, buf.size());
        DWORD w = 0;
        if (!WriteFile(h, buf.data(), n, &w, nullptr)) break;
        left -= w;
    }
    CloseHandle(h);
    return left == 0;
}

static bool Exists(const std::wstring& p) { return FileExists(p) || DirExists(p); }

static std::wstring GetEnvVar(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return n ? std::wstring(buf, n) : std::wstring();
}

int main() {
    std::wstring tmp = PathJoin(GetEnvVar(L"TEMP"), L"amber_mv_test");
    // очистка
    {
        auto items = EnumerateDir(tmp, nullptr);
        for (auto& e : items) DeleteDirect({ PathJoin(tmp, e.name) });
    }
    MakeDir(tmp);
    MakeDir(PathJoin(tmp, L"from"));
    MakeDir(PathJoin(tmp, L"to"));

    printf("=== 1) Перемещение 1 ГБ в пределах одного тома ===\n");
    MakeFile(PathJoin(tmp, L"from\\big.bin"), 1ull << 30);
    {
        ULONGLONG t0 = GetTickCount64();
        CopyOptions o;
        o.move = true;
        CopyOutcome r = RunCopy({ { PathJoin(tmp, L"from\\big.bin"), PathJoin(tmp, L"to\\big.bin") } },
                                o, nullptr, nullptr, g_cancel, g_pause);
        ULONGLONG ms = GetTickCount64() - t0;
        printf("  результат: %s, источник удалён: %s, приёмник есть: %s\n",
               r.ok ? "OK" : "ОШИБКА", !Exists(PathJoin(tmp, L"from\\big.bin")) ? "да" : "нет",
               Exists(PathJoin(tmp, L"to\\big.bin")) ? "да" : "нет");
        printf("  время: %llu мс (мгновенно — только метаданные)\n", (unsigned long long)ms);
    }

    printf("\n=== 2) Перемещение 5000 мелких файлов в пределах тома ===\n");
    MakeDir(PathJoin(tmp, L"many"));
    for (int i = 0; i < 5000; i++) {
        wchar_t nm[64];
        std::swprintf(nm, 64, L"f%04d.tmp", i);
        MakeFile(PathJoin(PathJoin(tmp, L"many"), nm), 10000);
    }
    MakeDir(PathJoin(tmp, L"many2"));
    {
        ULONGLONG t0 = GetTickCount64();
        CopyOptions o;
        o.move = true;
        CopyOutcome r = RunCopy({ { PathJoin(tmp, L"many"), PathJoin(tmp, L"many2\\many") } },
                                o, nullptr, nullptr, g_cancel, g_pause);
        ULONGLONG ms = GetTickCount64() - t0;
        printf("  результат: %s, время: %llu мс\n", r.ok ? "OK" : "ОШИБКА",
               (unsigned long long)ms);
    }

    printf("\n=== 3) Перемещение 1 ГБ на другой том (если D: есть) ===\n");
    std::wstring droot = L"D:\\amber_mv_test2";
    if (GetDriveTypeW(L"D:\\") == DRIVE_FIXED) {
        MakeFile(PathJoin(tmp, L"to\\big.bin"), 1ull << 30);
        MakeDir(droot);
        ULONGLONG t0 = GetTickCount64();
        CopyOptions o;
        o.move = true;
        CopyOutcome r = RunCopy({ { PathJoin(tmp, L"to\\big.bin"), PathJoin(droot, L"big.bin") } },
                                o, nullptr, nullptr, g_cancel, g_pause);
        ULONGLONG ms = GetTickCount64() - t0;
        double mbps = 1024.0 / (ms / 1000.0);
        printf("  результат: %s, время: %llu мс (%.0f МБ/с) — копирование + удаление источника\n",
               r.ok ? "OK" : "ОШИБКА", (unsigned long long)ms, mbps);
        DeleteDirect({ droot });
    } else {
        printf("  диск D: недоступен — пропуск\n");
    }

    DeleteDirect({ tmp });
    printf("\nГотово.\n");
    return 0;
}
