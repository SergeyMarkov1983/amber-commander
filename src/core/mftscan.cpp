#include "mftscan.h"
#include "fsenum.h"
#include "pathutil.h"
#include "fileops.h"
#include <winioctl.h>
#include <deque>
#include <condition_variable>

// Прямой скан MFT (таблицы NTFS) — тот же принцип, что в WizTree:
// одно последовательное чтение таблицы + разбор записей $FILE_NAME/$DATA.
// По памяти: ~100 МБ на 2 млн записей (время скана), затем всё освобождается.

namespace {

// ---------- чтение и разбор MFT ----------

struct RawEntry {
    uint64_t frn;
    uint64_t parent;
    uint64_t allocated;
    uint32_t nameOff;
    bool isDir;
};

bool ReadMft(const std::wstring& volumePath, std::vector<RawEntry>& out,
             std::wstring& nameBuf, std::atomic<bool>* cancel) {
    HANDLE h = CreateFileW(volumePath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    NTFS_VOLUME_DATA_BUFFER vd{};
    DWORD br = 0;
    bool ok = DeviceIoControl(h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                              &vd, sizeof(vd), &br, nullptr) != 0;
    if (!ok) {
        CloseHandle(h);
        return false;
    }
    uint64_t mftSize = vd.MftValidDataLength.QuadPart;
    uint64_t bytesPerRecord = vd.BytesPerFileRecordSegment;
    if (!bytesPerRecord || bytesPerRecord > 65536 || !mftSize) {
        CloseHandle(h);
        return false;
    }
    uint64_t mftOffset = (uint64_t)vd.MftStartLcn.QuadPart * vd.BytesPerCluster;
    uint64_t nRecords = mftSize / bytesPerRecord;

    out.clear();
    out.reserve((size_t)std::min<uint64_t>(nRecords, 4000000));
    nameBuf.clear();
    nameBuf.reserve((size_t)std::min<uint64_t>(nRecords, 4000000) * 12);

    LARGE_INTEGER off{};
    off.QuadPart = (LONGLONG)mftOffset;
    if (!SetFilePointerEx(h, off, nullptr, FILE_BEGIN)) {
        CloseHandle(h);
        return false;
    }

    const uint64_t kChunk = 16ull << 20;
    std::vector<char> buf((size_t)kChunk);
    uint64_t read = 0;
    while (read < mftSize) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        DWORD want = (DWORD)std::min<uint64_t>(kChunk, mftSize - read);
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) break;

        for (size_t offIn = 0; offIn + bytesPerRecord <= got; offIn += (size_t)bytesPerRecord) {
            const char* rec = buf.data() + offIn;
            if (memcmp(rec, "FILE", 4) != 0) continue;
            uint16_t flags = *(const uint16_t*)(rec + 0x16);
            if (!(flags & 0x01)) continue;               // запись не используется
            RawEntry e;
            e.frn = read / bytesPerRecord + offIn / bytesPerRecord;
            e.isDir = (flags & 0x02) != 0;
            e.parent = 0;
            e.allocated = 0;
            e.nameOff = 0xFFFFFFFFu;
            uint32_t attrOff = *(const uint16_t*)(rec + 0x14);
            uint32_t off2 = attrOff;
            bool haveName = false;
            while (off2 + 16 <= bytesPerRecord) {
                const char* attr = rec + off2;
                uint32_t type = *(const uint32_t*)attr;
                if (type == 0xFFFFFFFF) break;
                uint32_t len = *(const uint32_t*)(attr + 4);
                if (len < 16 || off2 + len > bytesPerRecord) break;
                uint8_t nonRes = *(const uint8_t*)(attr + 8);
                uint8_t nlen = *(const uint8_t*)(attr + 9);
                uint16_t noff = *(const uint16_t*)(attr + 10);
                if (type == 0x30 && !nonRes && !haveName) {
                    const char* body = attr + noff;
                    uint32_t valLen = *(const uint32_t*)(attr + 16);
                    // защита от повреждённых записей: смещение имени внутри атрибута
                    if ((uint32_t)noff + 0x42u > len) break;
                    if (valLen >= 0x42) {
                        uint8_t ns = *(const uint8_t*)(body + 0x41);
                        if (ns == 1 || ns == 3) {        // длинное Win32-имя
                            uint8_t nl = *(const uint8_t*)(body + 0x40);
                            if (nl > 0 && 0x42u + (uint32_t)nl * 2 <= valLen) {
                                e.parent = *(const uint64_t*)body & 0x0000FFFFFFFFFFFFull;
                                e.nameOff = (uint32_t)nameBuf.size();
                                nameBuf.append((const wchar_t*)(body + 0x42), nl);
                                nameBuf.push_back(L'\0');
                                haveName = true;
                            }
                        }
                    }
                } else if (type == 0x80) {
                    if (len >= 0x30) {
                        if (nonRes) e.allocated = *(const uint64_t*)(attr + 0x28);
                        else e.allocated = *(const uint32_t*)(attr + 16);
                    }
                }
                off2 += len;
            }
            if (!haveName) continue;
            out.push_back(e);
        }
        read += got;
    }
    CloseHandle(h);
    return !out.empty();
}

