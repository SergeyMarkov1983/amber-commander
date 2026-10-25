#include "panel.h"
#include "app.h"
#include "pathutil.h"
#include "wildcard.h"
#include "fsenum.h"

namespace {

bool EntryLess(const DirEntry& a, const DirEntry& b, int key, bool asc) {
    if (a.name == L"..") return true;
    if (b.name == L"..") return false;
    if (a.isDir != b.isDir) return a.isDir;
    int cmp = 0;
    switch (key) {
    case 1:
        cmp = WcsICmp(PathExtension(a.name).c_str(), PathExtension(b.name).c_str());
        if (!cmp) cmp = WcsICmp(a.name.c_str(), b.name.c_str());
        break;
    case 2:
        if (a.size != b.size) cmp = a.size < b.size ? -1 : 1;
        else cmp = WcsICmp(a.name.c_str(), b.name.c_str());
        break;
    case 3: {
        ULARGE_INTEGER ua{ a.mtime.dwLowDateTime, a.mtime.dwHighDateTime };
        ULARGE_INTEGER ub{ b.mtime.dwLowDateTime, b.mtime.dwHighDateTime };
        if (ua.QuadPart != ub.QuadPart) cmp = ua.QuadPart < ub.QuadPart ? -1 : 1;
        else cmp = WcsICmp(a.name.c_str(), b.name.c_str());
        break;
    }
    default:
        cmp = WcsICmp(a.name.c_str(), b.name.c_str());
        break;
    }
    return asc ? cmp < 0 : cmp > 0;
}

} // namespace

// Цвет имени по типу элемента. В обычных темах все типы = pal.text
// (как и было), в «Спектре» — свой цвет на каждый тип.
static D2D1_COLOR_F TypeTextColor(const Palette& pal, IconId id) {
    switch (id) {
    case IconId::Folder:
    case IconId::Up:    return pal.textFolder;
    case IconId::Exe:   return pal.textExe;
    case IconId::Image: return pal.textImage;
    case IconId::Video: return pal.textVideo;
    case IconId::Audio: return pal.textAudio;
    case IconId::Archive:return pal.textArchive;
    case IconId::Font:  return pal.textFont;
    case IconId::Link:  return pal.textLink;
    case IconId::Sys:
    case IconId::Drive: return pal.textSys;
    default:            return pal.textText;
    }
}

// Линейное смешение цветов (t=0 → a, t=1 → b)
static D2D1_COLOR_F MixColors(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                        a.b + (b.b - a.b) * t, a.a);
}

void Panel::Init(App* a, HWND h, int idx) {
    app = a;
    hwnd = h;
    index = idx;
}

void Panel::SetPath(const std::wstring& p, bool record) {
    if (sizeMode) ExitSizeMode();   // переход по диску/каталогу выходит из режима размеров
    std::wstring np = PathNormalize(p);
    if (np.empty()) return;
    if (!DirExists(np)) {
        if (app) app->SetStatusText(L"Каталог не найден: " + np);
        return;
    }
    if (np != path && record) {
        // запись в историю навигации (Alt+← / Alt+→)
        if (!path.empty() && (backStack.empty() || backStack.back() != path)) {
            backStack.push_back(path);
            if (backStack.size() > 64) backStack.erase(backStack.begin());
        }
        forwardStack.clear();
    }
    path = np;
    // набранный в командной строке текст сбрасывается при смене каталога
    if (app) app->ClearCmdLine();
    cancel = true;
    cancel = false;
    uint64_t g = ++gen;
    loading = true;
    int sortKeyCopy = sortKey;
    bool sortAscCopy = sortAsc;
    std::thread([this, g, np, sortKeyCopy, sortAscCopy]() {
        auto items = EnumerateDir(np, &cancel);
        if (cancel.load(std::memory_order_relaxed)) return;
        items.insert(items.begin(),
                     DirEntry{ L"..", 0, FILETIME{}, FILE_ATTRIBUTE_DIRECTORY, true });
        std::sort(items.begin() + 1, items.end(),
                  [sortKeyCopy, sortAscCopy](const DirEntry& a, const DirEntry& b) {
                      return EntryLess(a, b, sortKeyCopy, sortAscCopy);
                  });
        if (!hwnd) return;
        auto* msg = new LoadMsg{ g, new std::vector<DirEntry>(std::move(items)) };
        PostMessageW(hwnd, WM_APP_DIRLOADED, (WPARAM)index, (LPARAM)msg);
    }).detach();
}

