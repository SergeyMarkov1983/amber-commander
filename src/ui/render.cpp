#include "render.h"

// Проверка доступности шрифта в системе (иначе GDI молча подставит другой).
static bool FontExists(const std::wstring& face) {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face.c_str());
    bool found = false;
    EnumFontFamiliesExW(dc, &lf,
                        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp) -> int {
                            *(bool*)lp = true;
                            return 0;
                        },
                        (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}

Renderer::~Renderer() { Release(); }

bool Renderer::Init(HWND h) {
    hwnd = h;
    scale = (float)GetDpiForWindow(hwnd) / 96.0f;
    measureDC = CreateCompatibleDC(nullptr);
    MakeFonts();
    initDone = font != nullptr && measureDC != nullptr;
    return initDone;
}

void Renderer::Release() {
    ClearCaches();
    if (bufDC) {
        SelectObject(bufDC, bufOld);
        DeleteObject(bufBmp);
        DeleteDC(bufDC);
        bufDC = nullptr;
        bufBmp = nullptr;
    }
    if (font) DeleteObject(font);
    if (fontBold) DeleteObject(fontBold);
    if (fontMono) DeleteObject(fontMono);
    if (measureDC) DeleteDC(measureDC);
    font = fontBold = fontMono = nullptr;
    measureDC = nullptr;
    initDone = false;
}

void Renderer::MakeFonts() {
    if (font) DeleteObject(font);
    if (fontBold) DeleteObject(fontBold);
    if (fontMono) DeleteObject(fontMono);
    font = fontBold = fontMono = nullptr;
    std::wstring face = fontFace;
    if (!FontExists(face)) face = L"Segoe UI";   // запасной (напр., Win10 без Variable)
    int px = (int)std::lround((float)fontSizePx * scale);
    int mpx = (int)std::lround((float)fontSizePx * 0.92f * scale);
    font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face.c_str());
    fontBold = CreateFontW(-px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, face.c_str());
    fontMono = CreateFontW(-mpx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
    rowH = RowHeightFor(font);
    monoRowH = RowHeightFor(fontMono);
}

void Renderer::SetFont(const std::wstring& face, int sizePx) {
    if (face.empty() || sizePx <= 0) return;
    fontFace = face;
    fontSizePx = sizePx;
    MakeFonts();
}

float Renderer::RowHeightFor(HFONT f) const {
    if (!f || !measureDC) return 20.0f;
    HGDIOBJ old = SelectObject(measureDC, f);
    TEXTMETRICW tm{};
    GetTextMetricsW(measureDC, &tm);
    SelectObject(measureDC, old);
    return (float)tm.tmHeight + 4.0f;
}

void Renderer::Resize(UINT w, UINT h) {
    (void)w;
    (void)h;
    if (!hwnd) return;
    scale = (float)GetDpiForWindow(hwnd) / 96.0f;
    MakeFonts();
}

void Renderer::BeginDraw(HDC targetDC, const RECT& rcPaint, const D2D1_COLOR_F& clear) {
    target = targetDC;
    blitRect = rcPaint;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0 || !targetDC) {
        hdc = nullptr;
        return;
    }
    // Двойная буферизация: кадр собирается в памяти и выводится одним блитом —
    // полностью исключает мерцание при навигации и смене дисков.
    if (!bufDC || w != bufW || h != bufH) {
        if (bufDC) {
            SelectObject(bufDC, bufOld);
            DeleteObject(bufBmp);
            DeleteDC(bufDC);
        }
        bufDC = CreateCompatibleDC(targetDC);
        bufBmp = CreateCompatibleBitmap(targetDC, w, h);
        bufOld = SelectObject(bufDC, bufBmp);
        bufW = w;
        bufH = h;
    }
    hdc = bufDC;
    FillRect(hdc, &blitRect, Brush(clear));
}

void Renderer::EndDraw() {
    if (hdc && target) {
        int w = blitRect.right - blitRect.left;
        int h = blitRect.bottom - blitRect.top;
        if (w > 0 && h > 0)
            BitBlt(target, blitRect.left, blitRect.top, w, h,
                   hdc, blitRect.left, blitRect.top, SRCCOPY);
    }
    hdc = nullptr;
    target = nullptr;
}

void Renderer::ClearCaches() {
    for (auto& kv : brushes)
        if (kv.second) DeleteObject(kv.second);
    for (auto& kv : pens)
        if (kv.second) DeleteObject(kv.second);
    brushes.clear();
    pens.clear();
}

HBRUSH Renderer::Brush(const D2D1_COLOR_F& c) {
    COLORREF cr = PalRef(c);
    auto it = brushes.find(cr);
    if (it != brushes.end()) return it->second;
    HBRUSH b = CreateSolidBrush(cr);
    brushes[cr] = b;
    return b;
}

HPEN Renderer::Pen(const D2D1_COLOR_F& c, int w) {
    COLORREF cr = PalRef(c);
    uint64_t key = ((uint64_t)cr << 8) | (uint64_t)w;
    auto it = pens.find(key);
    if (it != pens.end()) return it->second;
    HPEN p = CreatePen(PS_SOLID, w, cr);
    pens[key] = p;
    return p;
}

void Renderer::UseFont(HDC target, int weight, bool mono) {
    if (!target) return;
    HFONT f = mono ? fontMono : (weight >= 600 ? fontBold : font);
    if (f) SelectObject(target, f);
}

void Renderer::Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    if (!hdc) return;
    RECT rc = { (int)r.left, (int)r.top, (int)r.right, (int)r.bottom };
    FillRect(hdc, &rc, Brush(c));
}

