#pragma once
#include "common.h"
#include "theme.h"
#include <d2d1.h>

// Обёртка отрисовки на чистом GDI (ExtTextOut + примитивы).
// Выбор сделан по результатам замеров: любой путь Direct2D (hwnd-таргет,
// WARP, DC-таргет) выделяет ~50 МБ private при показе окна 1240×800,
// тогда как GDI рисует прямо в поверхность окна и приложение остаётся
// в пределах ~3 МБ — в духе Volkov Commander.
// Типы координат/цветов сохранены с D2D-версии (D2D1_RECT_F/D2D1_COLOR_F),
// чтобы код панелей/диалогов не менялся.
class Renderer {
public:
    ~Renderer();
    bool Init(HWND hwnd);
    void Release();
    void Resize(UINT w, UINT h);       // обновляет масштаб DPI и шрифты
    void SetFont(const std::wstring& face, int sizePx);
    void BeginDraw(HDC hdc, const RECT& rcPaint, const D2D1_COLOR_F& clear);
    void EndDraw();
    bool IsReady() const { return initDone; }
    float Scale() const { return scale; }

    void Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void Rect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float w = 1.0f);
    void RoundRect(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c);
    void Line(float x0, float y0, float x1, float y1, const D2D1_COLOR_F& c, float w = 1.0f);
    void Ellipse(const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void Polygon(const D2D1_POINT_2F* pts, int n, const D2D1_COLOR_F& c);

    // weight: FW_NORMAL (400) / FW_SEMIBOLD (600) / FW_BOLD (700)
    void Text(const std::wstring& s, const D2D1_RECT_F& r, const D2D1_COLOR_F& c,
              bool rightAlign = false, bool vcenter = true, bool clip = true,
              int weight = 400);
    // моноширинная строка сверху по вертикали, с обрезкой (просмотрщик)
    void TextMono(const std::wstring& s, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    D2D1_SIZE_F Measure(const std::wstring& s, int weight = 400);
    float RowHeight() const;
    float MonoRowHeight() const;
    // растянутый вывод HBITMAP (просмотрщик изображений)
    void DrawImage(HBITMAP bmp, const D2D1_RECT_F& dst);

    HDC DC() { return hdc; }

private:
    void MakeFonts();
    void ClearCaches();
    HBRUSH Brush(const D2D1_COLOR_F& c);
    HPEN Pen(const D2D1_COLOR_F& c, int w);
    void UseFont(HDC target, int weight, bool mono);
    float RowHeightFor(HFONT font) const;

    HWND hwnd = nullptr;
    bool initDone = false;
    HDC hdc = nullptr;                 // буферный DC текущего кадра (куда рисуем)
    HDC target = nullptr;              // DC окна (куда бросаем готовый кадр)
    RECT blitRect{};
    HDC bufDC = nullptr;               // постоянный буферный DC (двойная буферизация)
    HBITMAP bufBmp = nullptr;
    HGDIOBJ bufOld = nullptr;
    int bufW = 0, bufH = 0;
    HDC measureDC = nullptr;           // для замеров текста
    HFONT font = nullptr, fontBold = nullptr, fontMono = nullptr;
    std::wstring fontFace = L"Segoe UI Variable Text";
    int fontSizePx = 12;
    float scale = 1.0f;
    float rowH = 20, monoRowH = 19;
    std::unordered_map<COLORREF, HBRUSH> brushes;
    std::unordered_map<uint64_t, HPEN> pens;
};
