#include "dialog.h"
#include <dwmapi.h>

bool InRect(const RECT& rc, LONG x, LONG y) {
    return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
}

D2D1_RECT_F ToRect(const RECT& rc) {
    return { (float)rc.left, (float)rc.top, (float)rc.right, (float)rc.bottom };
}

int ListCount(const ListBox& l) { return l.ref ? (int)l.ref->size() : (int)l.items.size(); }

void DrawBtn(Renderer& r, const Btn& b, const Palette& pal, bool focused) {
    D2D1_RECT_F rc = ToRect(b.rc);
    D2D1_COLOR_F fill = b.pressed ? pal.cursorBg : (b.hot || focused ? pal.selBg : pal.btnBg);
    r.RoundRect(rc, 5, fill);
    r.Rect(rc, focused ? pal.accent : pal.border, focused ? 2.0f : 1.0f);
    // Подпись строго по центру кнопки. Цвет подбирается под заливку:
    // на «фокусной» кнопке — selText (в светлых темах тёмный, в тёмных — светлый),
    // иначе — обычный текст (он по дизайну контрастен фону кнопки).
    D2D1_COLOR_F tc = focused ? pal.selText : pal.text;
    float tw = r.Measure(b.label).width;
    D2D1_RECT_F tr = { rc.left + (rc.right - rc.left - tw) / 2.0f, rc.top, rc.right, rc.bottom };
    if (tr.left < rc.left + 4) tr.left = rc.left + 4;
    r.Text(b.label, tr, tc, false, true, true);
}

void DrawEdit(Renderer& r, const EditBox& e, const Palette& pal) {
    D2D1_RECT_F rc = ToRect(e.rc);
    r.Fill(rc, pal.cmdLineBg);
    r.Rect(rc, e.focused ? pal.accent : pal.border, e.focused ? 2.0f : 1.0f);
    if (!e.text.empty())
        r.Text(e.text, { rc.left + 6, rc.top, rc.right - 6, rc.bottom }, pal.cmdLineText, false, true, true);
    if (e.focused) {
        float x = rc.left + 6 + (e.caret > 0 ? r.Measure(e.text.substr(0, (size_t)e.caret)).width : 0.0f);
        if ((GetTickCount64() / 500) & 1)
            r.Fill({ x, rc.top + 4, x + 1.5f, rc.bottom - 4 }, pal.text);
    }
}

void DrawCheck(Renderer& r, const CheckBox& c, const Palette& pal) {
    D2D1_RECT_F box = { (float)c.rc.left, (float)c.rc.top + 2, (float)c.rc.left + 16, (float)c.rc.top + 18 };
    r.Fill(box, pal.cmdLineBg);
    r.Rect(box, c.checked ? pal.accent : pal.border, c.checked ? 2.0f : 1.0f);
    if (c.checked)
        r.Fill({ box.left + 4, box.top + 4, box.right - 4, box.bottom - 4 }, pal.accent);
    r.Text(c.label, { (float)c.rc.left + 22, (float)c.rc.top, (float)c.rc.right, (float)c.rc.bottom },
           pal.text, false, true, false);
}

void DrawList(Renderer& r, const ListBox& l, const Palette& pal, float rowH) {
    D2D1_RECT_F rc = ToRect(l.rc);
    r.Fill(rc, pal.cmdLineBg);
    r.Rect(rc, pal.border, 1.0f);
    int n = ListCount(l);
    for (int i = l.top; i < n; i++) {
        float y = rc.top + (i - l.top) * rowH;
        if (y >= rc.bottom - rowH) break;
        D2D1_RECT_F row = { rc.left + 3, y, rc.right - 14, y + rowH };
        if (i == l.sel) r.Fill(row, pal.selBg);
        const std::wstring& item = l.ref ? (*l.ref)[i] : l.items[i];
        r.Text(item, row, i == l.sel ? pal.selText : pal.text, false, true, true);
    }
    if (n > 0) {
        float total = (float)n * rowH;
        float view = rc.bottom - rc.top;
        if (total > view) {
            float thumbH = std::max(16.0f, view * view / total);
            float maxTop = view - thumbH;
            float pos = maxTop * (float)l.top * rowH / (total - view);
            r.Fill({ rc.right - 10, rc.top + pos, rc.right - 4, rc.top + pos + thumbH }, pal.dimText);
        }
    }
}

void DrawProgress(Renderer& r, const ProgressBar& p, const Palette& pal) {
    D2D1_RECT_F rc = ToRect(p.rc);
    r.Fill(rc, pal.cmdLineBg);
    r.Rect(rc, pal.border, 1.0f);
    double v = p.value < 0 ? 0 : (p.value > 1 ? 1 : p.value);
    float w = (rc.right - rc.left - 4) * (float)v;
    if (w > 1.0f) r.Fill({ rc.left + 2, rc.top + 2, rc.left + 2 + w, rc.bottom - 2 }, pal.accent);
}

bool EditKey(EditBox& e, WPARAM vk) {
    switch (vk) {
    case VK_LEFT:  if (e.caret > 0) e.caret--; return true;
    case VK_RIGHT: if (e.caret < (int)e.text.size()) e.caret++; return true;
    case VK_HOME:  e.caret = 0; return true;
    case VK_END:   e.caret = (int)e.text.size(); return true;
    case VK_BACK:
        if (e.caret > 0) { e.text.erase((size_t)e.caret - 1, 1); e.caret--; }
        return true;
    case VK_DELETE:
        if (e.caret < (int)e.text.size()) e.text.erase((size_t)e.caret, 1);
        return true;
    default: return false;
    }
}

