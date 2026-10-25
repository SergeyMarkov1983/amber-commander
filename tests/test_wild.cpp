// tests/test_wild.cpp — корректность и бенчмарк матчинга масок (агент B).
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//          -D_WIN32_WINNT=0x0A00 -Isrc -Isrc\ui -Isrc\core tests\test_wild.cpp src\core\wildcard.cpp -o tests\test_wild.exe
#include "wildcard.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <cwchar>

// Эталонная реализация (старый итеративный алгоритм) — для перекрёстной проверки
// семантики оптимизированной версии.
static bool RefMatchImpl(const wchar_t* pat, const wchar_t* str, bool ci) {
    const wchar_t* star = nullptr;
    const wchar_t* ss = nullptr;
    while (*str) {
        wchar_t p = ci ? (wchar_t)towlower(*pat) : *pat;
        wchar_t s = ci ? (wchar_t)towlower(*str) : *str;
        if (p == L'?' || p == s) { pat++; str++; continue; }
        if (p == L'*') { star = pat++; ss = str; continue; }
        if (star) { pat = star + 1; str = ++ss; continue; }
        return false;
    }
    while (*pat == L'*') pat++;
    return *pat == 0;
}

static int g_fail = 0;

static void Check(const wchar_t* pat, const wchar_t* str, bool ci, bool expect, const char* label) {
    bool got = ci ? WildMatchI(pat, str) : WildMatch(pat, str);
    if (got != expect) {
        g_fail++;
        printf("FAIL [%s] pat=\"%ls\" str=\"%ls\" ci=%d: got %d, expect %d\n",
               label, pat, str, (int)ci, (int)got, (int)expect);
    }
}

// Детерминированный LCG для имён в бенчмарке.
struct Rng { uint64_t s = 0x9E3779B97F4A7C15ull; uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } };

