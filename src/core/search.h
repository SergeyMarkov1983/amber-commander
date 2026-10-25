#pragma once
#include "common.h"

struct SearchQuery {
    std::wstring root;
    std::wstring masks;      // фрагменты имён и маски: "kotor;*.txt", пусто = все
    std::wstring text;       // текст в файле (пусто = только имена)
    uint64_t sizeMin = 0;
    uint64_t sizeMax = UINT64_MAX;
    bool subdirs = true;
    bool caseSensitive = false;
    bool matchFiles = true;
    bool matchDirs = true;
    uint64_t maxContentFile = 256ull * 1024 * 1024; // файлы крупнее не сканировать
};

// onHit вызывается из рабочих потоков; UI должен маршалить данные сам.
using HitFn = std::function<void(const std::wstring& fullPath, const DirEntry& e)>;

void RunSearch(const SearchQuery& q, HitFn onHit, std::atomic<bool>& cancel);
