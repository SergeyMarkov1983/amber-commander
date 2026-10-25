#include "folderidx.h"
#include "mftscan.h"
#include "volumes.h"
#include "pathutil.h"

// Формат файла индекса (folders.idx):
//   [0..7]   magic "AMBFOLD1"
//   [8..15]  count (uint64)
//   далее    count × uint64 — смещения записей
//   записи:  uint16 len | имя в UTF-8 (нижний регистр) | uint16 len | путь в UTF-8
// Поиск: бинарный по смещениям, сравнение имени с префиксом.

namespace {

const char* kMagic = "AMBFOLD2";   // v2: корневой слэш в сортировке и обходе исправлены

std::wstring IndexPath() {
    std::wstring ini = GetIniPath();
    return PathJoin(PathParent(ini), L"folders.idx");
}

std::string ToUtf8Lower(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (wchar_t c : s) {
        wchar_t lc = LowerChar(c);
        if (lc < 0x80) {
            out.push_back((char)lc);
        } else if (lc < 0x800) {
            out.push_back((char)(0xC0 | (lc >> 6)));
            out.push_back((char)(0x80 | (lc & 0x3F)));
        } else {
            out.push_back((char)(0xE0 | (lc >> 12)));
            out.push_back((char)(0x80 | ((lc >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (lc & 0x3F)));
        }
    }
    return out;
}

std::wstring FromUtf8(const char* p, size_t n) {
    std::wstring out;
    out.reserve(n);
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)p[i];
        wchar_t wc;
        if (c < 0x80) {
            wc = (wchar_t)c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            wc = (wchar_t)(((c & 0x1F) << 6) | (p[i + 1] & 0x3F));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < n) {
            wc = (wchar_t)(((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F));
            i += 3;
        } else {
            wc = L'?';
            i += 1;
        }
        out.push_back(wc);
    }
    return out;
}

// Рекурсивный обход каталогов (запасной путь без MFT).
// Ограничен по времени и числу каталогов, чтобы не грузить систему.
void WalkDirs(const std::wstring& dir, std::vector<std::wstring>& out,
              std::atomic<bool>* cancel, std::atomic<size_t>& collected,
              size_t maxDirs, ULONGLONG deadline, int depth) {
    if (depth > 40) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (cancel && cancel->load()) break;
        if (collected.load() >= maxDirs || GetTickCount64() > deadline) break;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        std::wstring full = dir + fd.cFileName + L"\\";
        out.push_back(full);
        collected.fetch_add(1);
        WalkDirs(full, out, cancel, collected, maxDirs, deadline, depth + 1);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

struct Mapping {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE map = nullptr;
    const unsigned char* base = nullptr;
    uint64_t size = 0;
    ~Mapping() {
        if (base) UnmapViewOfFile((void*)base);
        if (map) CloseHandle(map);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
};

std::mutex g_m;
std::atomic<bool> g_ready{ false };
std::atomic<bool> g_busy{ false };
std::atomic<bool> g_cancelBuild{ false };
uint64_t g_count = 0;
uint64_t g_size = 0;                      // размер отображения (для защиты границ)
const unsigned char* g_base = nullptr;   // общая часть отображения
const uint64_t* g_offsets = nullptr;

// Открыть индекс и наложить на память. Возвращает true при успехе.
bool MapIndex() {
    // FILE_SHARE_DELETE: позволит MoveFileExW заменить файл при пересборке
    HANDLE f = CreateFileW(IndexPath().c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 24) {
        CloseHandle(f);
        return false;
    }
    HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) {
        CloseHandle(f);
        return false;
    }
    const unsigned char* b = (const unsigned char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!b) {
        CloseHandle(m);
        CloseHandle(f);
        return false;
    }
    if (memcmp(b, kMagic, 8) != 0) {
        UnmapViewOfFile((void*)b);
        CloseHandle(m);
        CloseHandle(f);
        return false;
    }
    memcpy(&g_count, b + 8, 8);
    if (16 + g_count * 8 > (uint64_t)sz.QuadPart) {   // таблица смещений не влезает
        UnmapViewOfFile((void*)b);
        CloseHandle(m);
        CloseHandle(f);
        return false;
    }
    // храним дескрипторы глобально (живут до конца процесса)
    static Mapping s;
    if (s.base) {
        UnmapViewOfFile((void*)s.base);
        CloseHandle(s.map);
        CloseHandle(s.file);
    }
    s.file = f;
    s.map = m;
    s.base = b;
    s.size = (uint64_t)sz.QuadPart;
    memcpy(&g_count, b + 8, 8);
    g_size = s.size;
    g_base = b;
    g_offsets = (const uint64_t*)(b + 16);
    return true;
}

// Читает запись по индексу: [nameUtf8, nameLen] [pathW, pathLen].
// Защита от повреждённого файла: при выходе за границы вернёт false.
bool ReadEntry(uint64_t idx, const unsigned char*& namePtr, uint16_t& nameLen,
               std::wstring& path) {
    if (idx >= g_count) return false;
    uint64_t off = g_offsets[idx];
    if (off + 4 > g_size) return false;
    const unsigned char* p = g_base + off;
    memcpy(&nameLen, p, 2);
    if (off + 4 + (uint64_t)nameLen + 2 > g_size) return false;   // до чтения pathLen
    uint16_t pathLen = 0;
    memcpy(&pathLen, p + 2 + nameLen, 2);
    if (off + 4 + (uint64_t)nameLen + pathLen > g_size) return false;
    namePtr = p + 2;
    path = FromUtf8((const char*)(p + 4 + nameLen), pathLen);
    return true;
}

// Полная валидация индекса: каждая запись в границах И смещения строго
// последовательны (entry i+1 начинается ровно там, где кончилась entry i).
// Повреждённый или старого формата файл → пересборка.
bool ValidateIndex() {
    uint64_t expect = 16 + g_count * 8;
    for (uint64_t i = 0; i < g_count; i++) {
        const unsigned char* np = nullptr;
        uint16_t nl = 0;
        std::wstring path;
        if (!ReadEntry(i, np, nl, path)) return false;
        if (g_offsets[i] != expect) return false;   // не тот формат
        uint16_t pl = 0;
        memcpy(&pl, np + nl, 2);
        expect += 4 + (uint64_t)nl + pl;
    }
    return expect == g_size;
}

// Сборка индекса: все фиксированные NTFS-диски (MFT) + запасной обход.
void BuildThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    g_busy = true;
    std::vector<std::wstring> all;
    {
        DWORD drv = GetLogicalDrives();
        for (int i = 0; drv && i < 26; i++) {
            if (!(drv & (1u << i))) continue;
            std::wstring root = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
            if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
            if (g_cancelBuild.load()) break;
            std::wstring volume = L"\\\\.\\" + root.substr(0, 2);
            auto dirs = ScanVolumeFolders(volume, &g_cancelBuild);
            if (dirs.empty()) {
                // Запасной путь: рекурсивный обход (медленнее, без прав на MFT),
                // с ограничениями — системный диск может покрыться не полностью.
                std::atomic<size_t> walkCount{ 0 };
                ULONGLONG deadline = GetTickCount64() + 90000;
                WalkDirs(root, dirs, &g_cancelBuild, walkCount, 150000, deadline, 0);
                // WalkDirs вернул полные пути (с корнем) — срезаем корень,
                // ниже он добавится один раз
                for (auto& d : dirs)
                    if (d.size() >= root.size() &&
                        WcsICmp(d.substr(0, root.size()).c_str(), root.c_str()) == 0)
                        d = d.substr(root.size());
            }
            for (auto& d : dirs) all.push_back(root + d);
        }
    }
    if (g_cancelBuild.load()) {
        g_busy = false;
        return;
    }
    // сортировка по имени (регистронезависимо, по последнему компоненту;
    // PathFileName нормализует завершающие слэши)
    std::sort(all.begin(), all.end(), [](const std::wstring& a, const std::wstring& b) {
        int c = WcsICmp(PathFileName(a).c_str(), PathFileName(b).c_str());
        if (c) return c < 0;
        return WcsICmp(a.c_str(), b.c_str()) < 0;
    });
    // запись файла
    {
        std::wstring tmp = IndexPath() + L".tmp";
        HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            uint64_t count = all.size();
            // таблица смещений — сначала вся, затем все записи
            std::vector<char> offTab;
            offTab.reserve(16 + count * 8);
            std::vector<char> entries;
            entries.reserve(1 << 20);
            uint64_t offBase = 16 + count * 8;
            for (auto& p : all) {
                std::string name = ToUtf8Lower(PathFileName(p));
                int wlen = WideCharToMultiByte(CP_UTF8, 0, p.c_str(), (int)p.size(),
                                               nullptr, 0, nullptr, nullptr);
                std::string pathUtf8((size_t)std::max(0, wlen), '\0');
                if (wlen > 0)
                    WideCharToMultiByte(CP_UTF8, 0, p.c_str(), (int)p.size(), &pathUtf8[0],
                                        wlen, nullptr, nullptr);
                for (int i = 0; i < 8; i++) offTab.push_back((char)(offBase >> (i * 8)));
                offBase += 2 + name.size() + 2 + pathUtf8.size();
                uint16_t nl = (uint16_t)std::min<size_t>(name.size(), 0xFFF0);
                entries.push_back((char)(nl & 0xFF));
                entries.push_back((char)(nl >> 8));
                entries.insert(entries.end(), name.begin(), name.end());
                uint16_t pl = (uint16_t)std::min<size_t>(pathUtf8.size(), 0xFFF0);
                entries.push_back((char)(pl & 0xFF));
                entries.push_back((char)(pl >> 8));
                entries.insert(entries.end(), pathUtf8.begin(), pathUtf8.end());
            }
            DWORD wr = 0;
            std::vector<char> head;
            head.insert(head.end(), kMagic, kMagic + 8);
            for (int i = 0; i < 8; i++) head.push_back((char)(count >> (i * 8)));
            WriteFile(f, head.data(), (DWORD)head.size(), &wr, nullptr);
            WriteFile(f, offTab.data(), (DWORD)offTab.size(), &wr, nullptr);
            WriteFile(f, entries.data(), (DWORD)entries.size(), &wr, nullptr);
            CloseHandle(f);
            MoveFileExW(tmp.c_str(), IndexPath().c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }
    if (!g_cancelBuild.load()) {
        std::lock_guard<std::mutex> g(g_m);
        if (MapIndex() && ValidateIndex()) g_ready = true;
    }
    g_busy = false;
}

} // namespace

void StartFolderIndexBuild() {
    static std::atomic<bool> started{ false };
    if (started.exchange(true)) return;
    // индекс уже свежий — просто накладываем на память
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        std::wstring ip = IndexPath();
        if (GetFileAttributesExW(ip.c_str(), GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER last{ fad.ftLastWriteTime.dwLowDateTime,
                                 fad.ftLastWriteTime.dwHighDateTime };
            ULARGE_INTEGER now{};
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            now.LowPart = ft.dwLowDateTime;
            now.HighPart = ft.dwHighDateTime;
            uint64_t age = now.QuadPart > last.QuadPart ? now.QuadPart - last.QuadPart : 0;
            const uint64_t kWeek = 7ull * 24 * 3600 * 10000000;
            {
                std::lock_guard<std::mutex> g(g_m);
                if (MapIndex() && age < kWeek && ValidateIndex()) {
                    g_ready = true;
                    return;
                }
            }
        }
    }
    g_cancelBuild = false;
    std::thread(BuildThread).detach();
}

bool FolderIndexReady() {
    return g_ready.load();
}

bool FolderIndexBusy() {
    return g_busy.load();
}

size_t FolderIndexQuery(const std::wstring& prefix, std::vector<std::wstring>& out,
                        size_t limit) {
    out.clear();
    std::lock_guard<std::mutex> g(g_m);
    if (!g_ready || prefix.empty()) return 0;
    std::string pre = ToUtf8Lower(prefix);
    // бинарный поиск первой записи с именем >= префикса
    uint64_t lo = 0, hi = g_count;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        const unsigned char* np = nullptr;
        uint16_t nl = 0;
        std::wstring path;
        if (!ReadEntry(mid, np, nl, path)) {
            out.clear();
            return 0;   // повреждённый индекс — не роняем приложение
        }
        int c = memcmp(np, pre.data(), std::min<size_t>(nl, pre.size()));
        if (c < 0 || (c == 0 && nl < pre.size())) lo = mid + 1;
        else hi = mid;
    }
    size_t total = 0;
    for (uint64_t i = lo; i < g_count; i++) {
        const unsigned char* np = nullptr;
        uint16_t nl = 0;
        std::wstring path;
        if (!ReadEntry(i, np, nl, path)) break;
        if (nl < pre.size() || memcmp(np, pre.data(), pre.size()) != 0) break;
        total++;
        if (out.size() < limit) out.push_back(std::move(path));
    }
    return total;
}
