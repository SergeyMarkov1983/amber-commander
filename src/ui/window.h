#pragma once
#include "common.h"
#include "render.h"

class App;

// Главное окно: отрисовка сцены, клавиатура, мышь, раскладка.
class MainWindow {
public:
    bool Create(App* app);
    HWND Hwnd() const { return hwnd; }
    void Redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
    Renderer& R() { return renderer; }
    void ToggleMaximize();
    // ширина панелей: доля левой (0.2..0.8), Shift+←/→
    void NudgeSplit(int dir);
    void SetSplit(float frac) {
        splitFrac = frac;
        if (splitFrac < 0.2f) splitFrac = 0.2f;
        if (splitFrac > 0.8f) splitFrac = 0.8f;
    }
    float SplitFrac() const { return splitFrac; }

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Layout();
    void OnPaint(HDC hdc, const RECT& rcPaint);
    void OnMouse(UINT msg, int x, int y, WPARAM wKey);
    int PanelIndexAt(int x);
    RECT PanelRect(int idx);
    int TitleButtonAt(int x, int y);

    HWND hwnd = nullptr;
    App* app = nullptr;
    Renderer renderer;

    float W = 0, H = 0, rowH = 20, headerH = 24, keybarH = 26, cmdH = 24, panH = 0, split = 0;
    float splitFrac = 0.5f;          // доля левой панели (0.2..0.8)
    float titleH = 28;
    float driveBarH = 0;
    RECT panRect[2]{};
    std::vector<RECT> driveRects;   // геометрия элементов линейки дисков
    int hoverDrive = -1;
    RECT btnMin{}, btnMax{}, btnClose{};
    bool hMin = false, hMax = false, hClose = false;
    std::vector<RECT> keyRects;      // кнопки нижней линейки (F1..F10 + F11 Размеры)
    int hoverKey = -1;
    bool hasFocus = true;            // рисуем синюю рамку, когда окно не в фокусе
    // перетаскивание правой кнопкой (перемещение в папку под курсором)
    int dragPanel = -1;
    std::wstring dragSourceName;
    POINT dragStart{};
    bool rbtnDragging = false;
    ULONGLONG idxFrame = 0;       // кадр анимации индикатора индексации (throttle)
    bool idxBusy = false;         // идёт ли индексация в прошлый тик таймера
    ULONGLONG idxDoneUntil = 0;   // до какого момента показывать подтверждение
};
