#include "fileops.h"
#include "pathutil.h"
#include "fsenum.h"
#include <winioctl.h>
#include <cwchar>

// ---------------------------------------------------------------------------
// Движок копирования: конвейер "читатель -> писатель" на overlapped I/O.
//   * источник: FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED
//   * приёмник: FILE_FLAG_OVERLAPPED (+ SEQUENTIAL_SCAN как подсказка)
//   * 3 буфера по 4 МБ, чтение опережает запись (чтение не ждёт запись)
//   * крупные файлы (>8 МБ): предвыделение места FileAllocationInfo —
//     NTFS выделяет непрерывные экстенты, меньше фрагментации на HDD
//   * мелкие файлы (<1 МБ): быстрая ветка — одно чтение + одна запись
//   * БОНУС: ReFS block-clone (FSCTL_DUPLICATE_EXTENTS_TO_FILE) —
//     мгновенное копирование без чтения данных (если том ReFS)
//   * ERROR_SHARING_VIOLATION / ERROR_LOCK_VIOLATION: 3 повтора с паузой 50 мс
//   * скорость считается скользящим окном 300 мс
//
// Сознательно НЕ используем FILE_FLAG_NO_BUFFERING (unbuffered-запись):
// с ним нужно выравнивать буферы и длины по границе сектора, а хвост файла
// довыравнивать через SetFilePointerEx+SetEndOfFile; кроме того unbuffered-
// запись недопустима вместе с block-clone и ломается на файловых системах
// со сжатием. Конвейер на overlapped I/O + предвыделение даёт близкую к
// теоретической пропускную способность и без этих рисков.
// ---------------------------------------------------------------------------

namespace {

constexpr size_t  kBufSize         = 4ull << 20;  // буфер конвейера
constexpr int     kBufCount        = 3;           // 3 буфера по 4 МБ
constexpr uint64_t kBigThreshold   = 8ull << 20;  // предвыделение места
constexpr uint64_t kCloneThreshold = 4ull << 20;  // порог ReFS block-clone
constexpr uint64_t kSmallThreshold = 1ull << 20;  // быстрая ветка
constexpr DWORD  kSpeedWindowMs    = 300;         // окно скользящей скорости
constexpr int    kMaxRetries       = 3;           // повторы при sharing-ошибках
constexpr DWORD  kRetrySleepMs     = 50;

bool IsSharingError(DWORD err) {
    return err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION;
}

std::wstring ErrText() {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, GetLastError(), 0, (wchar_t*)&buf, 0, nullptr);
    std::wstring s = buf ? std::wstring(buf, n) : L"неизвестная ошибка";
    if (buf) LocalFree(buf);
    return s;
}

// Открытие с ретраями на sharing-ошибки (3 повтора × 50 мс).
HANDLE OpenWithRetry(const std::function<HANDLE()>& open) {
    DWORD err = ERROR_SUCCESS;
    for (int i = 0; i <= kMaxRetries; i++) {
        HANDLE h = open();
        if (h != INVALID_HANDLE_VALUE) return h;
        err = GetLastError();
        if (!IsSharingError(err)) break;
        if (i < kMaxRetries) Sleep(kRetrySleepMs);
    }
    SetLastError(err);
    return INVALID_HANDLE_VALUE;
}

// Кэш поддержки block-clone по букве тома (бонус).
std::mutex g_cloneMtx;
std::unordered_map<std::wstring, bool> g_cloneCache;

bool VolumeSupportsClone(const std::wstring& root) {
    std::lock_guard<std::mutex> g(g_cloneMtx);
    auto it = g_cloneCache.find(root);
    if (it != g_cloneCache.end()) return it->second;
    DWORD flags = 0;
    bool ok = GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr,
                                    &flags, nullptr, 0) != 0;
    bool sup = ok && (flags & FILE_SUPPORTS_BLOCK_REFCOUNTING) != 0;
    g_cloneCache[root] = sup;
    return sup;
}