void Panel::HistoryBack() {
    if (backStack.empty()) {
        if (app) app->SetStatusText(L"Назад по истории некуда");
        return;
    }
    forwardStack.push_back(path);
    std::wstring target = backStack.back();
    backStack.pop_back();
    SetPath(target, /*record=*/false);   // стеки ведём сами
}

void Panel::HistoryForward() {
    if (forwardStack.empty()) {
        if (app) app->SetStatusText(L"Вперёд по истории некуда");
        return;
    }
    std::wstring target = forwardStack.back();
    forwardStack.pop_back();
    backStack.push_back(path);
    SetPath(target, /*record=*/false);
}

// ── режим размеров (WizTree) ──

void Panel::EnterSizeMode(const std::wstring& root) {
    if (!DirExists(root)) return;
    if (!sizeMode) {
        sizeTopRoot = PathNormalize(root);
        sizeMode = true;
    }
    sizeRoot = PathNormalize(root);
    StartSizeScan();
    if (app) app->Redraw();
}

void Panel::ExitSizeMode() {
    if (!sizeMode) return;
    sizeMode = false;
    sizeCancel = true;
    sizeItems.clear();
    sizeLoading = false;
    Refresh();
}

void Panel::StartSizeScan() {
    sizeCancel = true;
    sizeCancel = false;
    uint64_t g = ++sizeGen;
    sizeLoading = true;
    sizeStart = GetTickCount64();
    sizeItems.clear();
    cursor = 0;
    top = 0;
    std::wstring root = sizeRoot;
    std::thread([this, g, root]() {
        auto items = ScanFolderSizes(root, &sizeCancel);
        if (sizeCancel.load(std::memory_order_relaxed)) return;
        if (!hwnd) return;
        auto* msg = new SizeMsg{ g, new std::vector<SizeItem>(std::move(items)) };
        PostMessageW(hwnd, WM_APP_SIZEREADY, (WPARAM)index, (LPARAM)msg);
    }).detach();
}

void Panel::OnSizeReady(uint64_t g, std::vector<SizeItem>* itemsPtr) {
    std::unique_ptr<std::vector<SizeItem>> items(itemsPtr);
    if (g != sizeGen.load() || !sizeMode) return;
    sizeItems = std::move(*items);
    sizeLoading = false;
    cursor = 0;
    top = 0;
    if (app) app->Redraw();
}

std::wstring Panel::SizeCursorPath() const {
    if (!sizeMode) return L"";
    if (cursor < 0 || cursor >= (int)sizeItems.size()) return L"";
    return PathJoin(sizeRoot, sizeItems[cursor].name);
}

std::wstring Panel::CursorPath() const {
    if (sizeMode) return SizeCursorPath();
    if (!snapshot || cursor < 0 || cursor >= (int)snapshot->size()) return L"";
    const DirEntry& e = (*snapshot)[cursor];
    if (e.name == L"..") return L"";
    return PathJoin(path, e.name);
}

int Panel::HitSeg(float x, float y) const {
    for (size_t i = 0; i < segs.size(); i++)
        if (x >= segs[i].rc.left && x < segs[i].rc.right &&
            y >= segs[i].rc.top && y < segs[i].rc.bottom)
            return (int)i;
    return -1;
}

void Panel::OnLoaded(uint64_t g, std::vector<DirEntry>* itemsPtr) {
    std::unique_ptr<std::vector<DirEntry>> items(itemsPtr);
    if (g != gen.load()) return;
    std::wstring curName;
    if (snapshot && cursor >= 0 && cursor < (int)snapshot->size())
        curName = (*snapshot)[cursor].name;
    snapshot = std::move(items);
    cursor = 0;
    if (!curName.empty()) {
        for (int i = 0; i < (int)snapshot->size(); i++)
            if ((*snapshot)[i].name == curName) { cursor = i; break; }
    }
    if (!pendingReveal.empty()) {
        for (int i = 0; i < (int)snapshot->size(); i++)
            if ((*snapshot)[i].name == pendingReveal) { cursor = i; break; }
        pendingReveal.clear();
    }
    // после удаления курсор встаёт на место удалённого (следующий файл,
    // или предыдущий, если удалён последний)
    if (pendingCursor >= 0) {
        cursor = std::min(pendingCursor, (int)snapshot->size() - 1);
        if (cursor < 0) cursor = 0;
        pendingCursor = -1;
    }
    top = 0;
    EnsureVisible();
    loading = false;
    if (app) app->OnPanelLoaded(this);
}

