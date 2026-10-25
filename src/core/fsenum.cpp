#include "fsenum.h"
#include "pathutil.h"
#include <winternl.h>
#include <cwchar>

// ---------------------------------------------------------------------------
// Быстрое перечисление каталога.
// Основной путь — NtQueryDirectoryFile(FileIdBothDirectoryInformation):
//   * один syscall возвращает сразу пачку записей (256 КБ буфер),
//   * EndOfFile/LastWriteTime/FileAttributes приходят в готовом виде,
//   * ntdll.dll резолвится динамически через GetProcAddress (без линковки).
// Fallback — FindFirstFileExW с FIND_FIRST_EX_LARGE_FETCH.
// Семантика: имена без пути, "." и ".." пропускаются, порядок не важен.
// ---------------------------------------------------------------------------

namespace {

// NtQueryDirectoryFile не объявлена в winternl.h — объявляем сами
// (остальные типы: NTSTATUS, IO_STATUS_BLOCK, FILE_INFORMATION_CLASS,
//  FILE_ID_BOTH_DIR_INFORMATION — берём из winternl.h).
typedef NTSTATUS(NTAPI* NtQueryDirectoryFileFn)(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass,
    BOOLEAN ReturnSingleEntry,
    PUNICODE_STRING FileName,
    BOOLEAN RestartScan);

constexpr LONG kStatusNoMoreFiles = (LONG)0x80000006;  // STATUS_NO_MORE_FILES

NtQueryDirectoryFileFn g_NtQueryDirectoryFile = [] {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");  // ntdll всегда загружена
    return ntdll ? (NtQueryDirectoryFileFn)(void*)GetProcAddress(ntdll, "NtQueryDirectoryFile")
                 : nullptr;
}();

// Перечисление через NtQueryDirectoryFile.
// Возвращает false, если путь NT неприменим (нет доступа/ошибка) — тогда
// вызывающий переключается на fallback.
bool EnumerateNt(const std::wstring& path, std::atomic<bool>* cancel,
                 std::vector<DirEntry>& out) {
    HANDLE h = CreateFileW(LongPath(path).c_str(),
                           FILE_LIST_DIRECTORY | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    const ULONG kBuf = 256u << 10;  // 256 КБ на запрос — меньше syscall'ов
    std::vector<char> buf(kBuf);
    UNICODE_STRING mask;
    mask.Length = 2;
    mask.MaximumLength = 2;
    mask.Buffer = (PWSTR)L"*";

    IO_STATUS_BLOCK iosb{};
    bool first = true;
    size_t scanned = 0;

    for (;;) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        NTSTATUS st = g_NtQueryDirectoryFile(h, nullptr, nullptr, nullptr, &iosb,
                                             buf.data(), kBuf,
                                             FileIdBothDirectoryInformation,
                                             FALSE, &mask, first);
        first = false;
        if (st == kStatusNoMoreFiles) break;
        if (st != 0 /*STATUS_SUCCESS*/ || iosb.Information == 0) {
            CloseHandle(h);
            return false;  // непредвиденная ошибка — пусть попробует fallback
        }
        ULONG off = 0;
        for (;;) {
            if ((scanned++ & 63) == 0 && cancel &&
                cancel->load(std::memory_order_relaxed)) {
                CloseHandle(h);
                return true;  // отменено: отдаём то, что набрали
            }
            const auto* info = (const FILE_ID_BOTH_DIR_INFORMATION*)(buf.data() + off);
            std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
            if (name != L"." && name != L"..") {
                DirEntry e;
                e.name = std::move(name);
                e.size = (uint64_t)info->EndOfFile.QuadPart;
                e.mtime = *(const FILETIME*)&info->LastWriteTime;  // LARGE_INTEGER == FILETIME
                e.attrs = info->FileAttributes;
                e.isDir = (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                out.push_back(std::move(e));
            }
            if (info->NextEntryOffset == 0) break;
            off += info->NextEntryOffset;
        }
    }
    CloseHandle(h);
    return true;
}

// Fallback: FindFirstFileExW с крупными выборками (Win7+).
void EnumerateFind(const std::wstring& path, std::atomic<bool>* cancel,
                   std::vector<DirEntry>& out) {
    std::wstring wild = LongPath(PathJoin(path, L"*"));
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(wild.c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        // старый драйвер без LARGE_FETCH — обычный вариант
        h = FindFirstFileExW(wild.c_str(), FindExInfoBasic, &fd,
                             FindExSearchNameMatch, nullptr, 0);
    }
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        DirEntry e;
        e.name = fd.cFileName;
        e.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e.mtime = fd.ftLastWriteTime;
        e.attrs = fd.dwFileAttributes;
        e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

} // namespace

std::vector<DirEntry> EnumerateDir(const std::wstring& path, std::atomic<bool>* cancel) {
    std::vector<DirEntry> out;
    if (g_NtQueryDirectoryFile) {
        if (EnumerateNt(path, cancel, out)) return out;
        out.clear();
    }
    EnumerateFind(path, cancel, out);
    return out;
}

// Доступность NT-пути (для тестов/диагностики; в заголовок не выносим).
bool FsEnumNtAvailable() {
    return g_NtQueryDirectoryFile != nullptr;
}

std::vector<VolumeInfo> GetVolumes() {
    std::vector<VolumeInfo> vols;
    DWORD drives = GetLogicalDrives();
    for (wchar_t c = L'A'; c <= L'Z'; c++) {
        if (!(drives & (1u << (c - L'A')))) continue;
        std::wstring root;
        root += c; root += L":\\";
        VolumeInfo v;
        v.letter = c;
        wchar_t label[128] = L"";
        GetVolumeInformationW(root.c_str(), label, 128, nullptr, nullptr, nullptr, nullptr, 0);
        v.label = label;
        ULARGE_INTEGER avail{}, total{}, totalFree{};
        if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &totalFree)) {
            v.free = avail.QuadPart;
            v.total = total.QuadPart;
        }
        UINT t = GetDriveTypeW(root.c_str());
        v.ready = t != DRIVE_NO_ROOT_DIR;
        vols.push_back(std::move(v));
    }
    return vols;
}