// БОНУС: мгновенное копирование через ReFS block-clone (данные не читаются
// и не пишутся — файл получает ссылки на те же экстенты). Если FSCTL не
// вышел (не ReFS, другой том, сжатый файл и т.п.) — возвращаем false,
// вызывающий продолжает обычным копированием. Операция атомарна.
bool TryBlockClone(HANDLE hs, HANDLE hd, uint64_t size,
                   const std::wstring& src, const std::wstring& dst) {
    if (size < kCloneThreshold) return false;
    if (!IsSameVolume(src, dst)) return false;
    if (!VolumeSupportsClone(RootOf(dst))) return false;
    DUPLICATE_EXTENTS_DATA d{};
    d.FileHandle = hs;
    d.SourceFileOffset.QuadPart = 0;
    d.TargetFileOffset.QuadPart = 0;
    d.ByteCount.QuadPart = (LONGLONG)size;
    DWORD junk = 0;
    return DeviceIoControl(hd, FSCTL_DUPLICATE_EXTENTS_TO_FILE,
                           &d, sizeof(d), nullptr, 0, &junk, nullptr) != FALSE;
}

struct CopyState {
    const CopyOptions* opt = nullptr;
    ProgressFn prog;
    AskFn ask;
    std::atomic<bool>* cancel = nullptr;
    std::atomic<bool>* pause = nullptr;
    CopyOutcome* out = nullptr;
    uint64_t totalBytes = 0, totalFiles = 0;
    uint64_t bytesDone = 0, filesDone = 0;
    ULONGLONG windowStart = 0;
    uint64_t windowBytes = 0;
    double speed = 0;
    std::wstring current;
    bool yesAll = false, noAll = false;

    bool CheckCancel() {
        if (pause && pause->load()) {
            Report(1);
            while (pause->load() && !cancel->load()) Sleep(40);
            Report(0);
        }
        return cancel->load();
    }

    void Report(int state = 0) {
        if (!prog) return;
        CopyProgress p;
        p.bytesDone = bytesDone; p.bytesTotal = totalBytes;
        p.filesDone = filesDone; p.filesTotal = totalFiles;
        p.speedBps = speed;
        p.current = current;
        p.state = state;
        prog(p);
    }

    // Скользящее окно 300 мс.
    void Tick(uint64_t nbytes) {
        bytesDone += nbytes;
        ULONGLONG now = GetTickCount64();
        if (now - windowStart >= kSpeedWindowMs) {
            if (windowStart) {
                speed = double(bytesDone - windowBytes) / (double(now - windowStart) / 1000.0);
                Report(0);
            }
            windowStart = now;
            windowBytes = bytesDone;
        }
    }

    void Fail(const std::wstring& msg) {
        out->ok = false;
        out->errors.push_back(msg + L" (" + ErrText() + L")");
    }

    AskChoice AskOverwrite(const std::wstring& src, const std::wstring& dst,
                           uint64_t szSrc, uint64_t szDst, FILETIME tSrc, FILETIME tDst) {
        switch (opt->overwrite) {
        case Overwrite::Always: return AskChoice::Yes;
        case Overwrite::Never: return AskChoice::No;
        case Overwrite::IfNewer:
            return CompareFileTime(&tSrc, &tDst) > 0 ? AskChoice::Yes : AskChoice::No;
        default: break;
        }
        if (yesAll) return AskChoice::Yes;
        if (noAll) return AskChoice::No;
        if (!ask) return AskChoice::Yes;
        AskChoice c = ask(src, dst, szSrc, szDst, tSrc, tDst);
        if (c == AskChoice::YesAll) { yesAll = true; return AskChoice::Yes; }
        if (c == AskChoice::NoAll) { noAll = true; return AskChoice::No; }
        return c;
    }
};

// Одна операция (чтение/запись) с ретраями на sharing-ошибки.
// Возвращает число байт или ~0u при ошибке.
DWORD SmallIo(HANDLE h, OVERLAPPED* ov, void* buf, DWORD n, bool isWrite) {
    for (int attempt = 0;; attempt++) {
        DWORD got = 0;
        BOOL ok = isWrite ? WriteFile(h, buf, n, &got, ov) : ReadFile(h, buf, n, &got, ov);
        DWORD err = ok ? ERROR_SUCCESS : GetLastError();
        if (err == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(h, ov, &got, TRUE);
            err = ok ? ERROR_SUCCESS : GetLastError();
        }
        if (ok && got == n) return got;
        if (!IsSharingError(err) || attempt >= kMaxRetries) return ~0u;
        Sleep(kRetrySleepMs);
    }
}

