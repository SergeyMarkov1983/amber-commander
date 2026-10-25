// tests/test_search.cpp — приёмочные тесты поискового движка (агент B).
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//          -D_WIN32_WINNT=0x0A00 -Isrc -Isrc\ui -Isrc\core
//          tests\test_search.cpp src\core\search.cpp src\core\wildcard.cpp src\core\fsenum.cpp src\core\pathutil.cpp
//          -luser32 -lshell32 -ladvapi32 -o tests\test_search.exe
#include "search.h"
#include "wildcard.h"
#include "pathutil.h"
#include "fsenum.h"
#include <windows.h>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>

struct Counters {
    std::atomic<uint64_t> hits{0}, files{0}, dirs{0};
};

static double NowMs() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Поиск по имени. Возвращает {время мс, hits, files, dirs}.
static void RunNameSearch(const std::wstring& root, const std::wstring& mask, bool subdirs,
                          Counters& c, double& msOut) {
    SearchQuery q;
    q.root = root;
    q.masks = mask;
    q.subdirs = subdirs;
    std::atomic<bool> cancel{false};
    double t0 = NowMs();
    RunSearch(q, [&](const std::wstring&, const DirEntry& e) {
        c.hits++;
        if (e.isDir) c.dirs++; else c.files++;
    }, cancel);
    msOut = NowMs() - t0;
}

// Поиск текста в содержимом.
static void RunTextSearch(const std::wstring& root, const std::wstring& text,
                          Counters& c, double& msOut) {
    SearchQuery q;
    q.root = root;
    q.masks = L"*";
    q.text = text;
    std::atomic<bool> cancel{false};
    double t0 = NowMs();
    RunSearch(q, [&](const std::wstring&, const DirEntry& e) {
        c.hits++;
        if (e.isDir) c.dirs++; else c.files++;
    }, cancel);
    msOut = NowMs() - t0;
}

static void PrintLine(const char* name, double ms, uint64_t hits, uint64_t files, uint64_t dirs) {
    printf("    %-28s %8.0f ms   hits=%llu (files=%llu, dirs=%llu)  %8.0f hits/s\n",
           name, ms, (unsigned long long)hits, (unsigned long long)files,
           (unsigned long long)dirs, ms > 0 ? (double)hits * 1000.0 / ms : 0.0);
}

