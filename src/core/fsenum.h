#pragma once
#include "common.h"

// Быстрое перечисление каталога (без "." и "..", без сортировки).
// path — нормализованный путь без хвостового слэша.
// При cancel->load() == true перечисление прекращается.
std::vector<DirEntry> EnumerateDir(const std::wstring& path, std::atomic<bool>* cancel = nullptr);

struct VolumeInfo {
    wchar_t letter = 0;
    std::wstring label;
    uint64_t total = 0;
    uint64_t free = 0;
    bool ready = true;
};
std::vector<VolumeInfo> GetVolumes();