// Быстрая ветка для файлов < 1 МБ: одно чтение + одна запись, без конвейера
// и без предвыделения (мелким файлам предвыделение не нужно).
bool CopySmall(CopyState& st, HANDLE hs, HANDLE hd, uint64_t size) {
    if (size == 0) return true;
    std::vector<char> buf((size_t)size);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    DWORD n = (DWORD)size;
    bool ok = SmallIo(hs, &ov, buf.data(), n, false) != ~0u &&
              SmallIo(hd, &ov, buf.data(), n, true) != ~0u;
    CloseHandle(ov.hEvent);
    if (ok) st.Tick(size);
    return ok;
}

// Основной конвейер: overlapped-чтение опережает запись на kBufCount буферов.
// Событийный цикл: ждём завершения ЛЮБОЙ операции (WaitForMultipleObjects по
// событиям буферов); готовые данные пишутся строго по порядку блоков, а
// освободившийся буфер сразу получает следующее чтение — пока не наткнёмся
// на конец файла.
bool CopyBig(CopyState& st, HANDLE hs, HANDLE hd) {
    enum Phase { Idle, Reading, Data, Writing };

    struct Buf {
        std::unique_ptr<char[]> mem;
        OVERLAPPED ov{};
        uint64_t block = 0;   // номер блока, находящегося в буфере
        DWORD n = 0;          // сколько байт прочитано (для записи)
        Phase phase = Idle;
    };

    Buf bufs[kBufCount];
    for (int k = 0; k < kBufCount; k++) {
        bufs[k].mem = std::make_unique<char[]>(kBufSize);
        bufs[k].ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!bufs[k].ov.hEvent) {
            for (int j = 0; j < k; j++) CloseHandle(bufs[j].ov.hEvent);
            return false;  // память под mem освободится автоматически
        }
    }

    auto setOff = [](OVERLAPPED& ov, uint64_t off) {
        ov.Offset = (DWORD)(off & 0xFFFFFFFFull);
        ov.OffsetHigh = (DWORD)(off >> 32);
    };

    // Выдать асинхронное чтение блока. false — синхронная ошибка выдачи.
    auto issueRead = [&](Buf& b) -> bool {
        setOff(b.ov, b.block * kBufSize);
        b.n = 0;
        BOOL ok = ReadFile(hs, b.mem.get(), (DWORD)kBufSize, nullptr, &b.ov);
        if (ok || GetLastError() == ERROR_IO_PENDING) return true;
        return false;
    };

    // Выдать асинхронную запись. false — синхронная ошибка выдачи.
    auto issueWrite = [&](Buf& b) -> bool {
        setOff(b.ov, b.block * kBufSize);
        BOOL ok = WriteFile(hd, b.mem.get(), b.n, nullptr, &b.ov);
        if (ok || GetLastError() == ERROR_IO_PENDING) return true;
        return false;
    };

    // Забрать результат завершённой операции (событие уже сработало).
    // При sharing-ошибке операция повторяется (до kMaxRetries раз).
    auto takeResult = [&](HANDLE h, Buf& b, uint64_t* nbytes) -> bool {
        for (int attempt = 0;; attempt++) {
            DWORD got = 0;
            // bWait=TRUE безопасно: событие уже сработало; после повторной
            // выдачи — ждём завершения нового запроса.
            if (GetOverlappedResult(h, &b.ov, &got, TRUE)) {
                *nbytes = got;
                return true;
            }
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) return false;  // отменено
            if (!IsSharingError(err) || attempt >= kMaxRetries) return false;
            Sleep(kRetrySleepMs);
            bool issued = (b.phase == Reading) ? issueRead(b) : issueWrite(b);
            if (!issued) return false;
        }
    };

    // Гасим незавершённый I/O и закрываем события буферов.
    auto drainAndClose = [&]() {
        CancelIoEx(hs, nullptr);
        CancelIoEx(hd, nullptr);
        for (auto& b : bufs) {
            if (b.phase == Reading || b.phase == Writing) {
                HANDLE h = (b.phase == Reading) ? hs : hd;
                DWORD got;
                GetOverlappedResult(h, &b.ov, &got, TRUE);
                b.phase = Idle;
            }
            if (b.ov.hEvent) CloseHandle(b.ov.hEvent);
        }
    };

    // Старт: читаем первые kBufCount блоков (опережение на kBufCount).
    uint64_t nextRead = 0;  // следующий блок для выдачи чтения
    for (int k = 0; k < kBufCount; k++) {
        bufs[k].block = nextRead++;
        if (!issueRead(bufs[k])) {
            DWORD err = GetLastError();
            drainAndClose();
            SetLastError(err);
            return false;
        }
        bufs[k].phase = Reading;
    }

    uint64_t writeHead = 0;  // следующий блок для записи (строго по порядку)
    bool eof = false;
    bool ok = true;

    while (true) {
        if (st.CheckCancel()) { st.out->canceled = true; ok = false; break; }

        // Записываем все готовые блоки по порядку (блок writeHead может
        // лежать в любом буфере — номера блоков распределяются динамически).
        bool progressed;
        do {
            progressed = false;
            for (int k = 0; k < kBufCount; k++) {
                if (bufs[k].phase == Data && bufs[k].block == writeHead) {
                    if (!issueWrite(bufs[k])) { ok = false; break; }
                    bufs[k].phase = Writing;
                    writeHead++;
                    progressed = true;
                }
            }
        } while (progressed && ok);
        if (!ok) break;

        // Ждём завершения ЛЮБОЙ активной операции.
        HANDLE evs[kBufCount];
        Buf* map[kBufCount];
        int n = 0;
        for (int k = 0; k < kBufCount; k++) {
            if (bufs[k].phase == Reading || bufs[k].phase == Writing) {
                evs[n] = bufs[k].ov.hEvent;
                map[n] = &bufs[k];
                n++;
            }
        }
        if (n == 0) break;  // всё сделано
        DWORD r = WaitForMultipleObjects((DWORD)n, evs, FALSE, INFINITE);
        if (r < WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + (DWORD)n) { ok = false; break; }
        Buf* b = map[r - WAIT_OBJECT_0];

        if (b->phase == Reading) {
            uint64_t got = 0;
            if (takeResult(hs, *b, &got)) {
                if (got == 0) {          // конец файла
                    b->phase = Idle;
                    eof = true;
                } else {
                    b->n = (DWORD)got;
                    b->phase = Data;
                }
            } else if (GetLastError() == ERROR_HANDLE_EOF) {
                // Чтение за концом файла — чистый EOF (overlapped-чтение
                // возвращает ERROR_HANDLE_EOF вместо нуля байт).
                b->phase = Idle;
                eof = true;
            } else {
                ok = false;
                break;
            }
        } else {  // Writing
            uint64_t written = 0;
            if (takeResult(hd, *b, &written)) {
                st.Tick(written);
                b->phase = Idle;
                if (!eof) {
                    // Освободившийся буфер сразу получает следующее чтение —
                    // чтение всегда опережает запись.
                    b->block = nextRead++;
                    if (!issueRead(*b)) { ok = false; break; }
                    b->phase = Reading;
                }
            } else {
                ok = false;
                break;
            }
        }
    }

    DWORD err = GetLastError();
    drainAndClose();
    SetLastError(err);
    return ok;
}

