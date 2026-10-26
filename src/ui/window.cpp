#include "window.h"
#include "app.h"
#include "panel.h"
#include "dialog.h"
#include "dialogs.h"
#include "pathutil.h"
#include "shellmenu.h"
#include "core/folderidx.h"
#include <shlobj.h>
#include <dwmapi.h>

static const wchar_t* kMainClass = L"AmberMainWindow";

// Линейная интерполяция цвета (для «едва заметного» серого индикатора).
static D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                        a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

bool MainWindow::Create(App* a) {
    app = a;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    hwnd = CreateWindowExW(0, kMainClass, L"Amber Commander",
                           WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1240, 800,
                           nullptr, nullptr, wc.hInstance, this);
    if (!hwnd) return false;
    // Отключаем собственное рисование рамки/тени DWM (белая рамка при потере
    // фокуса) — рамку фокуса рисуем сами в OnPaint (только когда окно неактивно).
    {
        DWORD policy = 1;   // DWMNCRP_DISABLED
        DwmSetWindowAttribute(hwnd, 2 /*DWMWA_NCRENDERING_POLICY*/, &policy, sizeof(policy));
    }
    // стартовое положение — центр рабочей области монитора
    {
        POINT pt{ 0, 0 };
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        RECT wa = mi.rcWork;
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        if (w > wa.right - wa.left) w = wa.right - wa.left;
        if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
        SetWindowPos(hwnd, nullptr,
                     wa.left + ((wa.right - wa.left) - w) / 2,
                     wa.top + ((wa.bottom - wa.top) - h) / 2,
                     w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (!renderer.Init(hwnd)) return false;
    // пульс для живого индикатора скана «Размеров»
    SetTimer(hwnd, 1, 120, nullptr);
    return true;
}

int MainWindow::TitleButtonAt(int x, int y) {
    if (InRect(btnClose, x, y)) return 2;
    if (InRect(btnMax, x, y)) return 1;
    if (InRect(btnMin, x, y)) return 0;
    return -1;
}

void MainWindow::ToggleMaximize() {
    if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    else ShowWindow(hwnd, SW_MAXIMIZE);
    Redraw();
}

void MainWindow::Layout() {
    RECT rc;
    GetClientRect(hwnd, &rc);
    W = (float)rc.right;
    H = (float)rc.bottom;
    rowH = renderer.RowHeight();
    headerH = rowH + 4;
    titleH = rowH + 8;
    keybarH = rowH + 14;
    cmdH = rowH + 4;
    driveBarH = rowH + 2;
    panH = H - keybarH - cmdH - driveBarH - titleH;
    {
        // разделитель панелей: позиция из сохранённой доли + зажим по краям
        float minW = std::floor(W * 0.2f);
        float maxW = W - minW;
        split = std::floor(W * splitFrac + 0.5f);
        if (split < minW) split = minW;
        if (split > maxW) split = maxW;
        splitFrac = split / W;
    }
    btnMin = { (LONG)W - 3 - 138, 3, (LONG)W - 3 - 92, (LONG)titleH };
    btnMax = { (LONG)W - 3 - 92, 3, (LONG)W - 3 - 46, (LONG)titleH };
    btnClose = { (LONG)W - 3 - 46, 3, (LONG)W - 3, (LONG)titleH };
    // нижняя линейка: 11 кнопок (F1..F10 + F11 Размеры) с отступами
    {
        const int kGap = 4, kMargin = 9, kSizeW = 118;
        float cell = ((W - kMargin * 2 - kGap * 10 - kSizeW) / 10.0f);
        if (cell < 40) cell = 40;
        keyRects.clear();
        float kx = kMargin;
        for (int i = 0; i < 10; i++) {
            keyRects.push_back({ (LONG)kx, (LONG)(H - keybarH + 6), (LONG)(kx + cell), (LONG)H - 6 });
            kx += cell + kGap;
        }
        keyRects.push_back({ (LONG)kx, (LONG)(H - keybarH + 6), (LONG)(kx + kSizeW), (LONG)H - 6 });
    }
    if (!app->panelsHidden) {
        // отступы от краёв окна
        panRect[0] = { 6, (LONG)(titleH + driveBarH + 5), (LONG)split - 4,
                       (LONG)(titleH + driveBarH + panH) };
        panRect[1] = { (LONG)split + 4, (LONG)(titleH + driveBarH + 5), (LONG)W - 6,
                       (LONG)(titleH + driveBarH + panH) };
    } else {
        int side = app->activePanel;
        panRect[side] = { 6, (LONG)(titleH + driveBarH + 5), (LONG)W - 6,
                          (LONG)(titleH + driveBarH + panH) };
        panRect[1 - side] = { 0, 0, 0, 0 };
    }
    // геометрия линейки дисков
    driveRects.clear();
    float x = 7.0f;
    for (auto& d : app->drives) {
        std::wstring label = d.isCloud ? L"☁ " + d.label : d.label;
        float w = renderer.Measure(label).width + 16.0f;
        RECT r = { (LONG)x, (LONG)(titleH + 4), (LONG)(x + w),
                   (LONG)(titleH + 4 + driveBarH) };
        driveRects.push_back(r);
        x += w + 2.0f;
    }
}

void MainWindow::OnPaint(HDC hdc, const RECT& rcPaint) {
    if (!renderer.IsReady()) return;
    Layout();
    const Palette& pal = GetPalette(app->Theme());
    renderer.BeginDraw(hdc, rcPaint, pal.bg);

    // ── Заголовок окна в тёплом стиле ──
    renderer.Fill({ 3, 3, W - 3, titleH }, pal.headerInactive);
    {
        // мини-иконка — буква «А» на тёплой плитке
        renderer.RoundRect({ 10, 7, 32, titleH - 3 }, 5, pal.accent);
        renderer.Text(L"А", { 10, 7, 32, titleH - 3 }, pal.iconHighlight, false, true, false, 700);
    }
    {
        wchar_t buf[512];
        int n = GetWindowTextW(hwnd, buf, 512);
        std::wstring title(buf, (size_t)std::max(0, n));
        float btnX = W - 3 - 138.0f;
        renderer.Text(title, { 30, 0, btnX - 8, titleH }, pal.titleText, false, true, true);
        // справка по горячим клавишам в шапке (если помещается)
        float titleW = renderer.Measure(title).width;
        float free = btnX - 8 - (30 + titleW + 16);
        if (free > 150) {
            std::wstring hint = free > 460
                ? L"←/→ переход · Ctrl+←/→ история · F2 переим · F3/F4 · F5/F6 копир/перем · F7 катал · F8 удал · Alt+F7 поиск"
                : L"F3/F4 · F5/F6 · F7 · F8 · Alt+F7";
            if (free > renderer.Measure(hint).width)
                renderer.Text(hint, { 30 + titleW + 16, 0, btnX - 8, titleH },
                              pal.headerTextInactive, false, true, true);
        }
    }
    // кнопки окна
    {
        renderer.Fill(ToRect(btnMin), hMin ? pal.selBg : pal.headerInactive);
        renderer.Fill(ToRect(btnMax), hMax ? pal.selBg : pal.headerInactive);
        renderer.Fill(ToRect(btnClose), hClose ? pal.error : pal.headerInactive);
        float cym = titleH / 2;
        float cxm = (btnMin.left + btnMin.right) / 2.0f;
        renderer.Line(cxm - 4, cym, cxm + 4, cym, pal.titleText, 1.2f);
        float cx2 = (btnMax.left + btnMax.right) / 2.0f;
        if (IsZoomed(hwnd)) {
            renderer.Rect({ cx2 - 5, cym - 3, cx2 - 1, cym + 2 }, pal.titleText, 1.0f);
            renderer.Rect({ cx2 - 2, cym - 5, cx2 + 4, cym - 1 }, pal.titleText, 1.0f);
        } else {
            renderer.Rect({ cx2 - 4, cym - 4, cx2 + 4, cym + 4 }, pal.titleText, 1.0f);
        }
        float cx3 = (btnClose.left + btnClose.right) / 2.0f;
        D2D1_COLOR_F xsym = hClose ? pal.iconHighlight : pal.titleText;
        renderer.Line(cx3 - 4, cym - 4, cx3 + 4, cym + 4, xsym, 1.4f);
        renderer.Line(cx3 + 4, cym - 4, cx3 - 4, cym + 4, xsym, 1.4f);
    }

    // Линейка дисков (локальные, флешки, сетевые, облака)
    renderer.Fill({ 3, titleH + 3, W - 3, titleH + 3 + driveBarH }, pal.keybarBg);
    {
        std::wstring activeRoot = RootOf(app->ActivePanel()->path);
        for (size_t i = 0; i < driveRects.size() && i < app->drives.size(); i++) {
            DriveItem& d = app->drives[i];
            RECT& rr = driveRects[i];
            std::wstring label = d.isCloud ? L"☁ " + d.label : d.label;
            bool isActive = WcsICmp(d.path.c_str(), activeRoot.c_str()) == 0;
            if (isActive) {
                renderer.RoundRect({ (float)rr.left - 1, (float)rr.top + 1, (float)rr.right - 1,
                                     (float)rr.bottom - 1 }, 5, pal.selBg);
                renderer.Text(label, ToRect(rr), pal.accent, false, true, false, 600);
            } else if ((int)i == hoverDrive) {
                renderer.RoundRect({ (float)rr.left - 1, (float)rr.top + 1, (float)rr.right - 1,
                                     (float)rr.bottom - 1 }, 5, pal.selBg);
                renderer.Text(label, ToRect(rr), pal.keybarText, false, true, false);
            } else {
                renderer.Text(label, ToRect(rr), pal.keybarText, false, true, false);
            }
        }
    }

    if (!app->panelsHidden) {
        app->panels[0]->Draw(renderer, ToRect(panRect[0]), app->activePanel == 0);
        app->panels[1]->Draw(renderer, ToRect(panRect[1]), app->activePanel == 1);
        renderer.Fill({ split - 1, titleH + driveBarH, split + 1, titleH + driveBarH + panH },
                      pal.border);
    } else {
        int side = app->activePanel;
        app->panels[side]->Draw(renderer, ToRect(panRect[side]), true);
    }

    float cy = H - keybarH - cmdH;
    // попап глобального поиска каталогов — над командной строкой
    if (app->qpOpen && !app->qpResults.empty()) {
        float rowH2 = renderer.RowHeight();
        float cy2 = cy;
        int n = (int)app->qpResults.size();
        int rows = std::min(n, 12);
        float w = std::min(W * 0.72f, 1000.0f);
        float x0 = 12.0f;
        float yTop = cy2 - 4.0f - rows * rowH2;
        renderer.Fill({ x0, yTop, x0 + w, cy2 - 4 }, pal.panelBg);
        renderer.Rect({ x0, yTop, x0 + w, cy2 - 4 }, pal.accent, 2.0f);
        for (int i = 0; i < rows; i++) {
            int gi = app->qpTop + i;
            if (gi >= n) break;
            D2D1_RECT_F rr = { x0 + 8, yTop + i * rowH2, x0 + w - 8, yTop + (i + 1) * rowH2 };
            if (gi == app->qpSel) renderer.Fill(rr, pal.selBg);
            renderer.Text(app->qpResults[gi], rr,
                          gi == app->qpSel ? pal.selText : pal.text, false, true, true);
        }
    }
    renderer.Fill({ 3, cy + 3, W - 3, cy + cmdH - 3 }, pal.cmdLineBg);
    std::wstring cl = L"> " + app->cmdLine;
    if ((GetTickCount64() / 500) & 1) cl += L"▌";
    renderer.Text(cl, { 12, cy + 3, W - 12, cy + cmdH - 3 }, pal.cmdLineText, false, true, true);
    if (GetTickCount64() < app->statusUntil) {
        renderer.Text(app->statusText, { W * 0.3f, cy + 3, W * 0.7f, cy + cmdH - 3 }, pal.accent,
                      true, true, false);
    }
    // Едва заметный серый индикатор фоновой индексации глобального поиска
    // («индексация поиска» с бегущими точками); после успешного завершения — на
    // несколько секунд подтверждение «индекс поиска обновлён» тем же стилем.
    {
        D2D1_RECT_F rr = { W - 340.0f, cy + 3, W - 12.0f, cy + cmdH - 3 };
        D2D1_COLOR_F dim = Mix(pal.cmdLineBg, pal.dimText, 0.45f);
        if (FolderIndexBusy()) {
            int dots = 1 + (int)((GetTickCount64() / 400) % 3);
            std::wstring s = L"индексация поиска" + std::wstring((size_t)dots, L'.');
            renderer.Text(s, rr, dim, true, true, true);
        } else if (idxDoneUntil && GetTickCount64() < idxDoneUntil) {
            renderer.Text(L"индекс поиска обновлён", rr, dim, true, true, true);
        }
    }

    float ky = H - keybarH;
    renderer.Fill({ 3, ky + 3, W - 3, H - 3 }, pal.keybarBg);
    {
        // кнопки F1..F10 + F11 «Размеры» — в стиле шапки, с отступами
        static const wchar_t* fkey[] = { L"F1", L"F2", L"F3", L"F4", L"F5", L"F6",
                                         L"F7", L"F8", L"F9", L"F10", L"F11" };
        static const wchar_t* klab[] = { L"Помощь", L"Переим", L"Просм", L"Правка", L"Копир",
                                         L"Перем", L"Катал", L"Удал", L"Меню", L"Выход",
                                         L"Размеры" };
        bool anySize = app->panels[0]->sizeMode || app->panels[1]->sizeMode;
        for (int i = 0; i < 11 && i < (int)keyRects.size(); i++) {
            RECT& rr = keyRects[i];
            bool active = (i == 10 && anySize);
            renderer.Fill(ToRect(rr), (i == hoverKey || active) ? pal.selBg : pal.cmdLineBg);
            renderer.Rect(ToRect(rr), active ? pal.accent : pal.border, active ? 2.0f : 1.0f);
            D2D1_COLOR_F kc = active ? pal.accent : pal.keybarText;
            std::wstring fk = fkey[i];
            float fw = renderer.Measure(fk, 600).width;
            renderer.Text(fk, { (float)rr.left + 8, (float)rr.top, (float)rr.left + 8 + fw,
                                (float)rr.bottom }, kc, false, true, false, 600);
            renderer.Text(klab[i], { (float)rr.left + 10 + fw, (float)rr.top, (float)rr.right - 4,
                                     (float)rr.bottom }, kc, false, true, false);
        }
    }
    // тонкая синяя рамка — только когда окно не в фокусе
    if (!hasFocus)
        renderer.Rect({ 1, 1, W - 1, H - 1 }, Rgb(0, 120, 215), 2.0f);
    renderer.EndDraw();
}

int MainWindow::PanelIndexAt(int x) {
    if (!app->panelsHidden) {
        if (x >= panRect[1].left) return 1;
        if (x < panRect[0].right) return 0;
        return -1;
    }
    return app->activePanel;
}

RECT MainWindow::PanelRect(int idx) { return panRect[idx]; }

void MainWindow::NudgeSplit(int dir) {
    splitFrac += dir * 0.05f;
    if (splitFrac < 0.2f) splitFrac = 0.2f;
    if (splitFrac > 0.8f) splitFrac = 0.8f;
    Redraw();
}

// Копирование строки в буфер обмена (CF_UNICODETEXT)
static void CopyTextToClipboard(HWND h, const std::wstring& s) {
    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        void* p = GlobalLock(g);
        if (p) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(g);
        }
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}

void MainWindow::OnMouse(UINT msg, int x, int y, WPARAM wKey) {
    // Заголовок окна: кнопки + перетаскивание (перетаскивание — через WM_NCHITTEST)
    if (y < (LONG)titleH) {
        if (msg == WM_MOUSEMOVE) {
            bool a = InRect(btnMin, x, y), b = InRect(btnMax, x, y), c = InRect(btnClose, x, y);
            if (a != hMin || b != hMax || c != hClose) {
                hMin = a;
                hMax = b;
                hClose = c;
                Redraw();
            }
        } else if (msg == WM_LBUTTONDOWN) {
            if (InRect(btnClose, x, y)) app->Shutdown();
            else if (InRect(btnMax, x, y)) ToggleMaximize();
            else if (InRect(btnMin, x, y)) ShowWindow(hwnd, SW_MINIMIZE);
        }
        return;
    }
    // Линейка дисков: клик — переход, движение — подсветка
    if (y < (LONG)(titleH + driveBarH)) {
        if (msg == WM_LBUTTONDOWN) {
            for (size_t i = 0; i < driveRects.size() && i < app->drives.size(); i++) {
                if (InRect(driveRects[i], x, y)) {
                    app->GoToDrive(app->activePanel, app->drives[i].path);
                    return;
                }
            }
        } else if (msg == WM_MOUSEMOVE) {
            int hov = -1;
            for (size_t i = 0; i < driveRects.size(); i++)
                if (InRect(driveRects[i], x, y)) { hov = (int)i; break; }
            if (hov != hoverDrive) {
                hoverDrive = hov;
                Redraw();
            }
        }
        return;
    }
    // мышь ушла с линейки дисков — снять «прилипшую» подсветку
    if (msg == WM_MOUSEMOVE && hoverDrive != -1) {
        hoverDrive = -1;
        Redraw();
    }
    if (msg == WM_MOUSEWHEEL) {
        short d = GET_WHEEL_DELTA_WPARAM(wKey);
        int idx = PanelIndexAt(x);
        if (idx < 0) return;
        Panel* p = app->panels[idx];
        p->top += d > 0 ? -3 : 3;
        if (p->top < 0) p->top = 0;
        if (p->Count() > p->rowsVisible && p->top > p->Count() - p->rowsVisible)
            p->top = p->Count() - p->rowsVisible;   // не прокручивать в пустоту
        Redraw();
        return;
    }
    // нижняя линейка клавиш: кнопки F1..F10 + F11 «Размеры»
    if (y >= (LONG)(H - keybarH)) {
        if (msg == WM_MOUSEMOVE) {
            int hov = -1;
            for (int i = 0; i < (int)keyRects.size(); i++)
                if (InRect(keyRects[i], x, y)) { hov = i; break; }
            if (hov != hoverKey) {
                hoverKey = hov;
                Redraw();
            }
        } else if (msg == WM_LBUTTONDOWN) {
            static const Cmd cellCmd[11] = {
                Cmd::Help, Cmd::Rename, Cmd::View, Cmd::Edit, Cmd::Copy,
                Cmd::Move, Cmd::MkDir, Cmd::Delete, Cmd::Menu, Cmd::Quit, Cmd::SizeView
            };
            for (int i = 0; i < (int)keyRects.size() && i < 11; i++) {
                if (InRect(keyRects[i], x, y)) {
                    app->Exec(cellCmd[i]);
                    return;
                }
            }
        }
        return;
    }
    // мышь ушла с линейки клавиш — снять «прилипшую» подсветку
    if (msg == WM_MOUSEMOVE && hoverKey != -1) {
        hoverKey = -1;
        Redraw();
    }
    if (msg == WM_RBUTTONDOWN) {
        // начало возможного перетаскивания правой кнопкой
        int idx = PanelIndexAt(x);
        if (idx < 0) return;
        Panel* p = app->panels[idx];
        if (p->sizeMode || !p->snapshot) return;
        RECT r = PanelRect(idx);
        int row = (int)((y - r.top - headerH) / rowH);
        if (row < 0) return;
        int target = p->top + row;
        if (target < 0 || target >= (int)p->snapshot->size()) return;
        if ((*p->snapshot)[target].name == L"..") return;
        dragPanel = idx;
        dragSourceName = (*p->snapshot)[target].name;
        dragStart = { x, y };
        return;
    }
    if (msg == WM_MOUSEMOVE && (wKey & MK_RBUTTON) && dragPanel >= 0) {
        if (!rbtnDragging) {
            if (std::abs(x - dragStart.x) > 4 || std::abs(y - dragStart.y) > 4) {
                rbtnDragging = true;
                SetCapture(hwnd);
            } else {
                return;
            }
        }
        // подсветка папки-приёмника под курсором
        int np = -1, nr = -1;
        int idx = PanelIndexAt(x);
        if (idx >= 0) {
            Panel* p = app->panels[idx];
            if (!p->sizeMode && p->snapshot) {
                RECT r = PanelRect(idx);
                int row = (int)((y - r.top - headerH) / rowH);
                if (row >= 0) {
                    int t = p->top + row;
                    if (t >= 0 && t < (int)p->snapshot->size()) {
                        const DirEntry& e = (*p->snapshot)[t];
                        if (e.name != L".." && e.isDir) {
                            np = idx;
                            nr = t;
                        }
                    }
                }
            }
        }
        if (np != app->dropPanel || nr != app->dropRow) {
            app->dropPanel = np;
            app->dropRow = nr;
            Redraw();
        }
        return;
    }
    // ховер по хлебным крошкам панелей
    if (msg == WM_MOUSEMOVE) {
        int idx = PanelIndexAt(x);
        int hov = -1;
        if (idx >= 0 && !app->panelsHidden) {
            RECT r = PanelRect(idx);
            if (y >= r.top && y < r.top + (LONG)headerH && !app->panels[idx]->sizeMode)
                hov = app->panels[idx]->HitSeg((float)x, (float)y);
        }
        bool changed = false;
        for (int i = 0; i < 2; i++) {
            int want = (i == idx) ? hov : -1;
            if (app->panels[i]->hoverSeg != want) {
                app->panels[i]->hoverSeg = want;
                changed = true;
            }
        }
        if (changed) Redraw();
    }
    if (msg == WM_RBUTTONUP) {
        // правой кнопкой перетащили — перемещение в папку под курсором
        if (rbtnDragging) {
            rbtnDragging = false;
            int srcPanel = dragPanel;
            dragPanel = -1;
            app->dropPanel = -1;
            app->dropRow = -1;
            ReleaseCapture();
            if (srcPanel < 0 || srcPanel > 1) return;
            int idx = PanelIndexAt(x);
            if (idx < 0) return;
            Panel* tp = app->panels[idx];
            if (tp->sizeMode || !tp->snapshot) return;
            RECT r = PanelRect(idx);
            int row = (int)((y - r.top - headerH) / rowH);
            if (row < 0) return;
            int t = tp->top + row;
            if (t < 0 || t >= (int)tp->snapshot->size()) return;
            const DirEntry& e = (*tp->snapshot)[t];
            if (e.name == L".." || !e.isDir) return;
            std::wstring dst = PathJoin(tp->path, e.name);
            Panel* sp = app->panels[srcPanel];
            if (sp->sizeMode || !sp->snapshot) return;
            std::vector<std::wstring> srcs;
            bool inSel = sp->selected.count(dragSourceName) != 0;
            if (!sp->selected.empty() && inSel)
                srcs = sp->SelectedPaths(true);
            else if (dragSourceName != L"..")
                srcs.push_back(PathJoin(sp->path, dragSourceName));
            // отсев: каталог в самого себя/своего потомка, файл «уже на месте»
            std::wstring dstN = PathNormalize(dst);
            std::vector<std::wstring> real;
            for (auto& s : srcs) {
                std::wstring sn = PathNormalize(s);
                if (sn == dstN) continue;
                if (sn.size() < dstN.size() && dstN[sn.size()] == L'\\' &&
                    WcsICmp(dstN.substr(0, sn.size()).c_str(), sn.c_str()) == 0)
                    continue;
                if (WcsICmp(PathNormalize(PathParent(s)).c_str(), dstN.c_str()) == 0)
                    continue;
                real.push_back(s);
            }
            if (!real.empty())
                StartCopy(app, real, dst, /*move=*/true, /*ifNewer=*/false);
            else
                app->SetStatusText(L"Перемещение не требуется");
            return;
        }
        // правая кнопка на строке панели — системное контекстное меню
        dragPanel = -1;
        app->dropPanel = -1;
        app->dropRow = -1;
        int idx = PanelIndexAt(x);
        if (idx < 0) return;
        Panel* p = app->panels[idx];
        RECT r = PanelRect(idx);
        // правая кнопка по шапке (в любом месте) — копировать путь панели
        if (!p->sizeMode && y >= r.top && y < r.top + (LONG)headerH) {
            CopyTextToClipboard(hwnd, p->path);
            app->SetStatusText(L"Путь скопирован: " + p->path);
            return;
        }
        int row = (int)((y - r.top - headerH) / rowH);
        if (row < 0) return;
        app->activePanel = idx;
        if (p->sizeMode) {
            // режим размеров: меню для элемента под курсором
            int target = p->top + row;
            if (target < 0 || target >= (int)p->sizeItems.size()) return;
            p->cursor = target;
        } else {
            if (!p->snapshot) return;
            int target = p->top + row;
            if (target < 0 || target >= (int)p->snapshot->size()) return;
            const DirEntry& e = (*p->snapshot)[target];
            if (e.name == L"..") return;
            p->cursor = target;
            p->EnsureVisible();
        }
        POINT pt{ x, y };
        ClientToScreen(hwnd, &pt);
        app->ShowContextMenu(pt.x, pt.y);
        return;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        int idx = PanelIndexAt(x);
        if (idx < 0) return;
        app->activePanel = idx;
        Panel* p = app->panels[idx];
        RECT r = PanelRect(idx);
        // клик по шапке — хлебные крошки
        if (!p->sizeMode && y >= r.top && y < r.top + (LONG)headerH) {
            int s = p->HitSeg((float)x, (float)y);
            if (s >= 0) {
                const Panel::Seg& seg = p->segs[s];
                if (seg.gap) {
                    // «›» — список подпапок этого каталога
                    POINT pt{ x, y };
                    ClientToScreen(hwnd, &pt);
                    std::wstring sub =
                        SubdirPopupDialog(hwnd, app->Theme(), pt.x, pt.y, seg.prefix);
                    if (!sub.empty()) {
                        p->SetPath(sub);
                        Redraw();
                    }
                } else {
                    p->SetPath(seg.prefix);
                    Redraw();
                }
                return;
            }
            return;   // пустое место шапки — просто активация панели
        }
        int row = (int)((y - r.top - headerH) / rowH);
        if (row >= 0 && p->snapshot) {
            int target = p->top + row;
            if (target < (int)p->snapshot->size()) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                p->cursor = target;
                if (ctrl) {
                    const DirEntry& e = (*p->snapshot)[target];
                    if (e.name != L"..") {
                        auto it = p->selected.find(e.name);
                        if (it == p->selected.end()) p->selected.insert(e.name);
                        else p->selected.erase(it);
                    }
                }
                if (msg == WM_LBUTTONDBLCLK) p->EnterCurrent();
                p->EnsureVisible();
                Redraw();
                app->QuickViewCursorMoved();
            }
        }
    }
}

