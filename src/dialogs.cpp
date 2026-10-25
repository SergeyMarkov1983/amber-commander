#include "dialogs.h"
#include "pathutil.h"
#include "ui/panel.h"
#include "ui/window.h"

// ---------- вспомогательные ----------

namespace {

std::vector<std::wstring> SplitText(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t ch : text) {
        if (ch == L'\n') { lines.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty() || (!text.empty() && text.back() == L'\n')) lines.push_back(cur);
    return lines;
}

} // namespace

// ---------- InputDialog ----------

int InputDialog(HWND parent, ThemeId theme, const std::wstring& title,
                const std::wstring& prompt, std::wstring& value) {
    struct D : Dialog {
        std::wstring prompt;
        std::wstring* value = nullptr;
        EditBox edit;
        Btn ok{ L"ОК", {}, 1 }, cancel{ L"Отмена", {}, 2 };
        int focusedBtn = 1;

        void Draw() override {
            const Palette& pal = GetPalette(theme);
            renderer.Text(prompt, { 12, 10, (float)clientW - 12, 34 }, pal.text, false, true, false);
            DrawEdit(renderer, edit, pal);
            DrawBtn(renderer, ok, pal, focusedBtn == 1);
            DrawBtn(renderer, cancel, pal, focusedBtn == 2);
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            if (vk == VK_ESCAPE) { Close(0); return; }
            if (vk == VK_RETURN) { Close(1); return; }
            if (vk == VK_TAB) {
                focusedBtn = focusedBtn == 1 ? 2 : 1;
                edit.focused = false;
                Redraw();
                return;
            }
            if (EditKey(edit, vk)) { Redraw(); return; }
        }
        void OnChar(wchar_t ch) override { EditChar(edit, ch); Redraw(); }
        void OnMouse(UINT msg, int x, int y, WPARAM) override {
            if (msg == WM_MOUSEMOVE) {
                ok.hot = InRect(ok.rc, x, y);
                cancel.hot = InRect(cancel.rc, x, y);
                Redraw();
                return;
            }
            if (msg == WM_LBUTTONDOWN) {
                if (InRect(ok.rc, x, y)) { Close(1); return; }
                if (InRect(cancel.rc, x, y)) { Close(0); return; }
                if (InRect(edit.rc, x, y)) {
                    edit.focused = true;
                    edit.caret = (int)edit.text.size();
                    focusedBtn = 0;
                    Redraw();
                }
            }
        }
    };
    D d;
    d.parent = parent;
    d.theme = theme;
    d.prompt = prompt;
    d.value = &value;
    d.edit.text = value;
    d.edit.caret = (int)value.size();
    d.edit.focused = true;
    d.Create(title, 420, 150);
    d.edit.rc = { 12, 44, 396, 68 };
    d.ok.rc = { 190, 92, 282, 120 };
    d.cancel.rc = { 292, 92, 384, 120 };
    d.SetTimerMs(1, 500);
    int r = d.RunModal();
    if (r == 1) value = d.edit.text;
    return r;
}

// ---------- ConfirmDialog ----------

int ConfirmDialog(HWND parent, ThemeId theme, const std::wstring& title, const std::wstring& text,
                  const std::vector<std::pair<std::wstring, int>>& buttons, int defIdx) {
    struct D : Dialog {
        std::wstring text;
        std::vector<std::pair<std::wstring, int>> buttons;
        std::vector<Btn> btns;
        int focus = 0;

        void Draw() override {
            const Palette& pal = GetPalette(theme);
            auto lines = SplitText(text);
            float y = 14;
            for (auto& ln : lines) {
                renderer.Text(ln, { 16, y, (float)clientW - 16, y + 22 }, pal.text, false, true, false);
                y += 22;
            }
            for (size_t i = 0; i < btns.size(); i++)
                DrawBtn(renderer, btns[i], pal, (int)i == focus);
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            if (vk == VK_ESCAPE) { Close(0); return; }
            if (vk == VK_TAB || vk == VK_LEFT || vk == VK_RIGHT) {
                focus = (focus + 1) % (int)btns.size();
                Redraw();
                return;
            }
            if (vk == VK_RETURN) { Close(buttons[focus].second); return; }
        }
        void OnMouse(UINT msg, int x, int y, WPARAM) override {
            if (msg == WM_MOUSEMOVE) {
                for (auto& b : btns) b.hot = InRect(b.rc, x, y);
                Redraw();
                return;
            }
            if (msg == WM_LBUTTONDOWN) {
                for (size_t i = 0; i < btns.size(); i++)
                    if (InRect(btns[i].rc, x, y)) { Close(buttons[i].second); return; }
            }
        }
    };
    D d;
    d.parent = parent;
    d.theme = theme;
    d.text = text;
    d.buttons = buttons;
    int lines = (int)std::count(text.begin(), text.end(), L'\n') + 1;
    int bw = 110, gap = 12;
    int totalW = (int)buttons.size() * bw + ((int)buttons.size() - 1) * gap;
    int w = std::max(560, totalW + 40);   // ширина вмещает все кнопки (в т.ч. 5 кнопок «Да/…/Отмена»)
    int h = 130 + lines * 22;
    d.Create(title, w, h);
    int x0 = (w - totalW) / 2;
    int by = h - 46;
    for (size_t i = 0; i < buttons.size(); i++) {
        Btn b;
        b.label = buttons[i].first;
        b.id = buttons[i].second;
        b.rc = { x0 + (int)i * (bw + gap), by, x0 + (int)i * (bw + gap) + bw, by + 28 };
        d.btns.push_back(b);
    }
    if (defIdx < 0 || defIdx >= (int)d.btns.size()) defIdx = 0;
    d.focus = defIdx;
    return d.RunModal();
}

