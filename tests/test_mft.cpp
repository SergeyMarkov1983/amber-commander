// tests/test_mft.cpp — проверка MFT-сканера размеров
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         -D_WIN32_WINNT=0x0A00 -Isrc -Isrc\core tests\test_mft.cpp
//         src\core\mftscan.cpp src\core\fsenum.cpp src\core\wildcard.cpp src\core\pathutil.cpp
//         src\core\fileops.cpp -luser32 -lshell32 -static -o tests\test_mft.exe
#include <windows.h>
#include <cstdio>
#include <string>
#include <atomic>
#include "mftscan.h"
#include "fileops.h"
#include "pathutil.h"

static std::atomic<bool> g_cancel{ false };

static void Fmt(uint64_t v, char* buf, size_t n) {
    const char* u[] = { "B", "KB", "MB", "GB", "TB" };
    double d = (double)v;
    int k = 0;
    while (d >= 1024.0 && k < 4) { d /= 1024.0; k++; }
    if (k == 0) snprintf(buf, n, "%llu %s", (unsigned long long)v, u[k]);
    else snprintf(buf, n, "%.1f %s", d, u[k]);
}

int main(int argc, char** argv) {
    std::wstring path = argc > 1
        ? std::wstring(argv[1], argv[1] + strlen(argv[1]))
        : L"C:\\Windows";
    printf("Каталог: %ls\n", path.c_str());

    ULONGLONG t0 = GetTickCount64();
    auto items = ScanFolderSizes(path, &g_cancel);
    ULONGLONG dt = GetTickCount64() - t0;
    printf("MFT-скан: %d элементов за %llu мс\n", (int)items.size(), (unsigned long long)dt);

    uint64_t total = 0;
    char buf[64];
    for (size_t i = 0; i < items.size() && i < 15; i++) {
        Fmt(items[i].size, buf, 64);
        printf("  %3zu  %-40ls  %s%s\n", i + 1, items[i].name.c_str(), buf,
               items[i].isDir ? "  [каталог]" : "");
        total += items[i].size;
    }
    if (items.size() > 15) printf("  ...\n");

    // контрольная сумма обычным перечислением
    ULONGLONG t1 = GetTickCount64();
    uint64_t ref = CalcTreeSize(path, &g_cancel);
    ULONGLONG dt2 = GetTickCount64() - t1;
    Fmt(ref, buf, 64);
    printf("Сумма по MFT: %s\n", (Fmt(total, buf, 64), buf));
    Fmt(ref, buf, 64);
    printf("Сумма CalcTreeSize: %s (%llu мс)\n", buf, (unsigned long long)dt2);

    // расхождение бывает из-за размещённого размера (MFT) vs логического —
    // допускаем до 2% и до 50 МБ
    double diff = (double)(total > ref ? total - ref : ref - total);
    double pct = ref ? diff / (double)ref * 100.0 : 0;
    printf("Расхождение: %.2f %% (%s)\n", pct, (pct < 2.0 || diff < 50ull * 1048576)
           ? "OK (размещённый vs логический размер)" : "ПРОВЕРИТЬ");
    return 0;
}