void Panel::Resort() {
    if (!snapshot || snapshot->size() < 2) return;
    auto items = std::make_shared<std::vector<DirEntry>>(*snapshot);
    std::sort(items->begin() + 1, items->end(),
              [this](const DirEntry& a, const DirEntry& b) {
                  return EntryLess(a, b, sortKey, sortAsc);
              });
    std::wstring curName;
    if (cursor >= 0 && cursor < (int)snapshot->size())
        curName = (*snapshot)[cursor].name;
    snapshot = items;
    cursor = 0;
    if (!curName.empty()) {
        for (int i = 0; i < (int)snapshot->size(); i++)
            if ((*snapshot)[i].name == curName) { cursor = i; break; }
    }
    EnsureVisible();
}

void Panel::MoveCursor(int delta) {
    int n = Count();
    if (n == 0) return;
    cursor += delta;
    if (cursor < 0) cursor = 0;
    if (cursor >= n) cursor = n - 1;
    EnsureVisible();
}

void Panel::Page(int dir) {
    MoveCursor(dir * std::max(1, rowsVisible));
}

void Panel::EnsureVisible() {
    if (!snapshot) return;
    if (cursor < top) top = cursor;
    if (cursor >= top + rowsVisible) top = cursor - rowsVisible + 1;
    if (top < 0) top = 0;
}

void Panel::QuickSearch(const std::wstring& ch) {
    if (sizeMode || !snapshot || snapshot->empty()) return;
    ULONGLONG now = GetTickCount64();
    if (now - quickTime > 1500) quick.clear();
    quickTime = now;
    quick += ch;
    for (auto& c : quick) c = (wchar_t)LowerChar(c);
    int n = (int)snapshot->size();
    for (int step = 1; step <= n; step++) {
        int i = (cursor + step) % n;
        const DirEntry& e = (*snapshot)[i];
        if (e.name == L"..") continue;
        std::wstring nm = e.name;
        for (auto& c : nm) c = (wchar_t)LowerChar(c);
        if (nm.compare(0, quick.size(), quick) == 0) {
            cursor = i;
            EnsureVisible();
            return;
        }
    }
}

void Panel::ToggleSelect() {
    if (sizeMode || !snapshot || snapshot->empty()) return;
    const DirEntry& e = (*snapshot)[cursor];
    if (e.name == L"..") return;
    auto it = selected.find(e.name);
    if (it == selected.end()) selected.insert(e.name);
    else selected.erase(it);
    // Курсор остаётся на элементе — выделение всегда «привязано» к курсору,
    // и вторая подсвеченная строка не путает пользователя.
}

void Panel::SelectMask(const std::wstring& mask, bool select) {
    if (sizeMode || !snapshot) return;
    std::vector<std::wstring> masks;
    SplitMasks(mask, masks);
    for (auto& e : *snapshot) {
        if (e.name == L"..") continue;
        if (!MatchAnyMask(masks, e.name)) continue;
        if (select) selected.insert(e.name);
        else selected.erase(e.name);
    }
}

void Panel::SelectAll(bool sel) {
    if (sizeMode || !snapshot) return;
    for (auto& e : *snapshot) {
        if (e.name == L"..") continue;
        if (sel) selected.insert(e.name);
        else selected.erase(e.name);
    }
}

void Panel::InvertSel() {
    if (sizeMode || !snapshot) return;
    for (auto& e : *snapshot) {
        if (e.name == L"..") continue;
        auto it = selected.find(e.name);
        if (it == selected.end()) selected.insert(e.name);
        else selected.erase(it);
    }
}

int Panel::SelectionCount() const {
    if (sizeMode || !snapshot) return 0;
    int n = 0;
    for (auto& e : *snapshot)
        if (e.name != L".." && selected.count(e.name)) n++;
    return n;
}

uint64_t Panel::SelectionBytes() const {
    if (sizeMode || !snapshot) return 0;
    uint64_t total = 0;
    for (auto& e : *snapshot)
        if (e.name != L".." && !e.isDir && selected.count(e.name)) total += e.size;
    return total;
}