// Копирование данных одного файла с сохранением времени модификации.
bool CopyFileData(CopyState& st, const std::wstring& src, const std::wstring& dst,
                  uint64_t size, const FILETIME& tSrc) {
    HANDLE hs = OpenWithRetry([&] {
        return CreateFileW(LongPath(src).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED,
                           nullptr);
    });
    if (hs == INVALID_HANDLE_VALUE) { st.Fail(L"Не удалось открыть: " + src); return false; }

    HANDLE hd = OpenWithRetry([&] {
        return CreateFileW(LongPath(dst).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED,
                           nullptr);
    });
    if (hd == INVALID_HANDLE_VALUE) {
        CloseHandle(hs);
        st.Fail(L"Не удалось создать: " + dst);
        return false;
    }

    bool ok;
    if (TryBlockClone(hs, hd, size, src, dst)) {
        st.Tick(size);  // бонус: данные склонированы мгновенно
        ok = true;
    } else {
        if (size >= kBigThreshold) {
            // Предвыделение места: NTFS выделяет непрерывные экстенты —
            // меньше фрагментации, быстрее запись на HDD. Ошибки игнорируем
            // (на FAT и т.п. это просто не поддерживается).
            FILE_ALLOCATION_INFO ai{};
            ai.AllocationSize.QuadPart = (LONGLONG)size;
            SetFileInformationByHandle(hd, FileAllocationInfo, &ai, sizeof(ai));
        }
        ok = (size < kSmallThreshold) ? CopySmall(st, hs, hd, size)
                                      : CopyBig(st, hs, hd);
    }

    if (ok) {
        SetFileTime(hd, nullptr, nullptr, &tSrc);
        st.filesDone++;
    }
    CloseHandle(hd);
    CloseHandle(hs);
    if (!ok && !st.out->canceled) {
        DeleteFileW(LongPath(dst).c_str());
        st.Fail(L"Ошибка записи: " + dst);
    }
    return ok;
}