int main() {
    SetConsoleOutputCP(65001);
    unsigned hw = std::thread::hardware_concurrency();
    unsigned nThreads = hw ? std::max(4u, std::min(hw, 16u)) : 8u;
    printf("=== Amber search engine test ===\n");
    printf("hardware_concurrency=%u, pool threads=%u\n", hw, nThreads);
    fflush(stdout);

    // ---- 0. Функциональная проверка поиска по содержимому (известные файлы). ----
    printf("\n[0] Functional content-search check (temp dir)\n");
    {
        wchar_t tmpRoot[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpRoot);
        std::wstring tdir = std::wstring(tmpRoot) + L"amber_search_test";
        CreateDirectoryW(tdir.c_str(), nullptr);
        // Файлы: UTF-8 ASCII, UTF-8 кириллица (нижний регистр), UTF-8 верхний ASCII,
        // UTF-16LE (ASCII), UTF-16LE (кириллица).
        auto WriteFileBytes = [](const std::wstring& p, const void* data, size_t n) {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) return;
            DWORD w = 0;
            WriteFile(h, data, (DWORD)n, &w, nullptr);
            CloseHandle(h);
        };
        const char u8ascii[] = "hello world getprocaddress test";
        const char u8cyr[] = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80"; // "привет мир"
        const char u8upper[] = "GETPROCADDRESS";
        std::wstring u16ascii = L"\xFEFFGetProcAddress text";
        std::wstring u16cyr = L"\xFEFF\x043F\x0440\x0438\x0432\x0435\x0442";
        WriteFileBytes(tdir + L"\\a.txt", u8ascii, sizeof(u8ascii) - 1);
        WriteFileBytes(tdir + L"\\b.txt", u8cyr, sizeof(u8cyr) - 1);
        WriteFileBytes(tdir + L"\\c.txt", u8upper, sizeof(u8upper) - 1);
        WriteFileBytes(tdir + L"\\d.txt", u16ascii.data(), u16ascii.size() * 2);
        WriteFileBytes(tdir + L"\\e.txt", u16cyr.data(), u16cyr.size() * 2);

        struct TC { const wchar_t* text; uint64_t expect; const char* name; };
        TC cases[] = {
            { L"getprocaddress", 3, "UTF-8/UTF-16LE ASCII ci" },      // a.txt + c.txt + d.txt
            { L"GETPROCADDRESS", 3, "ASCII ci both ways" },            // a.txt + c.txt + d.txt
            { L"привет мир", 1, "UTF-8 Cyrillic exact" },              // b.txt
            { L"ПРИВЕТ МИР", 1, "UTF-8 Cyrillic via lowered needle" }, // b.txt
            { L"привет", 2, "Cyrillic utf8+utf16le" },                 // b.txt + e.txt
            { L"zzz_nope_2026", 0, "negative" },
        };
        int bad = 0;
        for (auto& tc : cases) {
            Counters c;
            double ms = 0;
            RunTextSearch(tdir, tc.text, c, ms);
            bool ok = c.hits == tc.expect;
            if (!ok) bad++;
            printf("    %-34s hits=%llu expect=%llu %s\n", tc.name,
                   (unsigned long long)c.hits.load(), (unsigned long long)tc.expect, ok ? "OK" : "FAIL");
        }
        // Зачистка.
        for (auto& f : { L"a.txt", L"b.txt", L"c.txt", L"d.txt", L"e.txt" })
            DeleteFileW((tdir + L"\\" + f).c_str());
        RemoveDirectoryW(tdir.c_str());
        if (bad) printf("    WARNING: %d functional case(s) failed!\n", bad);
        fflush(stdout);
    }

    // ---- 1. Поиск по имени по всему диску C: (маска "*"), холодный и тёплый прогон. ----
    printf("\n[1] Name search C:\\ mask=\"*\" (subdirs)\n");
    {
        Counters cold, warm;
        double msCold = 0, msWarm = 0;
        RunNameSearch(L"C:\\", L"*", true, cold, msCold);
        printf("    cold:  ");
        PrintLine("", msCold, cold.hits, cold.files, cold.dirs);
        RunNameSearch(L"C:\\", L"*", true, warm, msWarm);
        printf("    warm:  ");
        PrintLine("", msWarm, warm.hits, warm.files, warm.dirs);
        if (cold.hits != warm.hits)
            printf("    WARNING: counts differ between runs!\n");
        fflush(stdout);
    }

    // ---- 2. Маска "*.dll" в C:\Windows (с подкаталогами). ----
    printf("\n[2] Name search C:\\Windows mask=\"*.dll\"\n");
    {
        Counters c;
        double ms = 0;
        RunNameSearch(L"C:\\Windows", L"*.dll", true, c, ms);
        PrintLine("with subdirs", ms, c.hits, c.files, c.dirs);
        Counters c2;
        double ms2 = 0;
        RunNameSearch(L"C:\\Windows", L"*.dll", false, c2, ms2);
        PrintLine("subdirs=false (sanity)", ms2, c2.hits, c2.files, c2.dirs);
        fflush(stdout);
    }

    // ---- 3. Поиск текста в C:\Windows\System32. ----
    // Каталоги попадают в результаты по имени (маска "*"), содержимое не сканируется —
    // поэтому у негативного теста hits == числу каталогов, а files == 0.
    printf("\n[3] Text search in C:\\Windows\\System32\n");
    {
        Counters cold, warm, lower, neg;
        double msCold = 0, msWarm = 0, msLower = 0, msNeg = 0;
        RunTextSearch(L"C:\\Windows\\System32", L"GetProcAddress", cold, msCold);
        PrintLine("\"GetProcAddress\" (cold)", msCold, cold.hits, cold.files, cold.dirs);
        RunTextSearch(L"C:\\Windows\\System32", L"GetProcAddress", warm, msWarm);
        PrintLine("\"GetProcAddress\" (warm)", msWarm, warm.hits, warm.files, warm.dirs);
        RunTextSearch(L"C:\\Windows\\System32", L"getprocaddress", lower, msLower);
        PrintLine("\"getprocaddress\" (ci check)", msLower, lower.hits, lower.files, lower.dirs);
        if (lower.files != cold.files)
            printf("    NOTE: ASCII case-insensitive file counts differ (%llu vs %llu)\n",
                   (unsigned long long)lower.files.load(), (unsigned long long)cold.files.load());
        RunTextSearch(L"C:\\Windows\\System32", L"zzzQx9_never_2026", neg, msNeg);
        PrintLine("\"zzzQx9_never_2026\" (neg.)", msNeg, neg.hits, neg.files, neg.dirs);
        if (neg.files != 0) printf("    WARNING: negative test found FILE hits!\n");
        fflush(stdout);
    }

    // ---- 4. Поиск по имени на D: (задача требует; диск почти пуст, поиск завершается сразу). ----
    if (GetLogicalDrives() & (1u << ('D' - 'A'))) {
        printf("\n[4] Name search D:\\ mask=\"*\" (subdirs)\n");
        Counters c;
        double ms = 0;
        RunNameSearch(L"D:\\", L"*", true, c, ms);
        PrintLine("completed", ms, c.hits, c.files, c.dirs);
        fflush(stdout);
    } else {
        printf("\n[4] D: not present — skipped\n");
    }

    // ---- 5. Демонстрация мгновенной отмены: полное сканирование C: по содержимому
    //         (редкая строка — файлы читаются целиком), отмена через 15 с. ----
    {
        printf("\n[5] Cancel latency: C:\\ mask=\"*\" text=\"zzzQx9_never_2026\", cancel after 15 s\n");
        SearchQuery q;
        q.root = L"C:\\";
        q.masks = L"*";
        q.text = L"zzzQx9_never_2026";
        std::atomic<bool> cancel{false};
        std::atomic<bool> finished{false};
        Counters c;
        double t0 = NowMs();
        std::thread th([&] {
            RunSearch(q, [&](const std::wstring&, const DirEntry& e) {
                c.hits++;
                if (e.isDir) c.dirs++; else c.files++;
            }, cancel);
            finished.store(true);
        });
        while (NowMs() - t0 < 15000.0 && !finished.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        double runMs = NowMs() - t0;
        double cancelLatency = 0;
        if (!finished.load()) {
            double t1 = NowMs();
            cancel.store(true);
            th.join();
            cancelLatency = NowMs() - t1;
            printf("    still running at %.0f ms -> cancel; scanned hits so far: %llu (files=%llu, dirs=%llu)\n",
                   runMs, (unsigned long long)c.hits.load(), (unsigned long long)c.files.load(),
                   (unsigned long long)c.dirs.load());
            printf("    cancel->return latency: %.1f ms (target < 50 ms)\n", cancelLatency);
        } else {
            th.join();
            printf("    finished before timer (%.0f ms), hits=%llu\n", runMs,
                   (unsigned long long)c.hits.load());
        }
        fflush(stdout);
    }

    printf("\nDone.\n");
    return 0;
}
