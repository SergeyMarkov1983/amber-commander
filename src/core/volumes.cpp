#include "volumes.h"
#include "fsenum.h"
#include "pathutil.h"

// Собирает полный список точек перехода для линейки дисков и диалога выбора:
// - локальные диски (fixed)
// - съёмные носители (флешки, карты)
// - CD/DVD
// - сетевые диски (подключённые)
// - облачные папки: OneDrive (личный/бизнес), Dropbox, Google Drive (обе схемы),
//   iCloud Drive, Box, Яндекс.Диск, pCloud, MEGA

namespace {

std::wstring GetEnvVar(const wchar_t* name) {
    wchar_t buf[2048];
    DWORD n = GetEnvironmentVariableW(name, buf, 2048);
    return n ? std::wstring(buf, n) : std::wstring();
}

bool AlreadyListed(const std::vector<DriveItem>& out, const std::wstring& path) {
    std::wstring np = PathNormalize(path);
    for (auto& d : out)
        if (PathNormalize(d.path) == np) return true;
    return false;
}

void AddFolder(std::vector<DriveItem>& out, const std::wstring& label, const std::wstring& path,
               bool isCloud) {
    if (path.empty() || !DirExists(path)) return;
    if (AlreadyListed(out, path)) return;
    DriveItem d;
    d.label = label;
    d.path = PathNormalize(path);
    d.isCloud = isCloud;
    d.type = DRIVE_FIXED;
    ULARGE_INTEGER a{}, t{}, tf{};
    if (GetDiskFreeSpaceExW(RootOf(path).c_str(), &a, &t, &tf)) {
        d.free = a.QuadPart;
        d.total = t.QuadPart;
    }
    out.push_back(std::move(d));
}

} // namespace

std::vector<DriveItem> GetDriveItems() {
    std::vector<DriveItem> out;

    // 1) Диски: локальные, съёмные, сетевые, CD
    for (auto& v : GetVolumes()) {
        DriveItem d;
        d.label = std::wstring(1, v.letter) + L":";
        d.path = RootOf(d.label);
        d.free = v.free;
        d.total = v.total;
        d.ready = v.ready;
        d.type = GetDriveTypeW(d.path.c_str());
        d.isNetwork = d.type == DRIVE_REMOTE;
        out.push_back(std::move(d));
    }

    // 2) Облака: сначала явные переменные окружения OneDrive,
    //    затем сканирование профиля пользователя.
    std::wstring prof = GetEnvVar(L"USERPROFILE");
    std::wstring od = GetEnvVar(L"OneDriveConsumer");
    if (od.empty()) od = GetEnvVar(L"OneDrive");
    AddFolder(out, L"OneDrive", od, true);
    AddFolder(out, L"OneDrive (бизнес)", GetEnvVar(L"OneDriveCommercial"), true);

    if (!prof.empty() && DirExists(prof)) {
        std::wstring wild = LongPath(PathJoin(prof, L"*"));
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(wild.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == L'.') continue;
                std::wstring name = fd.cFileName;
                std::wstring lower = name;
                for (auto& c : lower) c = (wchar_t)LowerChar(c);
                std::wstring path = PathJoin(prof, name);
                if (lower.rfind(L"onedrive", 0) == 0) AddFolder(out, name, path, true);
                else if (lower == L"dropbox") AddFolder(out, L"Dropbox", path, true);
                else if (lower == L"google drive") AddFolder(out, L"Google Drive", path, true);
                else if (lower == L"iclouddrive") AddFolder(out, L"iCloud Drive", path, true);
                else if (lower == L"box") AddFolder(out, L"Box", path, true);
                else if (lower == L"yandexdisk") AddFolder(out, L"Яндекс.Диск", path, true);
                else if (lower == L"pclouddrive") AddFolder(out, L"pCloud", path, true);
                else if (lower == L"megasync") AddFolder(out, L"MEGA", path, true);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    // Google Drive по схеме DriveFS (локальный кэш в LOCALAPPDATA)
    std::wstring la = GetEnvVar(L"LOCALAPPDATA");
    if (!la.empty())
        AddFolder(out, L"Google Drive", PathJoin(la, L"Google\\DriveFS"), true);

    return out;
}