std::vector<std::wstring> Panel::SelectedPaths(bool includeCursorIfNone) const {
    std::vector<std::wstring> out;
    if (sizeMode || !snapshot) return out;
    auto collectSelection = [&]() {
        for (auto& e : *snapshot) {
            if (e.name == L"..") continue;
            if (selected.count(e.name)) out.push_back(PathJoin(path, e.name));
        }
    };
    if (SelectionCount() > 0) {
        // Если курсор внутри выделения (или на "..") — действуем на всё выделение,
        // иначе — только на файл под курсором (интуитивное поведение).
        if (cursor >= 0 && cursor < (int)snapshot->size()) {
            const DirEntry& cur = (*snapshot)[cursor];
            if (cur.name == L".." || selected.count(cur.name)) {
                collectSelection();
                return out;
            }
            out.push_back(PathJoin(path, cur.name));
            return out;
        }
        collectSelection();
        return out;
    }
    if (includeCursorIfNone && cursor >= 0 && cursor < (int)snapshot->size()) {
        const DirEntry& e = (*snapshot)[cursor];
        if (e.name != L"..") out.push_back(PathJoin(path, e.name));
    }
    return out;
}

bool Panel::JumpToPrefix(const std::wstring& prefix) {
    if (sizeMode || !snapshot || prefix.empty()) return false;
    for (int i = 0; i < (int)snapshot->size(); i++) {
        const DirEntry& e = (*snapshot)[i];
        if (e.name == L"..") continue;
        if (e.name.size() < prefix.size()) continue;
        bool ok = true;
        for (size_t j = 0; j < prefix.size(); j++) {
            if (LowerChar(e.name[j]) != LowerChar(prefix[j])) { ok = false; break; }
        }
        if (ok) {
            cursor = i;
            EnsureVisible();
            return true;
        }
    }
    return false;
}

bool Panel::OpenByName(const std::wstring& name) {
    if (sizeMode || !snapshot) return false;
    for (int i = 0; i < (int)snapshot->size(); i++) {
        const DirEntry& e = (*snapshot)[i];
        if (e.name == L"..") continue;
        if (WcsICmp(e.name.c_str(), name.c_str()) == 0) {
            cursor = i;
            EnsureVisible();
            EnterCurrent();
            return true;
        }
    }
    return false;
}

// Точное имя или префикс (как в живом поиске): открывает первый подходящий элемент.
// Именно так Enter после набора «dum» открывает «dumps», а не выполняет команду.
bool Panel::OpenByPrefixOrExact(const std::wstring& text) {
    if (sizeMode || !snapshot || text.empty()) return false;
    for (int i = 0; i < (int)snapshot->size(); i++) {
        const DirEntry& e = (*snapshot)[i];
        if (e.name == L"..") continue;
        if (WcsICmp(e.name.c_str(), text.c_str()) == 0) {
            cursor = i;
            EnsureVisible();
            EnterCurrent();
            return true;
        }
    }
    for (int i = 0; i < (int)snapshot->size(); i++) {
        const DirEntry& e = (*snapshot)[i];
        if (e.name == L"..") continue;
        if (e.name.size() < text.size()) continue;
        bool ok = true;
        for (size_t j = 0; j < text.size(); j++) {
            if (LowerChar(e.name[j]) != LowerChar(text[j])) { ok = false; break; }
        }
        if (ok) {
            cursor = i;
            EnsureVisible();
            EnterCurrent();
            return true;
        }
    }
    return false;
}

void Panel::GoUp() {
    if (sizeMode) {
        // в режиме размеров ←/Backspace поднимается по дереву размеров
        std::wstring parent = PathParent(sizeRoot);
        if (parent == sizeRoot) ExitSizeMode();   // выше корня диска некуда
        else EnterSizeMode(parent);
        return;
    }
    std::wstring parent = PathParent(path);
    if (parent != path) {
        // ←/Backspace: курсор остаётся на каталоге, из которого пришли,
        // а не прыгает в начало списка (как в VC/DC)
        pendingReveal = PathFileName(path);
        SetPath(parent);
    }
}

void Panel::GoRoot() {
    SetPath(RootOf(path));
}