int main() {
    // ---- 1. Ручные кейсы (включая краевые). ----
    Check(L"*", L"", false, true, "empty");
    Check(L"*", L"abc", false, true, "star-all");
    Check(L"", L"", false, true, "both-empty");
    Check(L"", L"x", false, false, "empty-pat");
    Check(L"?", L"", false, false, "q-empty");
    Check(L"?", L"x", false, true, "q-one");
    Check(L"??", L"x", false, false, "q-len");
    Check(L"abc", L"abc", false, true, "exact");
    Check(L"abc", L"ab", false, false, "exact-short");
    Check(L"abc", L"abcd", false, false, "exact-long");
    Check(L"a*c", L"abc", false, true, "star-mid");
    Check(L"a*c", L"ac", false, true, "star-empty");
    Check(L"a*c", L"ab", false, false, "star-mid-fail");
    Check(L"*c", L"abc", false, true, "star-suffix");
    Check(L"*c", L"ab", false, false, "star-suffix-fail");
    Check(L"a*", L"abc", false, true, "star-prefix");
    Check(L"a*", L"x", false, false, "star-prefix-fail");
    Check(L"a*b*c", L"abc", false, true, "multi-star");
    Check(L"a*b*c", L"axxbyyc", false, true, "multi-star-2");
    Check(L"a**b", L"ab", false, true, "double-star");
    Check(L"****", L"anything", false, true, "only-stars");
    Check(L"a?c", L"abc", false, true, "q-mid");
    Check(L"a?c", L"ac", false, false, "q-mid-short");
    Check(L"a?c", L"abbc", false, false, "q-mid-long");
    Check(L"a?c", L"aXc", false, true, "q-any");
    Check(L"*.cpp", L"main.cpp", false, true, "ext");
    Check(L"*.cpp", L"main.h", false, false, "ext-fail");
    Check(L"*.*", L"file", false, false, "dot-needed");
    Check(L"*.*", L"file.txt", false, true, "dot-ok");
    Check(L"*?", L"", false, false, "star-q-empty");
    Check(L"*?", L"x", false, true, "star-q-one");
    Check(L"*?*", L"", false, false, "sqs-empty");
    Check(L"*?*", L"x", false, true, "sqs-one");
    Check(L"*ab*cd*", L"xxabxxcdxx", false, true, "backtrack");
    Check(L"ab*cd*ef", L"abcdef", false, true, "tight-stars");
    Check(L"ab*cd*ef", L"abxef", false, false, "tight-fail");
    Check(L"*a*a*b", L"aaab", false, true, "backtrack-2");
    Check(L"*a*a*b", L"aabb", false, true, "backtrack-3");
    Check(L"*a*a*b", L"aaba", false, false, "backtrack-4");
    Check(L"[a]b", L"[a]b", false, true, "bracket-literal");
    Check(L"[a]b", L"ab", false, false, "bracket-not-class");
    // Регистр.
    Check(L"ABC", L"abc", false, false, "cs-case");
    Check(L"ABC", L"abc", true, true, "ci-case");
    Check(L"a?c", L"AbC", true, true, "ci-q");
    Check(L"*.DLL", L"kernel32.dll", true, true, "ci-ext");
    // Юникод.
    Check(L"*.txt", L"отчёт.txt", false, true, "uni-ext");
    Check(L"отчёт?.doc", L"отчёт1.doc", false, true, "uni-q");
    Check(L"отчёт?.doc", L"отчёт.doc", false, false, "uni-q-len");
    Check(L"ОТЧЁТ*", L"отчёт-2026", false, false, "uni-cs");
    Check(L"ОТЧЁТ*", L"ОТЧЁТ-2026", true, true, "uni-ci-identical");
    // В C-локали towlower не сворачивает кириллицу (как в эталонной реализации) —
    // регистронезависимость для не-ASCII появляется только при setlocale(..., ".UTF8").
    Check(L"ОТЧЁТ*", L"отчёт-2026", true, false, "uni-ci-c-locale");
    Check(L"при*", L"Пример", true, false, "uni-ci-c-locale-2");
    Check(L"при*", L"Пример", false, false, "uni-cs-prefix");
    Check(L"日本*", L"日本語", false, true, "cjk");
    Check(L"*文件*", L"我的文件.txt", false, true, "cjk-mid");

    // ---- 2. Фазз: перекрёстная проверка с эталонной реализацией. ----
    Rng rng;
    const wchar_t* patAlpha = L"abcxАБ*?";
    const wchar_t* strAlpha = L"abcxАб_";
    for (int iter = 0; iter < 40000; iter++) {
        size_t pl = rng.Next() % 10;
        size_t sl = rng.Next() % 12;
        std::wstring pat, str;
        pat.reserve(pl); str.reserve(sl);
        for (size_t i = 0; i < pl; i++) pat += patAlpha[rng.Next() % wcslen(patAlpha)];
        for (size_t i = 0; i < sl; i++) str += strAlpha[rng.Next() % wcslen(strAlpha)];
        for (int ci = 0; ci <= 1; ci++) {
            bool ref = RefMatchImpl(pat.c_str(), str.c_str(), ci != 0);
            bool got = ci ? WildMatchI(pat.c_str(), str.c_str()) : WildMatch(pat.c_str(), str.c_str());
            if (ref != got) {
                g_fail++;
                printf("FAIL [fuzz] pat=\"%ls\" str=\"%ls\" ci=%d: got %d, ref %d\n",
                       pat.c_str(), str.c_str(), ci, (int)got, (int)ref);
                if (g_fail > 10) { printf("too many failures, abort\n"); return 1; }
            }
        }
    }

    // ---- 3. SplitMasks. ----
    {
        std::vector<std::wstring> m;
        SplitMasks(L"*.cpp; *.h, *.hpp", m);
        bool ok = m.size() == 3 && m[0] == L"*.cpp" && m[1] == L"*.h" && m[2] == L"*.hpp";
        if (!ok) {
            g_fail++;
            printf("FAIL [split]: got %zu masks:", m.size());
            for (auto& x : m) printf(" '%ls'", x.c_str());
            printf("\n");
        }
        SplitMasks(L"  ; ,, ", m);
        if (!m.empty()) { g_fail++; printf("FAIL [split-empty]\n"); }
        SplitMasks(L"", m);
        if (!m.empty()) { g_fail++; printf("FAIL [split-blank]\n"); }
    }

    // ---- 4. Бенчмарк: 1 млн вызовов MatchAnyMask (5 масок, случайные имена). ----
    {
        const std::vector<std::wstring> masks = { L"*.cpp", L"*.h", L"readme*", L"*config*", L"?????.txt" };
        const wchar_t* parts[] = {
            L"src", L"lib", L"test", L"readme", L"config", L"main", L"index", L"make",
            L"файл", L"отчёт", L"2024", L"2025", L"2026", L"data", L"user", L"tmp",
            L"kernel32", L"ntdll", L"amber", L"build",
        };
        const wchar_t* exts[] = { L".cpp", L".h", L".txt", L".dll", L".exe", L".ini", L"" };
        std::vector<std::wstring> names;
        names.reserve(2048);
        Rng r2;
        for (int i = 0; i < 2048; i++) {
            std::wstring n;
            n += parts[r2.Next() % 20];
            n += (r2.Next() % 4 == 0) ? L"-2026" : L"";
            n += exts[r2.Next() % 7];
            names.push_back(n);
        }
        volatile bool sink = false;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 1'000'000; i++) {
            if (MatchAnyMask(masks, names[i & 2047])) sink = true;
        }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf("BENCH MatchAnyMask: 1,000,000 calls in %.1f ms (%.2f M calls/s) [sink=%d]\n",
               ms, 1000.0 / ms, (int)sink);
    }

    // ---- 5. Бенчмарк: частые формы масок напрямую. ----
    {
        const wchar_t* pats[] = { L"*.cpp", L"src*", L"*config*", L"main.*", L"??????????.dll" };
        const wchar_t* strs[] = { L"app.cpp", L"src\\main.cpp", L"my_config.ini", L"main.cpp", L"kernel32.dll" };
        auto t0 = std::chrono::steady_clock::now();
        volatile bool sink = false;
        for (int i = 0; i < 1'000'000; i++)
            if (WildMatchI(pats[i % 5], strs[i % 5])) sink = true;
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf("BENCH WildMatchI: 1,000,000 calls in %.1f ms (%.2f M calls/s) [sink=%d]\n",
               ms, 1000.0 / ms, (int)sink);
    }

    if (g_fail == 0) printf("ALL TESTS PASSED\n");
    else printf("%d TEST(S) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