void Renderer::Rect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float w) {
    if (!hdc) return;
    RECT rc = { (int)r.left, (int)r.top, (int)r.right, (int)r.bottom };
    if (w <= 1.0f) {
        FrameRect(hdc, &rc, Brush(c));
        return;
    }
    int t = std::max(1, (int)w);
    Fill({ r.left, r.top, r.right, r.top + t }, c);
    Fill({ r.left, r.bottom - t, r.right, r.bottom }, c);
    Fill({ r.left, r.top, r.left + t, r.bottom }, c);
    Fill({ r.right - t, r.top, r.right, r.bottom }, c);
}

void Renderer::RoundRect(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c) {
    if (!hdc) return;
    HGDIOBJ oldBr = SelectObject(hdc, Brush(c));
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    ::RoundRect(hdc, (int)r.left, (int)r.top, (int)r.right, (int)r.bottom,
                (int)radius, (int)radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
}

void Renderer::Line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F& c, float w) {
    if (!hdc) return;
    HGDIOBJ oldPen = SelectObject(hdc, Pen(c, std::max(1, (int)w)));
    MoveToEx(hdc, (int)x0, (int)y0, nullptr);
    LineTo(hdc, (int)x1, (int)y1);
    SelectObject(hdc, oldPen);
}

void Renderer::Ellipse(const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    if (!hdc) return;
    HGDIOBJ oldBr = SelectObject(hdc, Brush(c));
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    ::Ellipse(hdc, (int)r.left, (int)r.top, (int)r.right, (int)r.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
}

void Renderer::Polygon(const D2D1_POINT_2F* pts, int n, const D2D1_COLOR_F& c) {
    if (!hdc || n < 3) return;
    std::vector<POINT> g(n);
    for (int i = 0; i < n; i++) {
        g[i].x = (int)pts[i].x;
        g[i].y = (int)pts[i].y;
    }
    HGDIOBJ oldBr = SelectObject(hdc, Brush(c));
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    ::Polygon(hdc, g.data(), n);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
}

D2D1_SIZE_F Renderer::Measure(const std::wstring& s, int weight) {
    D2D1_SIZE_F out = { 0, 0 };
    if (!measureDC || s.empty()) return out;
    UseFont(measureDC, weight, false);
    SIZE sz{};
    GetTextExtentPoint32W(measureDC, s.c_str(), (int)s.size(), &sz);
    out = { (float)sz.cx, (float)sz.cy };
    return out;
}

static void DoExtText(HDC hdc, const std::wstring& s, const RECT& rc, bool clip, bool rightAlign) {
    if (hdc && !s.empty()) {
        ExtTextOutW(hdc, rightAlign ? rc.right : rc.left, rc.top,
                    clip ? ETO_CLIPPED : 0, &rc, s.c_str(), (UINT)s.size(), nullptr);
    }
}

void Renderer::Text(const std::wstring& s, const D2D1_RECT_F& r, const D2D1_COLOR_F& c,
                    bool rightAlign, bool vcenter, bool clip, int weight) {
    if (!hdc || s.empty()) return;
    int saved = SaveDC(hdc);
    UseFont(hdc, weight, false);
    SetTextColor(hdc, PalRef(c));
    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_TOP | TA_LEFT);

    float boxW = r.right - r.left;
    std::wstring disp = s;
    if (clip) {
        SIZE sz{};
        GetTextExtentPoint32W(hdc, disp.c_str(), (int)disp.size(), &sz);
        if ((float)sz.cx > boxW) {
            // обрезка с многоточием: бинарный поиск префикса
            size_t lo = 0, hi = disp.size();
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                std::wstring t = disp.substr(0, mid) + L"…";
                GetTextExtentPoint32W(hdc, t.c_str(), (int)t.size(), &sz);
                if ((float)sz.cx > boxW) hi = mid;
                else lo = mid + 1;
            }
            if (lo > 0) disp = disp.substr(0, lo - 1) + L"…";
        }
    }

    RECT rc = { (int)r.left, (int)r.top, (int)r.right, (int)r.bottom };
    if (vcenter || rightAlign) {
        SIZE sz{};
        GetTextExtentPoint32W(hdc, disp.c_str(), (int)disp.size(), &sz);
        if (vcenter) rc.top = (int)r.top + std::max(0, ((int)(r.bottom - r.top) - (int)sz.cy) / 2);
        if (rightAlign) rc.left = std::max((int)r.left, (int)r.right - (int)sz.cx);
    }
    DoExtText(hdc, disp, rc, clip, false);
    RestoreDC(hdc, saved);
}

void Renderer::TextMono(const std::wstring& s, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    if (!hdc || s.empty()) return;
    int saved = SaveDC(hdc);
    SelectObject(hdc, fontMono);
    SetTextColor(hdc, PalRef(c));
    SetBkMode(hdc, TRANSPARENT);
    RECT rc = { (int)r.left, (int)r.top, (int)r.right, (int)r.bottom };
    DoExtText(hdc, s, rc, true, false);
    RestoreDC(hdc, saved);
}

float Renderer::RowHeight() const { return rowH; }
float Renderer::MonoRowHeight() const { return monoRowH; }

void Renderer::DrawImage(HBITMAP bmp, const D2D1_RECT_F& dst) {
    if (!hdc || !bmp) return;
    BITMAP bm{};
    GetObjectW(bmp, sizeof(bm), &bm);
    if (!bm.bmWidth || !bm.bmHeight) return;
    HDC src = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(src, bmp);
    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, (int)dst.left, (int)dst.top,
               std::max(1, (int)(dst.right - dst.left)), std::max(1, (int)(dst.bottom - dst.top)),
               src, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(src, old);
    DeleteDC(src);
}