void Panel::EnterCurrent() {
    if (sizeMode) {
        // в режиме размеров Enter: каталог — внутрь, файл — открыть
        if (cursor < 0 || cursor >= (int)sizeItems.size()) return;
        const SizeItem& it = sizeItems[cursor];
        if (it.isDir) {
            EnterSizeMode(PathJoin(sizeRoot, it.name));
            return;
        }
        std::wstring full = PathJoin(sizeRoot, it.name);
        HINSTANCE r = ShellExecuteW(hwnd, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)r <= 32 && app)
            app->SetStatusText(L"Не удалось открыть: " + it.name);
        return;
    }
    if (!snapshot || snapshot->empty()) return;
    const DirEntry& e = (*snapshot)[cursor];
    if (e.name == L"..") { GoUp(); return; }
    std::wstring full = PathJoin(path, e.name);
    if (e.isDir) { SetPath(full); return; }
    HINSTANCE r = ShellExecuteW(hwnd, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32 && app)
        app->SetStatusText(L"Не удалось открыть: " + e.name);
}

void Panel::RevealFile(const std::wstring& fullPath) {
    if (sizeMode) return;
    std::wstring dir = PathParent(fullPath);
    std::wstring name = PathFileName(fullPath);
    if (PathNormalize(dir) == PathNormalize(path)) {
        if (snapshot) {
            for (int i = 0; i < (int)snapshot->size(); i++)
                if ((*snapshot)[i].name == name) { cursor = i; EnsureVisible(); return; }
        }
        pendingReveal = name;
        Refresh();
    } else {
        pendingReveal = name;
        SetPath(dir);
    }
}