const wchar_t* NameAt(const std::wstring& buf, uint32_t off) {
    return buf.c_str() + off;
}

// ---------- агрегация поддерева целевого каталога ----------

struct Agg {
    uint64_t size = 0;
    uint64_t files = 0;
};

// Итеративный DFS по поддереву; дети ищутся по отсортированному массиву индексов.
bool AggregateSubtree(uint64_t rootFrn, const std::vector<RawEntry>& e,
                      const std::vector<uint32_t>& byParent, const std::wstring& nameBuf,
                      std::unordered_map<uint64_t, Agg>& cache,
                      std::atomic<bool>* cancel) {
    struct Frame {
        uint64_t frn;
        uint64_t own;
        size_t childPos;
        size_t childEnd;
        uint64_t size, files;
        bool started;
    };
    std::vector<Frame> st;
    auto it = cache.find(rootFrn);
    if (it != cache.end()) return true;   // уже посчитано
    st.push_back({ rootFrn, 0, 0, 0, 0, 0, false });
    while (!st.empty()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        Frame& f = st.back();
        if (!f.started) {
            f.started = true;
            // прямой диапазон детей по отсортированному byParent
            uint64_t lo = std::lower_bound(byParent.begin(), byParent.end(), f.frn,
                [&](uint32_t idx, uint64_t frn) { return e[idx].parent < frn; }) - byParent.begin();
            uint64_t hi = std::lower_bound(byParent.begin(), byParent.end(), f.frn,
                [&](uint32_t idx, uint64_t frn) { return e[idx].parent <= frn; }) - byParent.begin();
            f.childPos = (size_t)lo;
            f.childEnd = (size_t)hi;
            f.size = f.own;
            f.files = 0;
        }
        if (f.childPos < f.childEnd) {
            uint32_t ci = byParent[f.childPos++];
            const RawEntry& c = e[ci];
            auto cached = cache.find(c.frn);
            if (cached != cache.end()) {
                f.size += cached->second.size;
                f.files += cached->second.files;
                continue;
            }
            if (c.isDir) {
                st.push_back({ c.frn, c.allocated, 0, 0, 0, 0, false });
            } else {
                f.size += c.allocated;
                f.files += 1;
            }
        } else {
            Agg a{ f.size, f.files };
            cache[f.frn] = a;
            st.pop_back();
            if (!st.empty()) {
                st.back().size += f.size;
                st.back().files += f.files;
            }
        }
    }
    return true;
}

} // namespace

// Все каталоги тома: разбираем parent-ссылки MFT и собираем пути от корня.
std::vector<std::wstring> ScanVolumeFolders(const std::wstring& volume,
                                            std::atomic<bool>* cancel) {
    std::vector<RawEntry> entries;
    std::wstring nameBuf;
    if (!ReadMft(volume, entries, nameBuf, cancel)) return {};
    struct D {
        uint64_t frn, parent;
        std::wstring name, path;
        bool done = false;
    };
    std::vector<D> dirs;
    dirs.reserve(entries.size() / 8);
    for (auto& e : entries) {
        if (!e.isDir) continue;
        const wchar_t* n = NameAt(nameBuf, e.nameOff);
        if (n[0] == L'.' && n[1] == 0) continue;   // «.» — сама запись
        dirs.push_back({ e.frn, e.parent, n, {}, false });
    }
    std::unordered_map<uint64_t, size_t> byFrn;
    byFrn.reserve(dirs.size() * 2);
    for (size_t i = 0; i < dirs.size(); i++) byFrn[dirs[i].frn] = i;
    for (size_t i = 0; i < dirs.size(); i++) {
        if (cancel && cancel->load()) return {};
        if (dirs[i].done) continue;
        std::vector<size_t> chain;
        size_t cur = i;
        int guard = 0;
        while (!dirs[cur].done) {
            chain.push_back(cur);
            auto it = byFrn.find(dirs[cur].parent);
            if (it == byFrn.end() || guard++ > 128) break;
            cur = it->second;
        }
        std::wstring base = dirs[cur].done ? dirs[cur].path : L"";
        for (size_t k = chain.size(); k-- > 0;) {
            size_t idx = chain[k];
            dirs[idx].path = base.empty() ? dirs[idx].name : base + L"\\" + dirs[idx].name;
            base = dirs[idx].path;
            dirs[idx].done = true;
        }
    }
    std::vector<std::wstring> out;
    out.reserve(dirs.size());
    for (auto& d : dirs)
        if (!d.path.empty()) out.push_back(std::move(d.path));
    return out;
}

