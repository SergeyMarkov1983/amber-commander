// tests/test_name_search.cpp — проверка поиска имён по фрагменту/маске (как в DC)
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         -D_WIN32_WINNT=0x0A00 -Isrc -Isrc\core tests\test_name_search.cpp
//         src\core\search.cpp src\core\fsenum.cpp src\core\wildcard.cpp src\core\pathutil.cpp
//         -luser32 -lshell32 -static -o tests\test_name_search.exe
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include "search.h"
#include "fsenum.h"
#include "pathutil.h"

static std::atomic<bool> g_cancel{ false };
static std::vector<std::wstring> g_hits;

static void OnHit(const std::wstring& p, const DirEntry&) {
    g_hits.push_back(p);
}

static bool MakeDir(const std::wstring& p) {
    return CreateDirectoryW(LongPath(p).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool MakeFile(const std::wstring& p) {
    HANDLE h = CreateFileW(LongPath(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

static bool DeleteTree2(const std::wstring& p) {
    DWORD a = GetFileAttributesW(LongPath(p).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        auto items = EnumerateDir(p, nullptr);
        for (auto& e : items) DeleteTree2(PathJoin(p, e.name));
        return RemoveDirectoryW(LongPath(p).c_str()) != 0;
    }
    return DeleteFileW(LongPath(p).c_str()) != 0;
}

static int Run(const SearchQuery& q, const wchar_t* label) {
    g_hits.clear();
    RunSearch(q, OnHit, g_cancel);
    std::vector<std::wstring> names;
    for (auto& h : g_hits) names.push_back(PathFileName(h));
    std::sort(names.begin(), names.end());
    printf("  %-28ls: %d hit(s)", label, (int)names.size());
    for (auto& n : names) printf("  [%ls]", n.c_str());
    printf("\n");
    return (int)names.size();
}

static std::wstring GetEnvVar(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return n ? std::wstring(buf, n) : std::wstring();
}

int main() {
    std::wstring root = PathJoin(GetEnvVar(L"TEMP"), L"amber_search_test");
    DeleteTree2(root);
    MakeDir(root);
    MakeDir(PathJoin(root, L"sub"));
    MakeFile(PathJoin(root, L"abc_kotor_xyz.txt"));
    MakeFile(PathJoin(root, L"kotor_1.bin"));
    MakeFile(PathJoin(root, L"KOTOR_2.log"));
    MakeFile(PathJoin(root, L"notko.txt"));
    MakeFile(PathJoin(root, L"star_kotor_wars"));
    MakeDir(PathJoin(root, L"sub\\kotor_deep"));
    MakeFile(PathJoin(root, L"sub\\kotor_deep\\readme_kotor.md"));

    printf("Каталог теста: %ls\n\n", root.c_str());

    SearchQuery q;
    q.root = root;
    q.subdirs = true;

    // 1) фрагмент "kotor" без регистра — ожидаем 6 (включая KOTOR_2, каталог kotor_deep, файлы)
    q.masks = L"kotor";
    int r1 = Run(q, L"фрагмент 'kotor' (без рег.)");
    printf("  ожидание: 6\n");

    // 2) с учётом регистра — 5 (KOTOR_2 не подходит)
    q.caseSensitive = true;
    int r2 = Run(q, L"фрагмент 'kotor' (регистр)");
    printf("  ожидание: 5\n");
    q.caseSensitive = false;

    // 3) только файлы
    q.matchDirs = false;
    int r3 = Run(q, L"только файлы");
    printf("  ожидание: 5\n");
    q.matchDirs = true;

    // 4) только каталоги
    q.matchFiles = false;
    int r4 = Run(q, L"только каталоги");
    printf("  ожидание: 1\n");
    q.matchFiles = true;

    // 5) маска *.bin
    q.masks = L"*.bin";
    int r5 = Run(q, L"маска *.bin");
    printf("  ожидание: 1\n");

    // 6) пусто = все
    q.masks = L"";
    int r6 = Run(q, L"пусто (все)");
    printf("  ожидание: 8 (5 файлов + 3 каталога: sub, kotor_deep, корень не считается)\n");

    // 7) несколько фрагментов
    q.masks = L"kotor;notko";
    int r7 = Run(q, L"'kotor;notko'");
    printf("  ожидание: 7\n");

    bool ok = r1 == 6 && r2 == 5 && r3 == 5 && r4 == 1 && r5 == 1 && r6 == 8 && r7 == 7;
    printf("\n%s\n", ok ? "ALL PASSED" : "FAILED");
    DeleteTree2(root);
    return ok ? 0 : 1;
}