LRESULT CALLBACK MainWindow::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    MainWindow* wnd = (MainWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        wnd = (MainWindow*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)wnd);
        if (wnd) wnd->hwnd = h;
    }
    if (!wnd || !wnd->app) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        wnd->OnPaint(ps.hdc, ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINT:
    case WM_PRINTCLIENT: {
        // отрисовка в переданный DC (PrintWindow, удалённый рабочий стол)
        RECT rc{};
        GetClientRect(h, &rc);
        wnd->OnPaint((HDC)w, rc);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        wnd->renderer.Resize(LOWORD(l), HIWORD(l));
        wnd->Redraw();
        return 0;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        wnd->renderer.Resize(r->right - r->left, r->bottom - r->top);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        wnd->app->OnKey(w, (DWORD)l, m == WM_SYSKEYDOWN);
        return 0;
    case WM_SYSCHAR: {
        wchar_t ch = (wchar_t)w;
        if (ch >= 0x20 && ch != L' ') wnd->app->OnSysChar(ch);
        return 0;
    }
    case WM_CHAR:
        if (w >= 0x20 || w == 8) wnd->app->OnChar((wchar_t)w);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &pt);
        wnd->OnMouse(m, pt.x, pt.y, w);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_MOUSEMOVE:
        wnd->OnMouse(m, GET_X_LPARAM(l), GET_Y_LPARAM(l), w);
        return 0;
    case WM_CAPTURECHANGED:
        // потеря захвата мыши — сбросить состояние перетаскивания
        wnd->rbtnDragging = false;
        wnd->dragPanel = -1;
        if (wnd->app) {
            wnd->app->dropPanel = -1;
            wnd->app->dropRow = -1;
        }
        return 0;
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
        // проброс сообщений расширениям контекстного меню (7-Zip и т.п.)
        HandleShellMenuMsg(m, w, l);
        return 0;
    case WM_RBUTTONUP:
        // системное контекстное меню для файла/папки под курсором
        wnd->OnMouse(m, GET_X_LPARAM(l), GET_Y_LPARAM(l), w);
        return 0;
    case WM_NCCALCSIZE:
        // убираем не-клиентскую рамку (белая полоса сверху) — рамку рисуем сами
        if (w) return 0;
        return DefWindowProcW(h, m, w, l);
    case WM_TIMER: {
        // живой индикатор скана размеров и фоновой индексации: перерисовка
        bool sizeLoading = wnd->app->panels[0]->sizeLoading || wnd->app->panels[1]->sizeLoading;
        bool idxBusy = FolderIndexBusy();
        // завершение индексации (busy→idle): при успехе — подтверждение на ~4 с
        if (wnd->idxBusy && !idxBusy) {
            if (FolderIndexBuildSucceeded())
                wnd->idxDoneUntil = GetTickCount64() + 4000;
            wnd->Redraw();
        }
        wnd->idxBusy = idxBusy;
        if (sizeLoading) {
            wnd->Redraw();
        } else if (idxBusy) {
            // анимация точек — перерисовка только при смене кадра (раз в ~400 мс)
            ULONGLONG frame = GetTickCount64() / 400;
            if (frame != wnd->idxFrame) {
                wnd->idxFrame = frame;
                wnd->Redraw();
            }
        } else if (wnd->idxDoneUntil) {
            if (GetTickCount64() >= wnd->idxDoneUntil) {
                wnd->idxDoneUntil = 0;   // убрать подтверждение
                wnd->Redraw();
            }
        }
        return 0;
    }
    case WM_ACTIVATE:
        // собственная рамка фокуса: синяя, только когда окно неактивно
        wnd->hasFocus = LOWORD(w) != WA_INACTIVE;
        wnd->Redraw();
        return 0;
    case WM_NCHITTEST: {
        // без не-клиентской рамки: края для изменения размера — вручную,
        // зона заголовка — перетаскивание, кнопки — кликабельны
        POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(h, &pt);
        RECT rc{};
        GetClientRect(h, &rc);
        if (!IsZoomed(h)) {
            int b = 6;
            if (pt.x < b && pt.y < b) return HTTOPLEFT;
            if (pt.x >= rc.right - b && pt.y < b) return HTTOPRIGHT;
            if (pt.x < b && pt.y >= rc.bottom - b) return HTBOTTOMLEFT;
            if (pt.x >= rc.right - b && pt.y >= rc.bottom - b) return HTBOTTOMRIGHT;
            if (pt.y < b) return HTTOP;
            if (pt.x < b) return HTLEFT;
            if (pt.x >= rc.right - b) return HTRIGHT;
            if (pt.y >= rc.bottom - b) return HTBOTTOM;
        }
        if (pt.y >= 0 && pt.y < (LONG)wnd->titleH && wnd->TitleButtonAt(pt.x, pt.y) < 0)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
        // двойной клик по заголовку — развернуть/восстановить
        if (w == HTCAPTION) wnd->ToggleMaximize();
        return 0;
    case WM_DEVICECHANGE:
        // подключение/отключение флешек и дисков — обновить линейку
        wnd->app->RefreshDrives();
        return TRUE;
    case WM_APP_DIRLOADED:
        wnd->app->OnLoadedAsync((int)w, (LoadMsg*)l);
        return 0;
    case WM_APP_ASKOVERWRITE:
        HandleAskOverwrite(wnd->app, (void*)l);
        return 0;
    case WM_APP_COPYDONE:
        HandleCopyDone(wnd->app, (CopyCtx*)l);
        return 0;
    case WM_APP_COPYTICK:
        if (wnd->app->progressDlg) wnd->app->progressDlg->Redraw();
        return 0;
    case WM_APP_DRIVEHOTKEY:
        // Alt+F1/F2 через LL-хук (приоритет Amber при фокусе)
        wnd->app->Exec(w ? Cmd::DriveRight : Cmd::DriveLeft);
        return 0;
    case WM_APP_SIZEREADY:
        wnd->app->OnSizeReady((int)w, (SizeMsg*)l);
        return 0;
    case WM_APP_QVREADY:
        wnd->app->OnQuickViewReady((uint64_t)w, (void*)l);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)l;
        HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        RECT wa = mi.rcWork;
        // максимизация ровно на рабочую область — иначе клиентская область
        // вылезает за экран на толщину рамки и правый текст «уезжает»
        mmi->ptMaxPosition.x = wa.left;
        mmi->ptMaxPosition.y = wa.top;
        mmi->ptMaxSize.x = wa.right - wa.left;
        mmi->ptMaxSize.y = wa.bottom - wa.top;
        mmi->ptMinTrackSize.x = std::min<LONG>(640, wa.right - wa.left - 16);
        mmi->ptMinTrackSize.y = std::min<LONG>(360, wa.bottom - wa.top - 16);
        return 0;
    }
    case WM_CLOSE:
        wnd->app->Shutdown();
        return 0;
    case WM_ENDSESSION:
        // завершение сеанса Windows — сохранить состояние
        if (w) wnd->app->SaveState();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