void MessageDialog(HWND parent, ThemeId theme, const std::wstring& title, const std::wstring& text) {
    ConfirmDialog(parent, theme, title, text, { { L"ОК", 1 } }, 0);
}

// ---------- CopyDialog ----------

int CopyDialog(HWND parent, ThemeId theme, bool moveDefault, const std::wstring& summary,
               std::wstring& dst, bool& newer) {
    struct D : Dialog {
        std::wstring summary;
        std::wstring* dstOut = nullptr;
        bool moveDefault = false;
        bool* newerOut = nullptr;
        EditBox eDst;
        CheckBox cNewer{ L"только новые/обновлённые" };
        Btn bCopy{ L"Копировать", {}, 1 }, bMove{ L"Переместить", {}, 2 }, bCancel{ L"Отмена", {}, 0 };
        int focus = 1;

        void Draw() override {
            const Palette& pal = GetPalette(theme);
            std::wstring head = (moveDefault ? L"Перемещение: " : L"Копирование: ") + summary;
            renderer.Text(head, { 12, 10, (float)clientW - 12, 32 }, pal.text, false, true, true);
            renderer.Text(L"в:", { 12, 40, 60, 66 }, pal.text, false, true, false);
            DrawEdit(renderer, eDst, pal);
            DrawCheck(renderer, cNewer, pal);
            DrawBtn(renderer, bCopy, pal, focus == 1);
            DrawBtn(renderer, bMove, pal, focus == 2);
            DrawBtn(renderer, bCancel, pal, focus == 0);
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            if (vk == VK_ESCAPE) { Close(0); return; }
            if (vk == VK_RETURN) { Close(focus == 2 ? 2 : 1); return; }
            if (vk == VK_TAB) {
                focus = (focus + 2) % 3;
                eDst.focused = focus == 0;
                Redraw();
                return;
            }
            if (EditKey(eDst, vk)) { Redraw(); return; }
        }
        void OnChar(wchar_t ch) override {
            if (eDst.focused) EditChar(eDst, ch);
            Redraw();
        }
        void OnMouse(UINT msg, int x, int y, WPARAM) override {
            if (msg == WM_LBUTTONDOWN) {
                if (InRect(bCopy.rc, x, y)) { Close(1); return; }
                if (InRect(bMove.rc, x, y)) { Close(2); return; }
                if (InRect(bCancel.rc, x, y)) { Close(0); return; }
                if (InRect(eDst.rc, x, y)) {
                    eDst.focused = true;
                    eDst.caret = (int)eDst.text.size();
                    focus = 0;
                    Redraw();
                } else if (InRect(cNewer.rc, x, y)) {
                    cNewer.checked = !cNewer.checked;
                    Redraw();
                }
            }
        }
    };
    D d;
    d.parent = parent;
    d.theme = theme;
    d.summary = summary;
    d.moveDefault = moveDefault;
    d.dstOut = &dst;
    d.newerOut = &newer;
    d.eDst.text = dst;
    d.eDst.caret = (int)dst.size();
    d.eDst.focused = true;
    d.Create(moveDefault ? L"Перемещение" : L"Копирование", 640, 220);
    d.eDst.rc = { 64, 40, 616, 66 };
    d.cNewer.rc = { 14, 82, 420, 108 };
    d.bCopy.rc = { 268, 148, 378, 178 };
    d.bMove.rc = { 390, 148, 500, 178 };
    d.bCancel.rc = { 512, 148, 616, 178 };
    int r = d.RunModal();
    if (r != 0) {
        dst = d.eDst.text;
        newer = d.cNewer.checked;
    }
    return r;
}

// ---------- DriveDialog (всплывающее меню выбора диска) ----------