bool CopyOne(CopyState& st, const std::wstring& src, const std::wstring& dst);

bool CopyDir(CopyState& st, const std::wstring& src, const std::wstring& dst) {
    if (!CreateDirectoryW(LongPath(dst).c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        st.Fail(L"Не удалось создать каталог: " + dst);
        return false;
    }
    auto items = EnumerateDir(src, st.cancel);
    for (auto& e : items) {
        if (st.CheckCancel()) { st.out->canceled = true; return false; }
        CopyOne(st, PathJoin(src, e.name), PathJoin(dst, e.name));
        if (st.out->canceled) return false;
    }
    return true;
}

bool CopyOne(CopyState& st, const std::wstring& src, const std::wstring& dst) {
    if (st.CheckCancel()) { st.out->canceled = true; return false; }
    st.current = src;

    // Перемещение в пределах тома — мгновенное переименование.
    if (st.opt->move && IsSameVolume(src, dst)) {
        bool dstExists = FileExists(dst) || DirExists(dst);
        if (dstExists) {
            WIN32_FILE_ATTRIBUTE_DATA sd{}, dd{};
            GetFileAttributesExW(LongPath(src).c_str(), GetFileExInfoStandard, &sd);
            GetFileAttributesExW(LongPath(dst).c_str(), GetFileExInfoStandard, &dd);
            AskChoice c = st.AskOverwrite(src, dst, 0, 0, sd.ftLastWriteTime, dd.ftLastWriteTime);
            if (c == AskChoice::Cancel) { st.out->canceled = true; return false; }
            if (c == AskChoice::No) return true;
        }
        if (MoveFileExW(LongPath(src).c_str(), LongPath(dst).c_str(),
                        MOVEFILE_REPLACE_EXISTING)) {
            st.filesDone++;
            return true;
        }
        // Не вышло (занято и т.п.) — копируем и удалим источник.
    }

    if (DirExists(src)) return CopyDir(st, src, dst);

    WIN32_FILE_ATTRIBUTE_DATA sd{};
    uint64_t size = 0;
    if (GetFileAttributesExW(LongPath(src).c_str(), GetFileExInfoStandard, &sd))
        size = ((uint64_t)sd.nFileSizeHigh << 32) | sd.nFileSizeLow;

    if (FileExists(dst)) {
        WIN32_FILE_ATTRIBUTE_DATA dd{};
        GetFileAttributesExW(LongPath(dst).c_str(), GetFileExInfoStandard, &dd);
        uint64_t szD = ((uint64_t)dd.nFileSizeHigh << 32) | dd.nFileSizeLow;
        AskChoice c = st.AskOverwrite(src, dst, size, szD, sd.ftLastWriteTime, dd.ftLastWriteTime);
        if (c == AskChoice::Cancel) { st.out->canceled = true; return false; }
        if (c == AskChoice::No) return true;
    }
    return CopyFileData(st, src, dst, size, sd.ftLastWriteTime);
}

} // namespace