void EditChar(EditBox& e, wchar_t ch) {
    if (ch >= 0x20) {
        e.text.insert((size_t)e.caret, 1, ch);
        e.caret++;
    }
}

bool ListKey(ListBox& l, WPARAM vk, int pageRows) {
    int n = ListCount(l);
    if (n == 0) return false;
    switch (vk) {
    case VK_UP:    if (l.sel > 0) l.sel--; break;
    case VK_DOWN:  if (l.sel < n - 1) l.sel++; break;
    case VK_HOME:  l.sel = 0; break;
    case VK_END:   l.sel = n - 1; break;
    case VK_PRIOR: l.sel = std::max(0, l.sel - pageRows); break;
    case VK_NEXT:  l.sel = std::min(n - 1, l.sel + pageRows); break;
    default: return false;
    }
    if (l.sel < 0) l.sel = 0;
    if (l.sel < l.top) l.top = l.sel;
    if (l.sel >= l.top + pageRows) l.top = l.sel - pageRows + 1;
    // страховка: прокрутка не уходит дальше последней видимой позиции
    int maxTop = std::max(0, n - pageRows);
    if (l.top > maxTop) l.top = maxTop;
    return true;
}

int ListVisibleRows(const ListBox& l, float rowH) {
    int v = (int)((l.rc.bottom - l.rc.top) / rowH);
    return std::max(1, v);
}

bool ListClick(ListBox& l, int x, int y, float rowH) {
    int n = ListCount(l);
    if (n == 0) return false;
    int row = (int)((y - l.rc.top) / rowH);
    int idx = l.top + row;
    if (idx < 0 || idx >= n) return false;
    l.sel = idx;
    return true;
}

// ---------- Dialog ----------

LRESULT CALLBACK Dialog::DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Dialog* d = (Dialog*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        d = (Dialog*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)d);
        if (d) d->hwnd = h;
    }
    if (!d) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (d->renderer.IsReady()) {
            d->renderer.BeginDraw(ps.hdc, ps.rcPaint, GetPalette(d->theme).bg);
            d->Draw();
            d->renderer.EndDraw();
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        d->OnKey(w, (DWORD)l, m == WM_SYSKEYDOWN);
        return 0;
    case WM_CHAR:
        d->OnChar((wchar_t)w);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_MOUSEMOVE:
        d->OnMouse(m, GET_X_LPARAM(l), GET_Y_LPARAM(l), w);
        return 0;
    case WM_TIMER:
        d->OnTimer((UINT)w);
        return 0;
    case WM_ACTIVATE:
        // всплывающие меню закрываются при клике мимо
        if (LOWORD(w) == WA_INACTIVE && d->closeOnDeactivate) d->Close(-1);
        return 0;
    case WM_CLOSE:
        d->Close(-1);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        if (d->autoDelete) delete d;
        return 0;
    }
    return d->OnMessage(m, w, l);
}

bool Dialog::Create(const std::wstring& t, int w, int h) {
    title = t;
    clientW = w;
    clientH = h;
    static bool clsRegistered = false;
    if (!clsRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.hIconSm = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"AmberDialog";
        RegisterClassExW(&wc);
        clsRegistered = true;
    }
    RECT r{ 0, 0, w, h };
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    AdjustWindowRect(&r, style, FALSE);
    int ow = r.right - r.left, oh = r.bottom - r.top;
    // Центр рабочей области монитора родителя + зажим в границы экрана
    HMONITOR mon = nullptr;
    if (parent) mon = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    POINT pt{ 0, 0 };
    if (!mon) mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    RECT wa = mi.rcWork;
    if (ow > wa.right - wa.left) ow = wa.right - wa.left;
    if (oh > wa.bottom - wa.top) oh = wa.bottom - wa.top;
    int x = wa.left + ((wa.right - wa.left) - ow) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - oh) / 2;
    hwnd = CreateWindowExW(0, L"AmberDialog", t.c_str(), style, x, y, ow, oh,
                           parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd) return false;
    renderer.Init(hwnd);
    ApplyCaptionTheme();
    return true;
}

// Windows 11: тёплая системная шапка диалога (цвета текущей темы менеджера)
void Dialog::ApplyCaptionTheme() {
    const Palette& pal = GetPalette(theme);
    COLORREF cap = PalRef(pal.headerInactive);
    COLORREF txt = PalRef(pal.headerText);
    COLORREF brd = PalRef(pal.border);
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
    DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &brd, sizeof(brd));
}

bool Dialog::CreatePopup(int x, int y, int w, int h) {
    clientW = w;
    clientH = h;
    static bool clsRegistered = false;
    if (!clsRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"AmberDialog";
        RegisterClassExW(&wc);
        clsRegistered = true;
    }
    hwnd = CreateWindowExW(0, L"AmberDialog", L"", WS_POPUP, x, y, w, h,
                           parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd) return false;
    renderer.Init(hwnd);
    return true;
}

void Dialog::Close(int res) {
    if (done) return;
    done = true;
    result = res;
    if (hwnd) DestroyWindow(hwnd);
}

int Dialog::RunModal() {
    if (!hwnd) return 0;
    bool disableParent = parent && !closeOnDeactivate;
    if (disableParent) EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(hwnd);
    MSG msg;
    while (!done) {
        GetMessageW(&msg, nullptr, 0, 0);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (disableParent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    return result;
}
