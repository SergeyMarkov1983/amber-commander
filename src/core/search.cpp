#include "search.h"
#include "pathutil.h"
#include "fsenum.h"
#include "wildcard.h"
#include <cwchar>
#include <cstring>
#include <deque>
#include <chrono>
#include <condition_variable>
#include <emmintrin.h>

// ============================================================================
// Параллельный поиск: пул потоков + общая очередь каталогов.
//
// Архитектура:
//  * N = clamp(hardware_concurrency, 4, 16) потоков; вызывающий поток тоже
//    участвует в обходе, поэтому RunSearch возвращается сразу после
//    завершения или отмены.
//  * Общая очередь каталогов: std::deque + std::mutex + std::condition_variable.
//    Work-stealing не нужен — очереди задач достаточно.
//  * Отмена: атомарный флаг проверяется в ожидании очереди (таймаут CV 5 мс),
//    каждые 64 записи каталога и каждые 64 КБ при сканировании содержимого.
//    Возврат из RunSearch после установки флага — миллисекунды (<50 мс).
//    Файлы открываются с FILE_FLAG_OPEN_NO_RECALL, чтобы облачные плейсхолдеры
//    (OneDrive) не гидратировались и не блокировали отмену.
//  * Содержимое файлов: CreateFileMappingW + MapViewOfFile кусками по 8 МБ
//    с перекрытием в (maxNeedleLen - 1) байт — совпадения на границах кусков
//    не теряются. Поиск иголки в куске — SSE2-фильтр по первому байту
//    (16 байт за инструкцию) + точная проверка кандидатов.
//  * Регистронезависимый поиск текста: ASCII-байты сворачиваются (A-Z <-> a-z);
//    байты >= 0x80 сравниваются точно. Дополнительно ищется «нижнерегистровая»
//    копия иголки (таблица для латиницы U+00C0..U+00FF и кириллицы
//    U+0400..U+042F). ОГРАНИЧЕНИЕ (честно): «ПРИВЕТ» в файле при иголке
//    «привет» не найдётся — для не-ASCII только точное байтовое совпадение.
//  * Reparse points (FILE_ATTRIBUTE_REPARSE_POINT) пропускаются полностью:
//    junction/symlink не обходим (защита от циклов и дублей) и не выдаём.
//  * Каталоги, перечисление которых упало (нет доступа и т.п.), дают пустой
//    список — обход просто продолжается (интерфейс EnumerateDir не различает
//    пустой каталог и ошибку).
//  * Горячий цикл не создаёт wstring-мусора: полный путь строится в
//    переиспользуемом буфере каждого потока.
// ============================================================================

namespace {

// Фрагмент без спецсимволов ("kotor") — поиск подстроки в имени (как в DC),
// со спецсимволами (*, ?) — обычная маска.
bool ContainsI(const wchar_t* hay, const wchar_t* needle) {
    size_t n = wcslen(needle);
    if (!n) return true;
    for (const wchar_t* h = hay; *h; h++) {
        if (LowerChar(*h) == LowerChar(*needle)) {
            size_t k = 1;
            while (k < n && h[k] && LowerChar(h[k]) == LowerChar(needle[k])) k++;
            if (k == n) return true;
        }
    }
    return false;
}

bool PatternMatches(const std::wstring& pat, const std::wstring& name, bool cs) {
    if (pat.find_first_of(L"*?") == std::wstring::npos) {
        if (cs) return name.find(pat) != std::wstring::npos;
        return ContainsI(name.c_str(), pat.c_str());
    }
    return cs ? WildMatch(pat.c_str(), name.c_str()) : WildMatchI(pat.c_str(), name.c_str());
}

bool MatchAnyPattern(const std::vector<std::wstring>& pats, const std::wstring& name, bool cs) {
    if (pats.empty()) return true;
    for (auto& p : pats)
        if (PatternMatches(p, name, cs)) return true;
    return false;
}

constexpr size_t kMapChunk = 8u << 20;          // окно отображения файла: 8 МБ
constexpr int kCancelCheckEvery = 64;           // проверка отмены каждые N записей каталога
constexpr size_t kCancelCheckBytes = 64u << 10; // проверка отмены каждые 64 КБ скана

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

inline unsigned FoldAscii(unsigned c) {
    unsigned d = c - (unsigned)'A';
    return c + (d <= 25u ? 32u : 0u);
}

// ---------------------------------------------------------------------------
// Подготовка иголок (один раз на поиск, не на файл).
// Все иголки складываются в один непрерывный пул (offsets стабильны при
// дозаписи), а указатели вычисляются только после сборки — иначе SSO-строки
// (короткие иголки) переезжали бы при реаллокации вектора и указатели
// протухали.
// Для ci-режима ищутся обе копии: исходная (ASCII-фолд в VerifyMatch делает её
// регистронезависимой для ASCII) и нижнерегистровая (для не-ASCII, где
// сравнение точное по байтам).
// ---------------------------------------------------------------------------
struct NeedleSet {
    std::string pool;                                       // все иголки подряд
    std::vector<std::pair<size_t, size_t>> list;            // {offset, len} в pool
    std::vector<std::pair<const unsigned char*, size_t>> ptrs; // после Finalize()
    size_t maxLen = 1;
    bool ci = false;