void Panel::Draw(Renderer& r, const D2D1_RECT_F& area, bool active) {
    const Palette& pal = GetPalette(app->Theme());
    float rowH = r.RowHeight();
    float headerH = rowH + 4;
    float statusH = rowH + 2;
    float rowsAreaH = area.bottom - area.top - headerH - statusH;
    rowsVisible = (int)std::max(0.0f, std::floor(rowsAreaH / rowH));

    // Быстрый просмотр (Ctrl+Q) занимает пассивную панель (если она не в «Размерах»)
    if (!active && !sizeMode && app->quickView) {
        app->DrawQuickView(r, area);
        return;
    }

    if (sizeMode) {
        // ── вид размеров (WizTree) ──
        r.Fill({ area.left, area.top, area.right, area.top + headerH },
               active ? pal.headerActive : pal.headerInactive);
        r.Text(L"Размеры: " + sizeRoot, { area.left + 6, area.top, area.right - 6, area.top + headerH },
               active ? pal.headerText : pal.headerTextInactive, false, true, true);
        r.Fill({ area.left, area.top + headerH, area.right, area.top + headerH + rowsAreaH }, pal.panelBg);
        if (sizeLoading) {
            // живой индикатор: точки + прошедшее время (скан идёт в фоне)
            int dots = (int)((GetTickCount64() / 250) % 4);
            std::wstring txt = L"Считаем размеры";
            txt += std::wstring((size_t)dots, L'.');
            txt += L"  (" + std::to_wstring((GetTickCount64() - sizeStart) / 1000) + L" с)";
            r.Text(txt, { area.left + 24, area.top + headerH, area.right,
                          area.top + headerH + rowH }, pal.dimText, false, true, false);
            return;
        }
        int n = (int)sizeItems.size();
        if (n == 0) return;
        if (top > n - 1) top = n - 1;
        if (top < 0) top = 0;
        uint64_t maxSize = sizeItems[0].size;   // список отсортирован по убыванию
        float y = area.top + headerH;
        for (int i = top; i < n && i < top + rowsVisible; i++) {
            const SizeItem& it = sizeItems[i];
            bool cur = i == cursor;
            D2D1_RECT_F row = { area.left, y, area.right, y + rowH };
            if (cur) r.Fill(row, pal.cursorBg);
            D2D1_COLOR_F tc = cur ? pal.selText : pal.text;
            // тёмная полоса размера на всю высоту строки (выделяется на фоне)
            float barX = area.left + 24;
            float barMaxW = (area.right - area.left) * 0.55f;
            float bw = maxSize ? (float)((double)it.size / (double)maxSize) * barMaxW : 0;
            if (bw > 1.0f)
                r.Fill({ barX, y + 1, barX + bw, y + rowH - 1 }, pal.sizeBar);
            r.Text(it.name, { barX + 6, y, area.right - 150, y + rowH }, tc, false, true, true);
            std::wstring sz = FormatSize(it.size);
            if (it.isDir && it.files) sz += L" · " + std::to_wstring(it.files) + L" ф.";
            r.Text(sz, { area.right - 140, y, area.right - 4, y + rowH }, tc, true, true, true);
            y += rowH;
        }
        float sy = area.top + headerH + rowsAreaH;
        r.Fill({ area.left, sy, area.right, area.bottom }, pal.statusBg);
        uint64_t total = 0;
        for (auto& it : sizeItems) total += it.size;
        std::wstring st = L"элементов: " + std::to_wstring(n) + L" · суммарно " +
                          FormatSize(total) +
                          L" · Enter — внутрь/открыть · F3 — просмотр · Del — удалить · "
                          L"←/Backspace — назад · Esc — выход";
        r.Text(st, { area.left + 6, sy, area.right - 6, area.bottom }, pal.statusText,
               false, true, true);
        return;
    }

    r.Fill({ area.left, area.top, area.right, area.top + headerH },
           active ? pal.headerActive : pal.headerInactive);
    // хлебные крошки пути (как в Проводнике)
    segs.clear();
    {
        std::vector<std::pair<std::wstring, std::wstring>> crumbs;
        std::wstring rest = path;
        if (rest.size() >= 3 && rest[1] == L':') {
            crumbs.push_back({ rest.substr(0, 2), rest.substr(0, 3) });   // "C:" → "C:\"
            rest = rest.substr(3);
        } else if (rest.size() >= 2 && rest[0] == L'\\' && rest[1] == L'\\') {
            size_t p = rest.find(L'\\', 2);
            if (p != std::wstring::npos) {
                crumbs.push_back({ rest.substr(0, p + 1), rest.substr(0, p + 1) });
                rest = rest.substr(p + 1);
            }
        }
        while (!rest.empty()) {
            size_t p = rest.find(L'\\');
            std::wstring part = p == std::wstring::npos ? rest : rest.substr(0, p);
            rest = p == std::wstring::npos ? L"" : rest.substr(p + 1);
            if (part.empty()) continue;
            std::wstring pref = crumbs.empty() ? part : PathJoin(crumbs.back().second, part);
            crumbs.push_back({ part, pref });
        }
        struct C {
            std::wstring label, prefix;
            float w;
        };
        std::vector<C> cs;
        float total = 0;
        for (auto& c : crumbs) {
            float w = r.Measure(c.first).width + 22;
            cs.push_back({ c.first, c.second, w });
            total += w;
        }
        float avail = area.right - area.left - 14;
        std::vector<C> vis;
        if (cs.empty()) {
            vis.push_back({ L"?", path, 30 });
        } else if (total <= avail) {
            vis = cs;
        } else {
            // корень + «…» + хвост, помещающийся справа налево
            vis.push_back(cs[0]);
            float used = cs[0].w + 24;
            std::vector<C> tail;
            size_t i = cs.size();
            while (i-- > 1) {
                if (used + cs[i].w > avail) break;
                tail.insert(tail.begin(), cs[i]);
                used += cs[i].w;
            }
            if (tail.empty() && cs.size() > 1) tail.push_back(cs.back());
            std::wstring ellPref =
                tail.empty() ? cs[0].prefix : PathParent(tail.front().prefix);
            vis.push_back({ L"…", ellPref, 24 });
            for (auto& t : tail) vis.push_back(t);
        }
        float cx = area.left + 6;
        for (auto& v : vis) {
            segs.push_back({ { cx, area.top, cx + v.w - 10, area.top + headerH },
                             v.prefix, false });
            cx += v.w - 10;
            segs.push_back({ { cx, area.top, cx + 10, area.top + headerH },
                             v.prefix, true });
            cx += 10;
        }
        D2D1_COLOR_F tc = active ? pal.headerText : pal.headerTextInactive;
        for (size_t i = 0; i < segs.size(); i++) {
            Seg& s = segs[i];
            if (s.gap) {
                if (i + 1 < segs.size())
                    r.Text(L"›", s.rc, tc, false, true, false);
            } else {
                const std::wstring& label = vis[i / 2].label;
                bool hov = (int)i == hoverSeg;
                if (hov) {
                    r.RoundRect({ s.rc.left - 2, s.rc.top + 2, s.rc.right + 2,
                                  s.rc.bottom - 2 }, 4, pal.selBg);
                    r.Text(label, s.rc, pal.selText, false, true, true);
                } else {
                    r.Text(label, s.rc, tc, false, true, true);
                }
            }
        }
    }
    // фон области списка
    r.Fill({ area.left, area.top + headerH, area.right, area.top + headerH + rowsAreaH }, pal.panelBg);

    if (loading && !snapshot) {
        r.Text(L"Чтение каталога…", { area.left + 24, area.top + headerH, area.right, area.top + headerH + rowH },
               pal.dimText, false, true, false);
        return;
    }
    if (!snapshot) return;

    int n = (int)snapshot->size();
    if (n == 0) return;
    if (top > n - 1) top = n - 1;
    if (top < 0) top = 0;

    float xName = area.left + 26;
    float xExtStart = area.right - 200;
    float xSizeStart = area.right - 160;
    float xDateStart = area.right - 96;
    if (xExtStart < xName + 40) {
        xExtStart = area.right - 60;
        xSizeStart = area.right;
        xDateStart = area.right;
    }

    float y = area.top + headerH;
    for (int i = top; i < n && i < top + rowsVisible; i++) {
        const DirEntry& e = (*snapshot)[i];
        bool sel = selected.count(e.name) != 0;
        bool cur = i == cursor;
        D2D1_RECT_F row = { area.left, y, area.right, y + rowH };
        if (cur) r.Fill(row, pal.cursorBg);
        else if (sel) r.Fill(row, pal.selBg);
        // цель перетаскивания правой кнопкой — рамка-подсветка
        if (app && app->dropPanel == index && app->dropRow == i)
            r.Rect({ row.left, row.top + 1, row.right - 1, row.bottom - 1 },
                   pal.accent, 2.0f);
        bool hidden = (e.attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
        bool vivid = app->Theme() == ThemeId::Vivid;
        D2D1_COLOR_F tc;
        if (cur || sel) {
            tc = pal.selText;
        } else {
            IconId id = e.name == L".." ? IconId::Up : IconFor(e);
            tc = vivid ? TypeTextColor(pal, id) : pal.text;
            // скрытые файлы приглушаем: в «Спектре» — смешиваем с фоном,
            // в остальных темах — dimText
            if (hidden)
                tc = vivid ? MixColors(tc, pal.panelBg, 0.45f) : pal.textHidden;
        }

        float iconSize = 16.0f * r.Scale();
        D2D1_RECT_F ir = { area.left + 4, y + (rowH - iconSize) / 2,
                           area.left + 4 + iconSize, y + (rowH - iconSize) / 2 + iconSize };
        DrawIcon(r, e.name == L".." ? IconId::Up : IconFor(e), ir, pal);

        std::wstring stem = e.name, ext;
        if (!e.isDir && e.name != L"..") {
            size_t dot = e.name.rfind(L'.');
            if (dot != std::wstring::npos && dot > 0) {
                stem = e.name.substr(0, dot);
                ext = e.name.substr(dot + 1);
            }
        }
        r.Text(stem, { xName, y, xExtStart - 6, y + rowH }, tc, false, true, true);
        if (!ext.empty()) {
            // в «Спектре» расширение — тот же цвет, слегка приглушённый
            D2D1_COLOR_F ec = vivid ? MixColors(tc, pal.panelBg, 0.35f) : pal.dimText;
            r.Text(ext, { xExtStart, y, xSizeStart - 6, y + rowH }, ec, false, true, true);
        }

        std::wstring sz;
        if (e.isDir && e.name != L"..") sz = L"<каталог>";
        else sz = FormatSize(e.size);
        r.Text(sz, { xSizeStart, y, xDateStart - 6, y + rowH },
               (e.isDir && e.name != L"..") ? pal.dimText : tc, true, true, true);

        std::wstring dt = FormatDate(e.mtime);
        r.Text(dt, { xDateStart, y, area.right - 2, y + rowH },
               (e.isDir && e.name != L"..") ? pal.dimText : tc, true, true, true);
        y += rowH;
    }

    float sy = area.top + headerH + rowsAreaH;
    r.Fill({ area.left, sy, area.right, area.bottom }, pal.statusBg);
    std::wstring st = L"файлов: " + std::to_wstring(n - 1);
    if (!selected.empty())
        st += L", выделено: " + std::to_wstring(SelectionCount()) +
              L" (" + FormatSize(SelectionBytes()) + L")";
    r.Text(st, { area.left + 6, sy, area.right - 6, area.bottom }, pal.statusText, false, true, true);
}