int DriveDialog(HWND parent, ThemeId theme, const std::vector<DriveItem>& items, int& selIndex,
                int side) {
    struct D : Dialog {
        const std::vector<DriveItem>* items = nullptr;
        int* selIndex = nullptr;
        ListBox list;
        int pageRows = 0;

        void Draw() override {
            const Palette& pal = GetPalette(theme);
            renderer.Text(L"Выбор диска", { 10, 6, (float)clientW - 10, 26 }, pal.dimText,
                          false, true, false);
            DrawList(renderer, list, pal, renderer.RowHeight() + 2);
            renderer.Rect({ 0.5f, 0.5f, (float)clientW - 0.5f, (float)clientH - 0.5f },
                          pal.border, 1.0f);
            renderer.Text(L"↑↓ Enter — выбор · Esc — отмена", { 10, (float)clientH - 24,
                          (float)clientW - 10, (float)clientH - 6 }, pal.dimText,
                          false, true, false);
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            if (vk == VK_ESCAPE) { Close(0); return; }
            if (vk == VK_RETURN) {
                if (list.sel >= 0 && list.sel < (int)items->size()) {
                    *selIndex = list.sel;
                    Close(1);
                }
                return;
            }
            if (ListKey(list, vk, std::max(1, pageRows))) { Redraw(); return; }
        }
        void OnMouse(UINT msg, int x, int y, WPARAM) override {
            if (msg == WM_LBUTTONDOWN) {
                if (InRect(list.rc, x, y)) {
                    if (ListClick(list, x, y, renderer.RowHeight() + 2)) Redraw();
                }
            } else if (msg == WM_LBUTTONDBLCLK) {
                if (InRect(list.rc, x, y)) OnKey(VK_RETURN, 0, false);
            }
        }
    };
    D d;
    d.parent = parent;
    d.theme = theme;
    d.items = &items;
    d.selIndex = &selIndex;
    d.closeOnDeactivate = true;   // закрытие при клике мимо (как в DC)
    for (auto& v : items) {
        std::wstring line;
        if (v.isCloud) {
            line = L"☁  " + v.label;
        } else {
            line = v.label;
            switch (v.type) {
            case DRIVE_REMOVABLE: line += L"   флешка"; break;
            case DRIVE_REMOTE: line += L"   сеть"; break;
            case DRIVE_CDROM: line += L"   CD"; break;
            default: break;
            }
            if (!v.ready) line += L"   — нет носителя";
        }
        if (v.total > 0)
            line += L"    " + FormatSize(v.free) + L" / " + FormatSize(v.total);
        d.list.items.push_back(line);
    }
    int w = 440, h = std::min(360, 56 + (int)d.list.items.size() * 23);
    // позиция: под линейкой дисков, над нужной панелью
    RECT pr{};
    GetWindowRect(parent, &pr);
    int x = pr.left + 8;
    if (side == 1) x = pr.left + (pr.right - pr.left) / 2 + 8;
    int y = pr.top + 92;
    if (!d.CreatePopup(x, y, w, h)) return 0;
    d.list.rc = { 8, 30, w - 8, h - 26 };
    d.pageRows = (int)((h - 56) / (d.renderer.RowHeight() + 2));
    d.list.sel = selIndex;
    if (d.list.sel >= (int)d.list.items.size()) d.list.sel = 0;
    return d.RunModal();
}

// ---------- SearchParamsDlg (немодальные параметры поиска) ----------

EditBox* SearchParamsDlg::FocusEdit() {
    if (focusIdx == 1) return &eText;
    if (focusIdx == 2) return &eRoot;
    return &eName;
}

void SearchParamsDlg::Refocus() {
    eName.focused = focusIdx == 0;
    eText.focused = focusIdx == 1;
    eRoot.focused = focusIdx == 2;
    EditBox* e = FocusEdit();
    e->caret = (int)e->text.size();
    Redraw();
}

void SearchParamsDlg::StartSearch() {
    if (!app) return;
    SearchQuery q;
    q.root = eRoot.text;
    q.masks = eName.text;
    q.text = eText.text;
    q.subdirs = cSub.checked;
    q.caseSensitive = cCase.checked;
    q.matchFiles = cFiles.checked;
    q.matchDirs = cDirs.checked;
    app->StartSearchWithParams(q);
}

void SearchParamsDlg::Draw() {
    const Palette& pal = GetPalette(theme);
    renderer.Text(L"Имя (фрагмент или маска):", { 12, 12, 210, 34 }, pal.text, false, true, false);
    renderer.Text(L"Содержащие текст:", { 12, 46, 210, 68 }, pal.text, false, true, false);
    renderer.Text(L"Искать в:", { 12, 80, 210, 102 }, pal.text, false, true, false);
    DrawEdit(renderer, eName, pal);
    DrawEdit(renderer, eText, pal);
    DrawEdit(renderer, eRoot, pal);
    DrawCheck(renderer, cSub, pal);
    DrawCheck(renderer, cFiles, pal);
    DrawCheck(renderer, cDirs, pal);
    DrawCheck(renderer, cCase, pal);
    renderer.Text(L"«kotor» — имена, содержащие kotor;  «*.txt;kotor» — маски и фрагменты через ;",
                  { 12, 158, (float)clientW - 12, 180 }, pal.dimText, false, true, false);
    DrawBtn(renderer, bSearch, pal, true);
    DrawBtn(renderer, bClose, pal, false);
}

void SearchParamsDlg::OnKey(WPARAM vk, DWORD, bool) {
    if (vk == VK_ESCAPE) {
        if (app) app->paramsDlg = nullptr;
        Close(0);
        return;
    }
    if (vk == VK_RETURN) { StartSearch(); return; }
    if (vk == VK_TAB) {
        focusIdx = (focusIdx + 1) % 3;
        Refocus();
        return;
    }
    EditBox* e = FocusEdit();
    if (EditKey(*e, vk)) { Redraw(); return; }
}

void SearchParamsDlg::OnChar(wchar_t ch) {
    EditChar(*FocusEdit(), ch);
    Redraw();
}