    void Finalize() {
        ptrs.clear();
        ptrs.reserve(list.size());
        for (auto& [off, len] : list)
            ptrs.push_back({ (const unsigned char*)pool.data() + off, len });
    }
};

void AddNeedle(NeedleSet& ns, const std::string& bytes) {
    if (bytes.empty()) return;
    ns.list.push_back({ ns.pool.size(), bytes.size() });
    ns.pool += bytes;
    if (bytes.size() > ns.maxLen) ns.maxLen = bytes.size();
}

// Нижний регистр UTF-8 для известных диапазонов:
//   0xC3 + [0xC0..0xD6],[0xD8..0xDE] — латиница U+00C0..U+00FF (À..Þ, кроме ×)
//   0xD0 + [0x80..0x8F]              — кириллица U+0400..U+040F (Ѐ..Џ) -> 0xD1 trail+0x10
//   0xD0 + [0x90..0x9F]              — кириллица U+0410..U+041F (А..П) -> trail+0x20
//   0xD0 + [0xA0..0xAF]              — кириллица U+0420..U+042F (Р..Я) -> 0xD1 trail-0x20
// 0xC3/0xD0 не бывают байтами продолжения в валидном UTF-8, так что менять
// следующий байт безопасно; длина строки не меняется.
std::string LowerUtf8(const std::string& in) {
    std::string out = in;
    for (size_t i = 0; i < out.size(); i++) {
        unsigned c = (unsigned char)out[i];
        if (c < 0x80u) { out[i] = (char)FoldAscii(c); continue; }
        if (i + 1 < out.size()) {
            unsigned t = (unsigned char)out[i + 1];
            if (c == 0xC3u) {
                if ((t >= 0xC0u && t <= 0xD6u) || (t >= 0xD8u && t <= 0xDEu))
                    out[i + 1] = (char)(t + 0x20u);
            } else if (c == 0xD0u) {
                if (t >= 0x80u && t <= 0x8Fu) { out[i] = (char)0xD1; out[i + 1] = (char)(t + 0x10u); }
                else if (t >= 0x90u && t <= 0x9Fu) out[i + 1] = (char)(t + 0x20u);
                else if (t >= 0xA0u && t <= 0xAFu) { out[i] = (char)0xD1; out[i + 1] = (char)(t - 0x20u); }
            }
        }
    }
    return out;
}

std::string Utf16LeBytes(const std::wstring& w) {
    return std::string((const char*)w.data(), w.size() * 2);
}

std::string Utf16LeLower(const std::wstring& w) {
    std::string out(w.size() * 2, 0);
    for (size_t i = 0; i < w.size(); i++) {
        wchar_t l = (wchar_t)LowerChar(w[i]);
        out[i * 2] = (char)(l & 0xFF);
        out[i * 2 + 1] = (char)((l >> 8) & 0xFF);
    }
    return out;
}

void BuildNeedles(const std::wstring& text, bool caseSensitive, NeedleSet& ns) {
    ns.ci = !caseSensitive;
    std::string u8 = ToUtf8(text);
    std::string u16 = Utf16LeBytes(text);
    if (caseSensitive) {
        AddNeedle(ns, u8);
        AddNeedle(ns, u16);
        ns.Finalize();
        return;
    }
    std::string u8l = LowerUtf8(u8);
    if (u8l != u8) { AddNeedle(ns, u8); AddNeedle(ns, u8l); }
    else AddNeedle(ns, u8);
    std::string u16l = Utf16LeLower(text);
    if (u16l != u16) { AddNeedle(ns, u16); AddNeedle(ns, u16l); }
    else AddNeedle(ns, u16);
    ns.Finalize();
}

// ---------------------------------------------------------------------------
// Поиск иголки в буфере.
// ---------------------------------------------------------------------------

// Проверка кандидата: точная (memcmp) либо ASCII-регистронезависимая;
// байты >= 0x80 в ci-режиме сравниваются точно.
inline bool VerifyMatch(const unsigned char* hay, const unsigned char* ndl, size_t n, bool ci) {
    if (!ci) return memcmp(hay, ndl, n) == 0;
    for (size_t i = 0; i < n; i++) {
        unsigned a = hay[i], b = ndl[i];
        if (a == b) continue;
        if ((a | b) >= 0x80u) return false;
        if (FoldAscii(a) != FoldAscii(b)) return false;
    }
    return true;
}

// SSE2-фильтр по первому байту + проверка кандидатов. true при первом совпадении.
// cancel проверяется каждые kCancelCheckBytes сканированных байт.
bool FindBytes(const unsigned char* hay, size_t hlen, const unsigned char* ndl, size_t n,
               bool ci, std::atomic<bool>* cancel) {
    if (n == 0 || n > hlen) return false;
    const unsigned char f = ndl[0];
    const __m128i vf = _mm_set1_epi8((char)f);
    const __m128i vf2 = _mm_set1_epi8((char)(f ^ 0x20u));
    const bool ciFirst = ci && f < 0x80u;   // фолд первого байта только для ASCII

    size_t pos = 0;
    const size_t blockEnd = hlen >= 16 ? hlen - 15 : 0;
    size_t nextCheck = kCancelCheckBytes;
    for (; pos < blockEnd; pos += 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)(hay + pos));
        unsigned m = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(v, vf));
        if (ciFirst) m |= (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(v, vf2));
        while (m) {
            unsigned b = (unsigned)__builtin_ctz(m);
            m &= m - 1;
            size_t c = pos + b;
            if (c + n <= hlen && VerifyMatch(hay + c, ndl, n, ci)) return true;
        }
        if (pos >= nextCheck) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return false;
            nextCheck = pos + kCancelCheckBytes;
        }
    }
    for (; pos + n <= hlen; pos++) {
        unsigned hb = hay[pos];
        if (hb == f || (ciFirst && (hb ^ 0x20u) == f)) {
            if (VerifyMatch(hay + pos, ndl, n, ci)) return true;
        }
        if (pos >= nextCheck) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return false;
            nextCheck = pos + kCancelCheckBytes;
        }
    }
    return false;
}