std::vector<SizeItem> ScanFolderSizes(const std::wstring& path, std::atomic<bool>* cancel) {
    std::vector<SizeItem> out;
    std::wstring np = PathNormalize(path);

    // 1) быстрый путь — MFT (только локальные диски)
    std::wstring root = RootOf(np);
    if (GetDriveTypeW(root.c_str()) == DRIVE_FIXED) {
        std::wstring volume = L"\\\\.\\" + root.substr(0, 2);
        std::vector<RawEntry> entries;
        std::wstring nameBuf;
        if (ReadMft(volume, entries, nameBuf, cancel)) {
            // найти FRN целевого каталога: от корня (5) по компонентам пути
            uint64_t cur = 5;
            std::wstring rest = np;
            if (rest.size() >= 3 && rest[1] == L':') rest = rest.substr(3);
            bool found = true;
            size_t pos = 0;
            while (pos < rest.size()) {
                size_t p2 = rest.find(L'\\', pos);
                std::wstring part = p2 == std::wstring::npos
                    ? rest.substr(pos) : rest.substr(pos, p2 - pos);
                pos = (p2 == std::wstring::npos) ? rest.size() : p2 + 1;
                uint64_t next = 0;
                for (auto& en : entries) {
                    if (en.parent == cur &&
                        WcsICmp(NameAt(nameBuf, en.nameOff), part.c_str()) == 0) {
                        next = en.frn;
                        break;
                    }
                }
                if (!next) { found = false; break; }
                cur = next;
            }
            if (found) {
                // индексы, отсортированные по родителю
                std::vector<uint32_t> byParent(entries.size());
                for (uint32_t i = 0; i < entries.size(); i++) byParent[i] = i;
                std::sort(byParent.begin(), byParent.end(),
                          [&](uint32_t a, uint32_t b) { return entries[a].parent < entries[b].parent; });
                std::unordered_map<uint64_t, Agg> cache;
                cache.reserve(entries.size() / 2 + 16);
                // прямые дети целевого каталога
                std::vector<uint32_t> kids;
                for (uint32_t i = 0; i < entries.size(); i++)
                    if (entries[i].parent == cur) kids.push_back(i);
                if (!AggregateSubtree(cur, entries, byParent, nameBuf, cache, cancel)) {
                    out.clear();
                    return out;
                }
                Agg total = cache[cur];
                for (uint32_t ci : kids) {
                    if (cancel && cancel->load()) { out.clear(); return out; }
                    const RawEntry& c = entries[ci];
                    Agg a = cache[c.frn];
                    SizeItem si;
                    si.name = NameAt(nameBuf, c.nameOff);
                    si.isDir = c.isDir;
                    si.size = c.isDir ? a.size : c.allocated;
                    si.files = c.isDir ? a.files : 1;
                    out.push_back(std::move(si));
                }
                (void)total;
                std::sort(out.begin(), out.end(),
                          [](const SizeItem& a, const SizeItem& b) { return a.size > b.size; });
                return out;
            }
            // путь не найден в MFT — упадём в перечисление
        }
    }

    // 2) запасной путь (нет прав на том/MFT, не NTFS) — параллельное перечисление
    //    с общей очередью каталогов: подкаталоги распределяются между потоками,
    //    суммы накапливаются по тегу элемента верхнего уровня.
    auto items = EnumerateDir(np, cancel);
    if (items.empty()) return out;
    out.resize(items.size());
    for (size_t i = 0; i < items.size(); i++) {
        out[i].name = items[i].name;
        out[i].isDir = items[i].isDir;
    }
    std::vector<std::atomic<uint64_t>> sums(items.size());
    for (size_t i = 0; i < items.size(); i++)
        if (!items[i].isDir) sums[i].store(items[i].size);

    struct Task {
        std::wstring dir;
        size_t tag;
    };
    std::deque<Task> q;
    std::mutex m;
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].isDir) q.push_back({ PathJoin(np, items[i].name), i });

    unsigned n = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    std::atomic<int> active{ 0 };
    std::condition_variable cv;
    for (unsigned t = 0; t < n; t++) {
        pool.emplace_back([&]() {
            for (;;) {
                Task task;
                {
                    std::unique_lock<std::mutex> lk(m);
                    cv.wait(lk, [&]() {
                        return !q.empty() || active.load() == 0 ||
                               (cancel && cancel->load());
                    });
                    if (cancel && cancel->load()) return;
                    if (q.empty()) return;   // вся работа закончена
                    task = std::move(q.front());
                    q.pop_front();
                    active.fetch_add(1);
                }
                bool canceled = false;
                auto entries = EnumerateDir(task.dir, cancel);
                for (auto& e : entries) {
                    if (cancel && cancel->load()) { canceled = true; break; }
                    if (e.isDir) {
                        std::lock_guard<std::mutex> lk(m);
                        q.push_back({ PathJoin(task.dir, e.name), task.tag });
                        cv.notify_one();
                    } else {
                        sums[task.tag].fetch_add(e.size);
                    }
                }
                active.fetch_sub(1);
                cv.notify_all();
                if (canceled) return;
            }
        });
    }
    for (auto& t : pool) t.join();
    for (size_t i = 0; i < items.size(); i++) out[i].size = sums[i].load();
    std::sort(out.begin(), out.end(),
              [](const SizeItem& a, const SizeItem& b) { return a.size > b.size; });
    return out;
}
