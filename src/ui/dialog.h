#pragma once
#include "common.h"
#include "render.h"
#include <deque>

// Простые виджеты диалогов, рисуются в тёплом стиле менеджера.

struct Btn {
    std::wstring label;
    RECT rc{};
    int id = 0;
    bool hot = false, pressed = false, def = false;
};
struct EditBox {
    std::wstring text;
    RECT rc{};
    int caret = 0;
    bool focused = false;
};
struct CheckBox {
    std::wstring label;
    RECT rc{};
    bool checked = false;
};
struct ListBox {
    std::vector<std::wstring> items;
    const std::deque<std::wstring>* ref = nullptr;  // внешний источник (без копий)
    RECT rc{};
    int top = 0, sel = -1;
};
struct ProgressBar {
    RECT rc{};
    double value = 0.0;
};

bool InRect(const RECT& rc, LONG x, LONG y);
D2D1_RECT_F ToRect(const RECT& rc);
int ListCount(const ListBox& l);

void DrawBtn(Renderer& r, const Btn& b, const Palette& pal, bool focused);
void DrawEdit(Renderer& r, const EditBox& e, const Palette& pal);
void DrawCheck(Renderer& r, const CheckBox& c, const Palette& pal);
void DrawList(Renderer& r, const ListBox& l, const Palette& pal, float rowH);
void DrawProgress(Renderer& r, const ProgressBar& p, const Palette& pal);

bool EditKey(EditBox& e, WPARAM vk);        // true = клавиша обработана
void EditChar(EditBox& e, wchar_t ch);
bool ListKey(ListBox& l, WPARAM vk, int pageRows);
int ListVisibleRows(const ListBox& l, float rowH);   // реально видимых строк
bool ListClick(ListBox& l, int x, int y, float rowH);

// Базовый класс модального/немодального окна диалога.
class Dialog {
public:
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    bool done = false;
    int result = 0;
    std::wstring title;
    int clientW = 0, clientH = 0;
    Renderer renderer;
    ThemeId theme = ThemeId::Light;
    bool autoDelete = false;
    bool closeOnDeactivate = false;   // закрыть при потере активации (всплывающие меню)

    virtual ~Dialog() {}
    bool Create(const std::wstring& title, int w, int h);
    // всплывающее окно без рамки в заданной позиции (меню выбора дисков и т.п.)
    bool CreatePopup(int x, int y, int w, int h);
    void Close(int res);
    int RunModal();
    void Show() { if (hwnd) ShowWindow(hwnd, SW_SHOW); }
    void Redraw() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
    void SetTimerMs(UINT id, UINT ms) { if (hwnd) SetTimer(hwnd, id, ms, nullptr); }

    virtual void Draw() {}
    virtual void OnKey(WPARAM vk, DWORD flags, bool sys) {}
    virtual void OnChar(wchar_t ch) {}
    virtual void OnMouse(UINT msg, int x, int y, WPARAM wKey) {}
    virtual void OnTimer(UINT id) {}
    virtual LRESULT OnMessage(UINT m, WPARAM w, LPARAM l) {
        return DefWindowProcW(hwnd, m, w, l);
    }
    void ApplyCaptionTheme();

    static LRESULT CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
};