void SearchParamsDlg::OnMouse(UINT msg, int x, int y, WPARAM) {
    if (msg != WM_LBUTTONDOWN) return;
    if (InRect(bSearch.rc, x, y)) { StartSearch(); return; }
    if (InRect(bClose.rc, x, y)) {
        if (app) app->paramsDlg = nullptr;
        Close(0);
        return;
    }
    if (InRect(eName.rc, x, y)) { focusIdx = 0; Refocus(); return; }
    if (InRect(eText.rc, x, y)) { focusIdx = 1; Refocus(); return; }
    if (InRect(eRoot.rc, x, y)) { focusIdx = 2; Refocus(); return; }
    if (InRect(cSub.rc, x, y)) { cSub.checked = !cSub.checked; Redraw(); return; }
    if (InRect(cFiles.rc, x, y)) { cFiles.checked = !cFiles.checked; Redraw(); return; }
    if (InRect(cDirs.rc, x, y)) { cDirs.checked = !cDirs.checked; Redraw(); return; }
    if (InRect(cCase.rc, x, y)) { cCase.checked = !cCase.checked; Redraw(); return; }
}

// ---------- ProgDlg ----------

void ProgDlg::Draw() {
    const Palette& pal = GetPalette(theme);
    CopyProgress p;
    {
        std::lock_guard<std::mutex> g(snap->m);
        p = snap->p;
    }
    std::wstring cur = p.current.empty() ? L"Подготовка…" : p.current;
    renderer.Text(cur, { 14, 10, (float)clientW - 14, 32 }, pal.text, false, true, true);
    std::wstring stat = FormatSize(p.bytesDone) + L" / " + FormatSize(p.bytesTotal) +
                        L"    файлов: " + std::to_wstring(p.filesDone) + L" / " +
                        std::to_wstring(p.filesTotal);
    wchar_t sp[64] = L"";
    if (p.speedBps >= 1048576.0) std::swprintf(sp, 64, L"    %.1f МБ/с", p.speedBps / 1048576.0);
    else if (p.speedBps >= 1024.0) std::swprintf(sp, 64, L"    %.0f КБ/с", p.speedBps / 1024.0);
    stat += sp;
    if (p.state == 1) stat += L"    — пауза —";
    renderer.Text(stat, { 14, 38, (float)clientW - 14, 60 }, pal.dimText, false, true, false);
    bar.value = p.bytesTotal ? double(p.bytesDone) / double(p.bytesTotal) : 0.0;
    DrawProgress(renderer, bar, pal);
    DrawBtn(renderer, bPause, pal, false);
    DrawBtn(renderer, bCancel, pal, false);
}

void ProgDlg::OnKey(WPARAM vk, DWORD, bool) {
    if (vk == VK_ESCAPE && cancel) cancel->store(true);
}

void ProgDlg::OnMouse(UINT msg, int x, int y, WPARAM) {
    if (msg == WM_LBUTTONDOWN) {
        if (InRect(bPause.rc, x, y) && pause) {
            pause->store(!pause->load());
            bPause.label = pause->load() ? L"Продолжить" : L"Пауза";
            Redraw();
            return;
        }
        if (InRect(bCancel.rc, x, y) && cancel) {
            cancel->store(true);
            bCancel.label = L"Отмена…";
            Redraw();
        }
    }
}

void ProgDlg::OnTimer(UINT) {
    CopyProgress p;
    {
        std::lock_guard<std::mutex> g(snap->m);
        p = snap->p;
    }
    if (p.state >= 2) {
        Close(0);
        return;
    }
    Redraw();
}

// ---------- SearchResultsDlg ----------

void SearchResultsDlg::Draw() {
    const Palette& pal = GetPalette(theme);
    std::wstring q = L"Маска: " + (query.masks.empty() ? L"*" : query.masks);
    if (!query.text.empty()) q += L"    Текст: " + query.text;
    renderer.Text(q, { 12, 8, (float)clientW - 12, 30 }, pal.text, false, true, true);
    {
        std::lock_guard<std::mutex> g(m);
        DrawList(renderer, list, pal, renderer.RowHeight() + 2);
    }
    size_t count;
    bool fin;
    {
        std::lock_guard<std::mutex> g(m);
        count = hits.size();
        fin = finished;
    }
    std::wstring st = L"Найдено: " + std::to_wstring(count) + (fin ? L"" : L"   (поиск…)") +
                      L"    Enter — перейти, F5 — копировать, F8 — удалить";
    renderer.Text(st, { 12, (float)clientH - 56, (float)clientW - 12, (float)clientH - 30 },
                  pal.dimText, false, true, false);
    DrawBtn(renderer, bClose, pal, false);
}

