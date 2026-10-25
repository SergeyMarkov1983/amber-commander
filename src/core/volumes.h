#pragma once
#include "common.h"

// Элемент линейки дисков: буквы дисков, флешки, сетевые диски и облачные папки.
struct DriveItem {
    std::wstring label;   // "C:" или "OneDrive"
    std::wstring path;    // "C:\" или полный путь облака
    uint64_t free = 0;
    uint64_t total = 0;
    bool isCloud = false;
    bool isNetwork = false;
    bool ready = true;
    UINT type = 0;        // DRIVE_FIXED / DRIVE_REMOVABLE / DRIVE_REMOTE / DRIVE_CDROM ...
};

// Все доступные точки перехода: диски (GetVolumes) + облачные папки.
std::vector<DriveItem> GetDriveItems();