#ifndef FILE_FLAG_OPEN_NO_RECALL
#define FILE_FLAG_OPEN_NO_RECALL 0x00400000   // не гидратировать облачные плейсхолдеры
#endif

// ---------------------------------------------------------------------------
// Поиск текста в файле через memory-mapped куски по 8 МБ.
// ---------------------------------------------------------------------------
bool FileContainsText(const std::wstring& path, const NeedleSet& ns, std::atomic<bool>* cancel) {
    HANDLE h = CreateFileW(LongPath(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_NO_RECALL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart == 0) { CloseHandle(h); return false; }
    HANDLE hm = CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hm) { CloseHandle(h); return false; }

    const uint64_t size = (uint64_t)sz.QuadPart;
    const size_t maxN = ns.maxLen;

    bool found = false;
    // Куски по 8 МБ, выровнены по гранулярности отображения (8 МБ % 64 КБ == 0).
    // View расширяется на (maxN - 1) байт — совпадения на границах кусков видны.
    for (uint64_t off = 0; off < size && !found; off += kMapChunk) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        uint64_t want64 = std::min<uint64_t>(kMapChunk + maxN - 1, size - off);
        const char* p = (const char*)MapViewOfFile(hm, FILE_MAP_READ,
                                                   (DWORD)(off >> 32), (DWORD)(off & 0xFFFFFFFFu),
                                                   (SIZE_T)want64);
        if (!p) break;   // ошибка отображения — файл пропускаем
        for (auto& nd : ns.ptrs) {
            if (FindBytes((const unsigned char*)p, (size_t)want64, nd.first, nd.second, ns.ci, cancel)) {
                found = true;
                break;
            }
        }
        UnmapViewOfFile(p);
    }
    CloseHandle(hm);
    CloseHandle(h);
    return found;
}

// ---------------------------------------------------------------------------
// Пул потоков + очередь каталогов.
// ---------------------------------------------------------------------------
struct SharedQueue {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::wstring> dirs;
    size_t busy = 0;     // потоков сейчас обрабатывают каталог
    bool done = false;   // очередь пуста и никто не работает
};

struct SearchCtx {
    const std::vector<std::wstring>* masks = nullptr;
    const NeedleSet* needles = nullptr;
    HitFn* onHit = nullptr;
    std::atomic<bool>* cancel = nullptr;
    uint64_t sizeMin = 0, sizeMax = UINT64_MAX, maxContentFile = 0;
    bool subdirs = true, caseSensitive = false, hasText = false;
    bool matchFiles = true, matchDirs = true;