void SearchResultsDlg::OnKey(WPARAM vk, DWORD, bool) {
    if (vk == VK_ESCAPE) {
        cancel->store(true);
        if (app) app->resultsDlg.reset();
        Close(0);
        return;
    }
    if (vk == VK_RETURN || vk == VK_F5 || vk == VK_F8 || vk == VK_DELETE) {
        std::wstring p;
        {
            std::lock_guard<std::mutex> g(m);
            if (list.sel < 0 || list.sel >= (int)hits.size()) return;
            p = hits[list.sel];
        }
        if (vk == VK_RETURN) {
            if (app) {
                app->ActivePanel()->RevealFile(p);
                app->Redraw();
            }
            if (app && app->win) SetForegroundWindow(app->win->Hwnd());
        } else if (vk == VK_F5) {
            if (app) StartCopy(app, { p }, app->PassivePanel()->path, false, false);
        } else {
            auto errs = DeleteToRecycleBin({ p });
            if (errs.empty()) {
                std::lock_guard<std::mutex> g(m);
                hits.erase(hits.begin() + list.sel);
                if (list.sel >= (int)hits.size()) list.sel = (int)hits.size() - 1;
                if (list.top > list.sel) list.top = list.sel;   // прокрутка не за пределами списка
            } else if (app) {
                app->SetStatusText(errs[0]);
            }
        }
        Redraw();
        return;
    }
    if (ListKey(list, vk, ListVisibleRows(list, renderer.RowHeight() + 2))) {
        Redraw();
        return;
    }
}

void SearchResultsDlg::OnMouse(UINT msg, int x, int y, WPARAM) {
    if (msg == WM_LBUTTONDOWN) {
        if (InRect(bClose.rc, x, y)) {
            cancel->store(true);
            if (app) app->resultsDlg.reset();
            Close(0);
            return;
        }
        if (InRect(list.rc, x, y)) {
            if (ListClick(list, x, y, renderer.RowHeight() + 2)) Redraw();
        }
    } else if (msg == WM_LBUTTONDBLCLK) {
        if (InRect(list.rc, x, y)) OnKey(VK_RETURN, 0, false);
    }
}

void SearchResultsDlg::OnTimer(UINT) {
    bool fin;
    {
        std::lock_guard<std::mutex> g(m);
        fin = finished;
    }
    if (dirty) Rebuild();
    if (fin) {
        size_t c;
        {
            std::lock_guard<std::mutex> g(m);
            c = hits.size();
        }
        SetWindowTextW(hwnd, (title + L" — найдено: " + std::to_wstring(c)).c_str());
    }
    Redraw();
}

LRESULT SearchResultsDlg::OnMessage(UINT mm, WPARAM w, LPARAM l) {
    if (mm == WM_APP_SEARCHHIT) {
        dirty = true;
        return 0;
    }
    if (mm == WM_APP_SEARCHDONE) {
        {
            std::lock_guard<std::mutex> g(m);
            finished = true;
        }
        dirty = true;
        return 0;
    }
    return Dialog::OnMessage(mm, w, l);
}

void SearchResultsDlg::AddHit(const std::wstring& p) {
    {
        std::lock_guard<std::mutex> g(m);
        hits.push_back(p);
    }
    ULONGLONG now = GetTickCount64();
    if (now - lastPost > 100) {
        lastPost = now;
        if (hwnd) PostMessageW(hwnd, WM_APP_SEARCHHIT, 0, 0);
    }
}

void SearchResultsDlg::Rebuild() {
    std::lock_guard<std::mutex> g(m);
    dirty = false;
    if (list.sel < 0 && !hits.empty()) list.sel = 0;
}

// ---------- SubdirPopupDialog (хлебные крошки: список подпапок) ----------

std::wstring SubdirPopupDialog(HWND parent, ThemeId theme, int x, int y,
                               const std::wstring& dir) {
    struct D : Dialog {
        std::vector<std::wstring> names;   // полные пути
        std::wstring result;
        ListBox list;
        void Draw() override {
            const Palette& pal = GetPalette(theme);
            DrawList(renderer, list, pal, renderer.RowHeight() + 2);
            renderer.Rect({ 0.5f, 0.5f, (float)clientW - 0.5f, (float)clientH - 0.5f },
                          pal.border, 1.0f);
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            if (vk == VK_ESCAPE) {
                Close(0);
                return;
            }
            if (vk == VK_RETURN) {
                if (list.sel >= 0 && list.sel < (int)names.size()) {
                    result = names[list.sel];
                    Close(1);
                }
                return;
            }
            if (ListKey(list, vk, ListVisibleRows(list, renderer.RowHeight() + 2))) {
                Redraw();
                return;
            }
        }
        void OnMouse(UINT msg, int x2, int y2, WPARAM) override {
            if (msg == WM_LBUTTONDOWN) {
                if (InRect(list.rc, x2, y2) &&
                    ListClick(list, x2, y2, renderer.RowHeight() + 2))
                    Redraw();
            } else if (msg == WM_LBUTTONDBLCLK) {
                if (InRect(list.rc, x2, y2)) OnKey(VK_RETURN, 0, false);
            }
        }
    };
    D d;
    d.parent = parent;
    d.theme = theme;
    d.closeOnDeactivate = true;   // закрытие при клике мимо
    auto items = EnumerateDir(dir, nullptr);
    for (auto& e : items) {
        if (!e.isDir || e.name == L"..") continue;
        d.list.items.push_back(e.name);
        d.names.push_back(PathJoin(dir, e.name));
    }
    if (d.list.items.empty()) return L"";
    int h = std::min(320, 34 + (int)d.list.items.size() * 23);
    int w = 360;
    if (!d.CreatePopup(x, y, w, h)) return L"";
    d.list.rc = { 8, 6, w - 8, h - 6 };
    if (d.RunModal() == 1) return d.result;
    return L"";
}

