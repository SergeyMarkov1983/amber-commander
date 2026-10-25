#include "pathutil.h"
#include <cwchar>

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (b.empty()) return a;
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') {
        if (b.front() == L'\\' || b.front() == L'/') return a + b.substr(1);
        return a + b;
    }
    if (b.front() == L'\\' || b.front() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring PathParent(const std::wstring& p) {
    std::wstring s = PathNormalize(p);
    size_t pos = s.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return s;
    if (pos == 2 && s[1] == L':') return s.substr(0, 3);   // "C:\"
    if (pos == 0) return L"\\";
    return s.substr(0, pos);
}

std::wstring PathFileName(const std::wstring& p) {
    std::wstring s = PathNormalize(p);
    size_t pos = s.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return s;
    return s.substr(pos + 1);
}

std::wstring PathExtension(const std::wstring& p) {
    std::wstring name = PathFileName(p);
    size_t pos = name.rfind(L'.');
    if (pos == std::wstring::npos || pos == 0) return L"";
    std::wstring ext = name.substr(pos);
    for (auto& c : ext) c = (wchar_t)LowerChar(c);
    return ext;
}

bool PathIsRoot(const std::wstring& p) {
    return p.size() == 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/');
}

std::wstring PathNormalize(const std::wstring& p) {
    std::wstring s = p;
    while (s.size() > 1 && (s.back() == L'\\' || s.back() == L'/')) {
        if (PathIsRoot(s)) break;
        s.pop_back();
    }
    for (auto& c : s) if (c == L'/') c = L'\\';
    return s;
}

std::wstring RootOf(const std::wstring& p) {
    if (p.size() >= 2 && p[1] == L':') return p.substr(0, 2) + L"\\";
    return L"C:\\";
}

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return PathParent(buf);
}

std::wstring GetIniPath() {
    std::wstring iniNear = PathJoin(GetExeDir(), L"amber.ini");
    // Портативный режим: ini рядом с exe уже существует — он наш.
    if (FileExists(iniNear)) return iniNear;
    // Иначе пробуем создать рядом с exe (проверка возможности записи).
    HANDLE h = CreateFileW(iniNear.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return iniNear;
    }
    // Рядом с exe писать нельзя (Program Files, защищённые носители) —
    // фолбэк в %APPDATA%\Amber Commander\amber.ini (стандарт Windows,
    // не чистится оптимизаторами в отличие от Temp).
    wchar_t appdata[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) && appdata[0]) {
        std::wstring dir = PathJoin(appdata, L"Amber Commander");
        CreateDirectoryW(dir.c_str(), nullptr);
        return PathJoin(dir, L"amber.ini");
    }
    return iniNear;
}

std::wstring FormatSize(uint64_t v) {
    static const wchar_t* units[] = { L"Б", L"КБ", L"МБ", L"ГБ", L"ТБ" };
    double d = (double)v;
    int u = 0;
    while (d >= 1024.0 && u < 4) { d /= 1024.0; u++; }
    wchar_t buf[64];
    if (u == 0) std::swprintf(buf, 64, L"%llu %s", (unsigned long long)v, units[u]);
    else std::swprintf(buf, 64, L"%.1f %s", d, units[u]);
    return buf;
}

std::wstring FormatDate(const FILETIME& ft) {
    FILETIME lft;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%02u.%02u.%04u %02u:%02u",
                  st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    return buf;
}

bool IsSameVolume(const std::wstring& a, const std::wstring& b) {
    DWORD sa = 0, sb = 0;
    std::wstring pa = RootOf(a), pb = RootOf(b);
    GetVolumeInformationW(pa.c_str(), nullptr, 0, &sa, nullptr, nullptr, nullptr, 0);
    GetVolumeInformationW(pb.c_str(), nullptr, 0, &sb, nullptr, nullptr, nullptr, 0);
    return sa != 0 && sa == sb;
}

std::wstring LongPath(const std::wstring& p) {
    std::wstring s = p;
    for (auto& c : s) if (c == L'/') c = L'\\';
    if (s.size() >= 4 && s.compare(0, 4, L"\\\\?\\") == 0) return s;
    if (s.size() >= 2 && s[0] == L'\\' && s[1] == L'\\') return L"\\\\?\\UNC" + s.substr(1);
    return L"\\\\?\\" + s;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(LongPath(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(LongPath(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void Trim(std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t");
    size_t b = s.find_last_not_of(L" \t");
    if (a == std::wstring::npos) { s.clear(); return; }
    s = s.substr(a, b - a + 1);
}
