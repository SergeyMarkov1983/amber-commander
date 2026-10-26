#include "config.h"
#include "dialogs.h"
#include "pathutil.h"
#include <algorithm>
#include <cwchar>
#include <winreg.h>

// Поиск Notepad++: App Paths реестра и стандартные пути установки.
std::wstring FindNotepadPlusPlus() {
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(root,
                          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\notepad++.exe",
                          0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t buf[1024];
            DWORD sz = sizeof(buf);
            if (RegQueryValueExW(k, nullptr, nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
                RegCloseKey(k);
                std::wstring p(buf, sz / sizeof(wchar_t) - 1);
                if (FileExists(p)) return p;
            } else {
                RegCloseKey(k);
            }
        }
    }
    wchar_t pf[MAX_PATH], pfx[MAX_PATH];
    GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles(x86)", pfx, MAX_PATH);
    std::wstring a = std::wstring(pf) + L"\\Notepad++\\notepad++.exe";
    if (FileExists(a)) return a;
    std::wstring b = std::wstring(pfx) + L"\\Notepad++\\notepad++.exe";
    if (FileExists(b)) return b;
    return L"";
}

bool LoadConfig(Config& c, const std::wstring& iniPath) {
    wchar_t buf[1024];
    c.theme = GetPrivateProfileIntW(L"main", L"theme", 0, iniPath.c_str());
    GetPrivateProfileStringW(L"main", L"editor", L"notepad.exe", buf, 1024, iniPath.c_str());
    c.editor = buf;
    // Если редактор не настроен — Notepad++ (F4), как просил пользователь
    if (c.editor == L"notepad.exe" || c.editor.empty()) {
        std::wstring np = FindNotepadPlusPlus();
        if (!np.empty()) c.editor = np;
    }
    GetPrivateProfileStringW(L"main", L"leftDir", L"", buf, 1024, iniPath.c_str());
    c.leftDir = buf;
    GetPrivateProfileStringW(L"main", L"rightDir", L"", buf, 1024, iniPath.c_str());
    c.rightDir = buf;
    c.confirmDelete = GetPrivateProfileIntW(L"main", L"confirmDelete", 1, iniPath.c_str()) != 0;
    c.confirmOverwrite = GetPrivateProfileIntW(L"main", L"confirmOverwrite", 1, iniPath.c_str()) != 0;
    c.showHidden = GetPrivateProfileIntW(L"main", L"showHidden", 1, iniPath.c_str()) != 0;
    c.folderIndexRefresh = GetPrivateProfileIntW(L"main", L"folderIndexRefresh", 1, iniPath.c_str()) != 0;
    GetPrivateProfileStringW(L"main", L"fontFace", L"Segoe UI Variable Text", buf, 1024,
                             iniPath.c_str());
    c.fontFace = buf;
    c.fontSizePx = GetPrivateProfileIntW(L"main", L"fontSize", 12, iniPath.c_str());
    // геометрия окна: "x,y,w,h" одной строкой (минимум места)
    GetPrivateProfileStringW(L"main", L"winRect", L"", buf, 1024, iniPath.c_str());
    if (buf[0]) {
        int x = -1, y = -1, w = 0, h = 0;
        if (std::swscanf(buf, L"%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0) {
            c.winX = x;
            c.winY = y;
            c.winW = w;
            c.winH = h;
        }
    }
    c.winMax = GetPrivateProfileIntW(L"main", L"winMax", 0, iniPath.c_str()) != 0;
    return true;
}

bool SaveConfig(const Config& c, const std::wstring& iniPath) {
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%d", c.theme);
    WritePrivateProfileStringW(L"main", L"theme", buf, iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"editor", c.editor.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"leftDir", c.leftDir.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"rightDir", c.rightDir.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"confirmDelete", c.confirmDelete ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"confirmOverwrite", c.confirmOverwrite ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"showHidden", c.showHidden ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"folderIndexRefresh", c.folderIndexRefresh ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"main", L"fontFace", c.fontFace.c_str(), iniPath.c_str());
    wchar_t fb[16];
    std::swprintf(fb, 16, L"%d", c.fontSizePx);
    WritePrivateProfileStringW(L"main", L"fontSize", fb, iniPath.c_str());
    // геометрия окна одной строкой: x,y,w,h
    if (c.winX != -1 && c.winY != -1 && c.winW > 0 && c.winH > 0) {
        wchar_t wr[64];
        std::swprintf(wr, 64, L"%d,%d,%d,%d", c.winX, c.winY, c.winW, c.winH);
        WritePrivateProfileStringW(L"main", L"winRect", wr, iniPath.c_str());
    }
    WritePrivateProfileStringW(L"main", L"winMax", c.winMax ? L"1" : L"0", iniPath.c_str());
    return true;
}

// ---------- Пользовательское меню (F2) ----------

bool ReadUserMenu(const std::wstring& iniPath, std::vector<UserMenuEntry>& out) {
    out.clear();
    size_t cap = 16384;
    std::vector<wchar_t> buf(cap);
    DWORD n = GetPrivateProfileSectionW(L"usermenu", buf.data(), (DWORD)cap, iniPath.c_str());
    if (n == 0) return false;  // секции нет или она пуста
    if (n >= (DWORD)cap - 2) { // возможно, обрезано — попробуем больший буфер
        cap = 65536;
        buf.assign(cap, 0);
        n = GetPrivateProfileSectionW(L"usermenu", buf.data(), (DWORD)cap, iniPath.c_str());
    }
    std::vector<std::pair<int, UserMenuEntry>> tmp;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        std::wstring line(p);
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        size_t bar = val.find(L'|');
        UserMenuEntry e;
        if (bar == std::wstring::npos) {
            e.label = val;
            e.command = val;
        } else {
            e.label = val.substr(0, bar);
            e.command = val.substr(bar + 1);
        }
        Trim(e.label);
        Trim(e.command);
        if (e.label.empty()) e.label = e.command;
        if (e.command.empty()) continue;
        int num = 0;
        for (wchar_t ch : key)
            if (ch >= L'0' && ch <= L'9') num = num * 10 + (ch - L'0');
        tmp.push_back({ num, e });
    }
    std::sort(tmp.begin(), tmp.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& t : tmp) out.push_back(std::move(t.second));
    return !out.empty();
}
