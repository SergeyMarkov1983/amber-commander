#pragma once
#include "common.h"

struct SizeItem {
    std::wstring name;
    bool isDir = false;
    uint64_t size = 0;    // размещённый размер (для каталога — сумма поддерева)
    uint64_t files = 0;   // число файлов внутри (для каталогов)
};

// Размеры всех элементов каталога: вложенные каталоги — с суммами поддеревьев.
// Результат отсортирован по убыванию размера (как в WizTree).
// Быстрый путь — прямой скан MFT тома; при неудаче (не NTFS, нет прав) —
// параллельное рекурсивное перечисление.
std::vector<SizeItem> ScanFolderSizes(const std::wstring& path, std::atomic<bool>* cancel);

// Все каталоги тома через MFT (пути без буквы диска, например L"Windows\System32").
// Пустой результат — MFT недоступен (нет прав/не NTFS).
std::vector<std::wstring> ScanVolumeFolders(const std::wstring& volume, std::atomic<bool>* cancel);