    bool MaskMatches(const std::wstring& name) const {
        return MatchAnyPattern(*masks, name, caseSensitive);
    }
};

void PushDir(SharedQueue& sh, const std::wstring& d) {
    {
        std::lock_guard<std::mutex> lk(sh.m);
        sh.dirs.push_back(d);
    }
    sh.cv.notify_one();
}

void ProcessDir(const std::wstring& dir, SearchCtx& ctx, SharedQueue& sh, std::wstring& full) {
    auto items = EnumerateDir(dir, ctx.cancel);
    int since = 0;
    for (const auto& e : items) {
        if ((++since & (kCancelCheckEvery - 1)) == 0 && ctx.cancel->load(std::memory_order_relaxed))
            return;

        // ПРОПУСК reparse point: junction/symlink не обходим (циклы, дубли) и не выдаём.
        if (e.attrs & FILE_ATTRIBUTE_REPARSE_POINT) continue;

        full.clear();
        full += dir;
        full += L'\\';
        full += e.name;

        if (e.isDir) {
            if (ctx.subdirs) PushDir(sh, full);
            if (ctx.matchDirs && ctx.MaskMatches(e.name)) (*ctx.onHit)(full, e);
        } else {
            if (!ctx.matchFiles) continue;
            if (!ctx.MaskMatches(e.name)) continue;
            if (e.size < ctx.sizeMin || e.size > ctx.sizeMax) continue;
            if (ctx.hasText) {
                if (e.size > ctx.maxContentFile) continue;   // слишком крупные не сканируем
                if (ctx.cancel->load(std::memory_order_relaxed)) return;
                if (!FileContainsText(full, *ctx.needles, ctx.cancel)) continue;
            }
            (*ctx.onHit)(full, e);
        }
    }
}

void Worker(SharedQueue& sh, SearchCtx& ctx) {
    std::wstring full;
    full.reserve(300);   // переиспользуемый буфер полного пути (без мусора в цикле)
    for (;;) {
        std::wstring dir;
        {
            std::unique_lock<std::mutex> lk(sh.m);
            // Таймаут CV — быстрая реакция на отмену, даже если очередь пуста.
            sh.cv.wait_for(lk, std::chrono::milliseconds(5), [&] {
                return sh.done || ctx.cancel->load(std::memory_order_relaxed) || !sh.dirs.empty();
            });
            if (sh.done || ctx.cancel->load(std::memory_order_relaxed)) return;
            if (sh.dirs.empty()) continue;   // таймаут без работы — ждём дальше
            dir = std::move(sh.dirs.front());
            sh.dirs.pop_front();
            sh.busy++;
        }
        ProcessDir(dir, ctx, sh, full);
        {
            std::lock_guard<std::mutex> lk(sh.m);
            sh.busy--;
            if (sh.dirs.empty() && sh.busy == 0) {
                sh.done = true;
                sh.cv.notify_all();
            }
        }
    }
}

} // namespace

void RunSearch(const SearchQuery& q, HitFn onHit, std::atomic<bool>& cancel) {
    std::vector<std::wstring> masks;
    SplitMasks(q.masks, masks);

    NeedleSet needles;
    if (!q.text.empty()) BuildNeedles(q.text, q.caseSensitive, needles);

    SearchCtx ctx;
    ctx.masks = &masks;
    ctx.needles = &needles;
    ctx.onHit = &onHit;
    ctx.cancel = &cancel;
    ctx.sizeMin = q.sizeMin;
    ctx.sizeMax = q.sizeMax;
    ctx.maxContentFile = q.maxContentFile;
    ctx.subdirs = q.subdirs;
    ctx.caseSensitive = q.caseSensitive;
    ctx.hasText = !q.text.empty();
    ctx.matchFiles = q.matchFiles;
    ctx.matchDirs = q.matchDirs;

    SharedQueue sh;
    sh.dirs.push_back(PathNormalize(q.root));

    unsigned hw = std::thread::hardware_concurrency();
    unsigned nThreads = std::max(4u, std::min(hw ? hw : 8u, 16u));
    std::vector<std::thread> pool;
    pool.reserve(nThreads - 1);
    for (unsigned i = 1; i < nThreads; i++)
        pool.emplace_back([&] { Worker(sh, ctx); });
    Worker(sh, ctx);   // вызывающий поток тоже обходит: RunSearch возвращается сразу после конца
    for (auto& t : pool) t.join();
}