// ---------- MenuDialog (F9 / F2) ----------

namespace {
// Перевод в верхний регистр с учётом кириллицы (не зависит от локали).
wchar_t UpLetter(wchar_t c) {
    if (c >= L'а' && c <= L'я') return (wchar_t)(c - 32);
    if (c == L'ё') return L'Ё';
    return towupper(c);
}
} // namespace

int MenuDialog(HWND parent, ThemeId theme, const std::wstring& title,
               const std::vector<MenuItem>& items) {
    struct D : Dialog {
        std::vector<MenuItem> items;
        std::vector<int> idx;  // порядковый номер строки -> индекс в items
        int sel = 0;           // индекс в idx
        int hover = -1;        // строка под курсором мыши

        float RowH() const { return renderer.RowHeight() + 3; }

        void Draw() override {
            const Palette& pal = GetPalette(theme);
            float rowH = RowH();
            for (int k = 0; k < (int)idx.size(); k++) {
                int i = idx[k];
                float y = 8 + (float)k * rowH;
                if (items[i].header) {
                    renderer.Line(12, y + rowH - 3, (float)clientW - 12, y + rowH - 3, pal.border);
                    renderer.Text(items[i].text, { 16, y, (float)clientW - 16, y + rowH },
                                  pal.dimText, false, true, false);
                } else {
                    if (k == sel)
                        renderer.Fill({ 10, y, (float)clientW - 10, y + rowH }, pal.selBg);
                    else if (k == hover)
                        renderer.Fill({ 10, y, (float)clientW - 10, y + rowH }, pal.headerInactive);
                    renderer.Text(items[i].text, { 18, y, (float)clientW - 18, y + rowH },
                                  k == sel ? pal.selText : pal.text, false, true, false);
                }
            }
            renderer.Text(L"↑↓ — выбор   Enter — выполнить   буква — пункт   Esc — отмена",
                          { 12, (float)clientH - 28, (float)clientW - 12, (float)clientH - 8 },
                          pal.dimText, false, true, false);
        }
        void MoveSel(int d) {
            int n = (int)idx.size();
            if (n == 0) return;
            int k = sel + d;
            while (k >= 0 && k < n && items[idx[k]].header) k += d;
            if (k < 0 || k >= n) return;
            sel = k;
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            switch (vk) {
            case VK_ESCAPE:
                Close(0);
                return;
            case VK_RETURN:
                if (sel >= 0 && sel < (int)idx.size()) Close(items[idx[sel]].id);
                return;
            case VK_UP: MoveSel(-1); Redraw(); return;
            case VK_DOWN: MoveSel(1); Redraw(); return;
            case VK_HOME:
                for (int k = 0; k < (int)idx.size(); k++)
                    if (!items[idx[k]].header) { sel = k; break; }
                Redraw();
                return;
            case VK_END:
                for (int k = (int)idx.size() - 1; k >= 0; k--)
                    if (!items[idx[k]].header) { sel = k; break; }
                Redraw();
                return;
            case VK_PRIOR: for (int i = 0; i < 10; i++) MoveSel(-1); Redraw(); return;
            case VK_NEXT: for (int i = 0; i < 10; i++) MoveSel(1); Redraw(); return;
            default: return;
            }
        }
        void OnChar(wchar_t ch) override {
            wchar_t c = UpLetter(ch);
            for (auto& it : items) {
                if (it.header || !it.hotkey) continue;
                if (UpLetter(it.hotkey) == c) { Close(it.id); return; }
            }
        }
        void OnMouse(UINT msg, int x, int y, WPARAM) override {
            if (msg == WM_MOUSEMOVE) {
                float rowH = RowH();
                int k = (int)((y - 8) / rowH);
                if (k < 0 || k >= (int)idx.size() || items[idx[k]].header) k = -1;
                if (k != hover) {
                    hover = k;
                    Redraw();
                }
                return;
            }
            if (msg != WM_LBUTTONDOWN && msg != WM_LBUTTONDBLCLK) return;
            float rowH = RowH();
            int k = (int)((y - 8) / rowH);
            if (k < 0 || k >= (int)idx.size()) return;
            int i = idx[k];
            if (items[i].header) return;
            if (msg == WM_LBUTTONDOWN) {
                sel = k;
                Redraw();
            } else {
                Close(items[i].id);
            }
        }
        // Подгоняет окно под содержимое (rowH известна только после Create).
        void Autosize() {
            float rowH = RowH();
            float maxW = 200.0f;
            for (auto& it : items) maxW = std::max(maxW, renderer.Measure(it.text).width);
            maxW = std::max(maxW, renderer.Measure(
                L"↑↓ — выбор   Enter — выполнить   буква — пункт   Esc — отмена").width);
            int w = std::max(300, (int)maxW + 44);
            int h = 8 + (int)((float)idx.size() * rowH) + 32;
            if (w == clientW && h == clientH) return;
            RECT wr{};
            GetWindowRect(hwnd, &wr);
            RECT cr{ 0, 0, w, h };
            DWORD style = GetWindowLongW(hwnd, GWL_STYLE) & (WS_CAPTION | WS_SYSMENU | WS_POPUP);
            AdjustWindowRect(&cr, style, FALSE);
            SetWindowPos(hwnd, nullptr, wr.left, wr.top, cr.right - cr.left, cr.bottom - cr.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            clientW = w;
            clientH = h;
            renderer.Resize((UINT)w, (UINT)h);
        }
    };
    D d;
    d.parent = parent;
    d.theme = theme;
    d.items = items;
    for (int i = 0; i < (int)items.size(); i++) d.idx.push_back(i);
    for (int k = 0; k < (int)d.idx.size(); k++)
        if (!items[d.idx[k]].header) { d.sel = k; break; }
    d.Create(title, 420, 120);
    d.Autosize();
    // центрировать на рабочей области после подгонки размера
    {
        RECT wr{};
        GetWindowRect(d.hwnd, &wr);
        HMONITOR mon = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        RECT wa = mi.rcWork;
        SetWindowPos(d.hwnd, nullptr,
                     wa.left + ((wa.right - wa.left) - (wr.right - wr.left)) / 2,
                     wa.top + ((wa.bottom - wa.top) - (wr.bottom - wr.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return d.RunModal();
}

// ---------- PropsDialog (Alt+Enter) ----------

bool PropsDialog(HWND parent, ThemeId theme, const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(LongPath(path).c_str(), GetFileExInfoStandard, &d)) return false;

    bool isDir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    uint64_t size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    std::wstring name = PathFileName(path);
    std::wstring type;
    if (isDir) {
        type = L"Каталог";
    } else {
        std::wstring ext = PathExtension(name);
        type = ext.empty() ? L"Файл" : L"Файл " + ext.substr(1);
        for (auto& ch : type) ch = UpLetter(ch);
    }
    std::wstring mtime = FormatDate(d.ftLastWriteTime);

    struct SizeCtx {
        std::atomic<bool> cancel{ false };
        std::atomic<bool> done{ false };
        std::atomic<uint64_t> bytes{ 0 };
    };

    struct D : Dialog {
        std::wstring path, name, type, mtime;
        bool isDir = false;
        uint64_t size = 0;
        CheckBox cA{ L"Архивный" }, cR{ L"Только чтение" }, cH{ L"Скрытый" }, cS{ L"Системный" };
        std::shared_ptr<SizeCtx> sc;
        Btn bOk{ L"ОК", {}, 1 }, bCancel{ L"Отмена", {}, 0 };
        bool changed = false;
        int focus = 0;  // 0..3 чекбоксы, 4 ОК, 5 Отмена

        void Apply() {
            DWORD cur = GetFileAttributesW(LongPath(path).c_str());
            if (cur == INVALID_FILE_ATTRIBUTES) return;
            DWORD want = cur;
            auto bit = [&](DWORD b, bool on) { want = on ? (want | b) : (want & ~b); };
            bit(FILE_ATTRIBUTE_ARCHIVE, cA.checked);
            bit(FILE_ATTRIBUTE_READONLY, cR.checked);
            bit(FILE_ATTRIBUTE_HIDDEN, cH.checked);
            bit(FILE_ATTRIBUTE_SYSTEM, cS.checked);
            if (want != cur && SetFileAttributesW(LongPath(path).c_str(), want))
                changed = true;
        }

        void Draw() override {
            const Palette& pal = GetPalette(theme);
            auto row = [&](int i, const wchar_t* label, const std::wstring& val) {
                float y = 12 + (float)i * 26;
                renderer.Text(label, { 14, y, 132, y + 24 }, pal.dimText, false, true, false);
                renderer.Text(val, { 140, y, (float)clientW - 14, y + 24 }, pal.text, false, true, true);
            };
            row(0, L"Имя:", name);
            row(1, L"Полный путь:", path);
            row(2, L"Тип:", type);
            std::wstring sz;
            if (isDir && !sc->done.load()) sz = L"считаю…";
            else sz = FormatSize(isDir ? sc->bytes.load() : size);
            row(3, L"Размер:", sz);
            row(4, L"Изменён:", mtime);
            renderer.Text(L"Атрибуты:", { 14, 156, 132, 180 }, pal.dimText, false, true, false);
            DrawCheck(renderer, cA, pal);
            DrawCheck(renderer, cR, pal);
            DrawCheck(renderer, cH, pal);
            DrawCheck(renderer, cS, pal);
            DrawBtn(renderer, bOk, pal, focus == 4);
            DrawBtn(renderer, bCancel, pal, focus == 5);
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            if (vk == VK_ESCAPE) { Close(0); return; }
            if (vk == VK_RETURN) {
                if (focus <= 3) focus = 4;
                else if (focus == 4) { Apply(); Close(1); }
                else { Close(0); }
                Redraw();
                return;
            }
            if (vk == VK_TAB) { focus = (focus + 1) % 6; Redraw(); return; }
            if (vk == VK_SPACE && focus <= 3) {
                CheckBox* cbs[4] = { &cA, &cR, &cH, &cS };
                cbs[focus]->checked = !cbs[focus]->checked;
                Redraw();
            }
        }
        void OnMouse(UINT msg, int x, int y, WPARAM) override {
            if (msg == WM_MOUSEMOVE) {
                bOk.hot = InRect(bOk.rc, x, y);
                bCancel.hot = InRect(bCancel.rc, x, y);
                Redraw();
                return;
            }
            if (msg == WM_LBUTTONDOWN) {
                if (InRect(bOk.rc, x, y)) { Apply(); Close(1); return; }
                if (InRect(bCancel.rc, x, y)) { Close(0); return; }
                CheckBox* cbs[4] = { &cA, &cR, &cH, &cS };
                for (int i = 0; i < 4; i++)
                    if (InRect(cbs[i]->rc, x, y)) {
                        cbs[i]->checked = !cbs[i]->checked;
                        focus = i;
                        Redraw();
                        return;
                    }
            }
        }
        void OnTimer(UINT) override {
            if (sc && sc->done.load()) {
                KillTimer(hwnd, 1);
                Redraw();
            } else {
                Redraw();
            }
        }
    };
    D dd;
    dd.parent = parent;
    dd.theme = theme;
    dd.path = path;
    dd.name = name;
    dd.type = type;
    dd.mtime = mtime;
    dd.isDir = isDir;
    dd.size = size;
    dd.sc = std::make_shared<SizeCtx>();
    dd.cA.checked = (d.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0;
    dd.cR.checked = (d.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    dd.cH.checked = (d.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    dd.cS.checked = (d.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
    dd.Create(L"Свойства — " + name, 620, 268);
    dd.cA.rc = { 140, 156, 250, 180 };
    dd.cR.rc = { 256, 156, 400, 180 };
    dd.cH.rc = { 406, 156, 500, 180 };
    dd.cS.rc = { 506, 156, 606, 180 };
    dd.bOk.rc = { 360, 224, 460, 252 };
    dd.bCancel.rc = { 472, 224, 606, 252 };
    if (isDir) {
        dd.SetTimerMs(1, 150);
        std::thread([sc = dd.sc, path]() {
            sc->bytes.store(CalcTreeSize(path, &sc->cancel));
            sc->done.store(true);
        }).detach();
    }
    int r = dd.RunModal();
    (void)r;
    dd.sc->cancel.store(true);
    return dd.changed;
}

// ---------- HistoryDialog (Alt+Down / меню) ----------

std::wstring HistoryDialog(HWND parent, ThemeId theme, const std::wstring& currentPath) {
    static std::vector<std::wstring> g_dirs;  // история за сессию (вся программа)
    {
        auto it = std::find(g_dirs.begin(), g_dirs.end(), currentPath);
        if (it != g_dirs.end()) g_dirs.erase(it);
    }
    g_dirs.insert(g_dirs.begin(), currentPath);
    if (g_dirs.size() > 50) g_dirs.resize(50);

    struct D : Dialog {
        std::vector<std::wstring> dirs;
        std::wstring result;
        ListBox list;
        Btn bGo{ L"Перейти", {}, 1 }, bCancel{ L"Отмена", {}, 0 };

        void Draw() override {
            const Palette& pal = GetPalette(theme);
            renderer.Text(L"История каталогов (последние 50):",
                          { 12, 8, (float)clientW - 12, 30 }, pal.text, false, true, false);
            DrawList(renderer, list, pal, renderer.RowHeight() + 2);
            DrawBtn(renderer, bGo, pal, true);
            DrawBtn(renderer, bCancel, pal, false);
        }
        void OnKey(WPARAM vk, DWORD, bool) override {
            if (vk == VK_ESCAPE) { Close(0); return; }
            if (vk == VK_RETURN) {
                if (list.sel >= 0 && list.sel < (int)dirs.size()) {
                    result = dirs[list.sel];
                    Close(1);
                }
                return;
            }
            if (ListKey(list, vk, ListVisibleRows(list, renderer.RowHeight() + 2))) {
                Redraw();
                return;
            }
        }
        void OnMouse(UINT msg, int x, int y, WPARAM) override {
            if (msg == WM_LBUTTONDOWN) {
                if (InRect(bGo.rc, x, y)) { OnKey(VK_RETURN, 0, false); return; }
                if (InRect(bCancel.rc, x, y)) { Close(0); return; }
                if (InRect(list.rc, x, y)) {
                    if (ListClick(list, x, y, renderer.RowHeight() + 2)) Redraw();
                }
            } else if (msg == WM_LBUTTONDBLCLK) {
                if (InRect(list.rc, x, y)) OnKey(VK_RETURN, 0, false);
            }
        }
    };
    D d;
    d.parent = parent;
    d.theme = theme;
    d.dirs = g_dirs;
    d.list.items = g_dirs;
    d.list.sel = g_dirs.size() > 1 ? 1 : 0;
    int h = 140 + (int)std::min<size_t>(g_dirs.size(), 16) * 22;
    d.Create(L"История каталогов", 680, h);
    d.list.rc = { 12, 40, 656, h - 64 };
    d.bGo.rc = { 420, h - 52, 520, h - 24 };
    d.bCancel.rc = { 532, h - 52, 656, h - 24 };
    if (d.RunModal() == 1 && !d.result.empty()) return d.result;
    return std::wstring();
}
