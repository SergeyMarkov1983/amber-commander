#include "wildcard.h"
#include <cwchar>
#include <cstring>

// Оптимизированный матчинг масок. Семантика прежняя:
//   '*' — произвольная последовательность символов (в т.ч. пустая),
//   '?' — ровно один символ.
//
// Быстрые пути:
//   1. Если в маске нет '*' — сравнение фиксированной длины: memcmp (без '?')
//      или короткий цикл с пропуском '?' (с '?').
//   2. Если '*' есть — литеральный префикс до первой '*' и литеральный суффикс
//      после последней '*' проверяются заранее, а общий алгоритм с возвратом
//      работает только на среднем сегменте (как правило, коротком).
// Регистронезависимость: ASCII-символы сворачиваются байтовой арифметикой
// (без вызова towlower), не-ASCII — через towlower, как раньше.

namespace {

// ASCII fold: 'A'..'Z' -> 'a'..'z', остальные байты не меняются (branchless).
inline char FoldAscii(char c) {
    unsigned d = (unsigned)c - (unsigned)'A';
    return (char)(c + (d <= 25u ? 32u : 0u));
}

// Регистронезависимое равенство двух символов:
// быстрый байтовый путь для ASCII, towlower для остальных (как в эталонной версии).
inline bool EqCi(wchar_t a, wchar_t b) {
    if ((unsigned)a < 128u && (unsigned)b < 128u)
        return FoldAscii((char)a) == FoldAscii((char)b);
    return LowerChar(a) == LowerChar(b);
}

// Сравнение равных по длине строк без '*' и без '?'.
inline bool MatchFixedExact(const wchar_t* pat, const wchar_t* str, size_t n, bool ci) {
    if (!ci) return memcmp(pat, str, n * sizeof(wchar_t)) == 0;
    for (size_t i = 0; i < n; i++)
        if (!EqCi(pat[i], str[i])) return false;
    return true;
}

// Классический итеративный алгоритм с возвратом (без рекурсии и аллокаций).
// p..pEnd — сегмент паттерна (всегда оканчивается '*'), s..sEnd — сегмент строки.
static bool MatchGeneral(const wchar_t* p, const wchar_t* pEnd,
                         const wchar_t* s, const wchar_t* sEnd, bool ci) {
    const wchar_t* star = nullptr;
    const wchar_t* ss = nullptr;
    while (s < sEnd) {
        if (p >= pEnd) { s++; continue; }   // хвостовая '*' сегмента поглощает остаток строки
        wchar_t pc = *p;
        wchar_t sc = *s;
        if (pc == L'?' || (ci ? EqCi(pc, sc) : pc == sc)) { p++; s++; continue; }
        if (pc == L'*') { star = p++; ss = s; continue; }
        if (star) { p = star + 1; s = ++ss; continue; }
        return false;
    }
    while (p < pEnd && *p == L'*') p++;
    return p == pEnd;
}

static bool MatchImpl(const wchar_t* pat, const wchar_t* str, bool ci) {
    // Первая '*'.
    const wchar_t* p = pat;
    while (*p && *p != L'*') p++;
    if (*p == 0) {
        // ---- Быстрый путь 1: нет '*' — фиксированная длина. ----
        size_t plen = (size_t)(p - pat);
        if (wcslen(str) != plen) return false;
        const wchar_t* q = pat;
        while (q != p && *q != L'?') q++;
        if (q == p) return MatchFixedExact(pat, str, plen, ci);
        for (size_t i = 0; i < plen; i++) {   // есть '?', но возврат не нужен
            if (pat[i] == L'?') continue;
            if (ci ? !EqCi(pat[i], str[i]) : pat[i] != str[i]) return false;
        }
        return true;
    }

    // ---- Быстрый путь 2: префикс до первой '*' и суффикс после последней '*'. ----
    const wchar_t* patEnd = pat + wcslen(pat);
    const wchar_t* lastStar = patEnd;
    while (lastStar > p && lastStar[-1] != L'*') lastStar--;
    lastStar--;                                        // сама последняя '*'
    size_t prefixLen = (size_t)(p - pat);
    size_t suffixLen = (size_t)(patEnd - lastStar - 1);
    size_t slen = wcslen(str);
    if (slen < prefixLen + suffixLen) return false;

    for (size_t i = 0; i < prefixLen; i++) {
        if (pat[i] == L'?') continue;
        if (ci ? !EqCi(pat[i], str[i]) : pat[i] != str[i]) return false;
    }
    if (suffixLen) {
        const wchar_t* ps = lastStar + 1;
        const wchar_t* ss = str + (slen - suffixLen);
        for (size_t i = 0; i < suffixLen; i++) {
            if (ps[i] == L'?') continue;
            if (ci ? !EqCi(ps[i], ss[i]) : ps[i] != ss[i]) return false;
        }
    }

    // ---- Общий алгоритм на среднем сегменте (от первой до последней '*'). ----
    return MatchGeneral(p, lastStar + 1, str + prefixLen, str + (slen - suffixLen), ci);
}

} // namespace

bool WildMatch(const wchar_t* pat, const wchar_t* str) { return MatchImpl(pat, str, false); }
bool WildMatchI(const wchar_t* pat, const wchar_t* str) { return MatchImpl(pat, str, true); }
bool WildMatchI(const std::wstring& pat, const std::wstring& str) {
    return MatchImpl(pat.c_str(), str.c_str(), true);
}

void SplitMasks(const std::wstring& s, std::vector<std::wstring>& out) {
    out.clear();
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == L';' || s[i] == L',') {
            std::wstring m = s.substr(start, i - start);
            size_t a = m.find_first_not_of(L" \t");
            size_t b = m.find_last_not_of(L" \t");
            if (a != std::wstring::npos) out.push_back(m.substr(a, b - a + 1));
            start = i + 1;
        }
    }
}

bool MatchAnyMask(const std::vector<std::wstring>& masks, const std::wstring& name) {
    if (masks.empty()) return true;
    for (auto& m : masks)
        if (WildMatchI(m.c_str(), name.c_str())) return true;
    return false;
}