CopyOutcome RunCopy(const std::vector<CopyJob>& jobs, const CopyOptions& opt,
                    ProgressFn onProgress, AskFn onAsk,
                    std::atomic<bool>& cancel, std::atomic<bool>& pause) {
    CopyOutcome out;
    CopyState st;
    st.opt = &opt; st.prog = std::move(onProgress); st.ask = std::move(onAsk);
    st.cancel = &cancel; st.pause = &pause; st.out = &out;
    st.windowStart = GetTickCount64();

    // Предварительный подсчёт для прогресса.
    for (auto& j : jobs) {
        if (cancel.load()) { out.canceled = true; return out; }
        if (DirExists(j.src)) {
            st.totalBytes += CalcTreeSize(j.src, &cancel);
            st.totalFiles += 1; // каталог считается одной записью
        } else if (FileExists(j.src)) {
            WIN32_FILE_ATTRIBUTE_DATA d{};
            if (GetFileAttributesExW(LongPath(j.src).c_str(), GetFileExInfoStandard, &d))
                st.totalBytes += ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
            st.totalFiles++;
        } else {
            out.errors.push_back(L"Не найден: " + j.src);
            out.ok = false;
        }
    }

    for (auto& j : jobs) {
        if (cancel.load()) { out.canceled = true; break; }
        CopyOne(st, j.src, j.dst);
        if (out.canceled) break;
    }

    // После перемещения удаляем источники — только если они ещё существуют
    // (same-volume move уже переименовал их, ничего удалять не надо).
    if (opt.move && !out.canceled) {
        for (auto& j : jobs) {
            if (cancel.load()) { out.canceled = true; break; }
            if (!FileExists(j.src) && !DirExists(j.src)) continue;
            auto errs = DeleteDirect({ j.src });
            for (auto& e : errs) { out.errors.push_back(e); out.ok = false; }
        }
    }

    // Итоговый прогресс: 2=готово, 3=отменено, 4=ошибка.
    if (st.prog) {
        CopyProgress p;
        p.bytesDone = st.bytesDone; p.bytesTotal = st.totalBytes;
        p.filesDone = st.filesDone; p.filesTotal = st.totalFiles;
        p.speedBps = st.speed;
        p.state = out.canceled ? 3 : (out.ok ? 2 : 4);
        st.prog(p);
    }
    return out;
}

uint64_t CalcTreeSize(const std::wstring& path, std::atomic<bool>* cancel) {
    if (FileExists(path)) {
        WIN32_FILE_ATTRIBUTE_DATA d{};
        if (GetFileAttributesExW(LongPath(path).c_str(), GetFileExInfoStandard, &d))
            return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
        return 0;
    }
    uint64_t total = 0;
    auto items = EnumerateDir(path, cancel);
    for (auto& e : items) {
        if (cancel && cancel->load()) break;
        if (e.isDir) total += CalcTreeSize(PathJoin(path, e.name), cancel);
        else total += e.size;
    }
    return total;
}

std::vector<std::wstring> DeleteTree(const std::wstring& path);

std::vector<std::wstring> DeleteTree(const std::wstring& path) {
    std::vector<std::wstring> errs;
    DWORD a = GetFileAttributesW(LongPath(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) { errs.push_back(L"Не найден: " + path); return errs; }
    if (a & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(LongPath(path).c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        auto items = EnumerateDir(path, nullptr);
        for (auto& e : items) {
            auto sub = DeleteTree(PathJoin(path, e.name));
            errs.insert(errs.end(), sub.begin(), sub.end());
        }
        if (!RemoveDirectoryW(LongPath(path).c_str()))
            errs.push_back(L"Не удалось удалить: " + path);
    } else {
        if (!DeleteFileW(LongPath(path).c_str()))
            errs.push_back(L"Не удалось удалить: " + path);
    }
    return errs;
}

std::vector<std::wstring> DeleteDirect(const std::vector<std::wstring>& paths) {
    std::vector<std::wstring> errs;
    for (auto& p : paths) {
        auto e = DeleteTree(p);
        errs.insert(errs.end(), e.begin(), e.end());
    }
    return errs;
}

std::vector<std::wstring> DeleteToRecycleBin(const std::vector<std::wstring>& paths) {
    std::vector<std::wstring> errs;
    std::wstring list;
    for (auto& p : paths) { list += PathNormalize(p); list.push_back(L'\0'); }
    list.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = list.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    if (SHFileOperationW(&op) != 0)
        errs.push_back(L"Не удалось отправить в Корзину: " + paths[0]);
    return errs;
}
