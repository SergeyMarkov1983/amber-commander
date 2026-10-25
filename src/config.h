#pragma once
#include "common.h"

struct Config {
    int theme = 0;                       // 0 = светлая, 1 = тёмная
    std::wstring editor = L"notepad.exe";
    std::wstring leftDir, rightDir;
    bool confirmDelete = true;
    bool confirmOverwrite = true;
    bool showHidden = true;
    std::wstring fontFace = L"Segoe UI Variable Text";
    int fontSizePx = 12;
    // геометрия окна (winX = -1 — не сохранялась, ставить по центру)
    int winX = -1, winY = -1, winW = 1240, winH = 800;
    bool winMax = false;
};

// Если редактор не настроен — ищет Notepad++ (реестр App Paths и стандартные пути).
std::wstring FindNotepadPlusPlus();

bool LoadConfig(Config& c, const std::wstring& iniPath);
bool SaveConfig(const Config& c, const std::wstring& iniPath);
