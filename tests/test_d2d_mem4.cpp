// tests/test_d2d_mem4.cpp — ID2D1DCRenderTarget: рендер в GDI DC окна, без своей поверхности
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         -D_WIN32_WINNT=0x0A00 tests\test_d2d_mem4.cpp tests\test_res.o -ld2d1 -ldwrite
//         -luser32 -lgdi32 -lole32 -lpsapi -o tests\test_d2d_mem4.exe
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <psapi.h>
#include <cstdio>

static ID2D1DCRenderTarget* g_rt = nullptr;
static ID2D1SolidColorBrush* g_br = nullptr;
static IDWriteTextFormat* g_fmt = nullptr;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        g_rt->BindDC(hdc, &ps.rcPaint);
        g_rt->BeginDraw();
        g_rt->Clear(D2D1::ColorF(0.96f, 0.92f, 0.85f));
        for (int i = 0; i < 40; i++) {
            D2D1_RECT_F r = { 10, 40 + (float)i * 21, 500, 40 + (float)i * 21 + 20 };
            g_rt->DrawText(L"Некоторое имя файла в каталоге.txt", 30, g_fmt, r, g_br,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        g_rt->EndDraw();
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static void Report(const char* stage) {
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb = sizeof(c);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&c, sizeof(c));
    printf("%-32s WS=%6.1f MB  Private=%6.1f MB\n", stage,
           c.WorkingSetSize / 1048576.0, c.PrivateUsage / 1048576.0);
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Report("start");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MemTest4";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, L"MemTest4", L"m", WS_OVERLAPPEDWINDOW, 0, 0, 1240, 800,
                                nullptr, nullptr, wc.hInstance, nullptr);
    Report("window created (hidden)");

    ID2D1Factory* d2d = nullptr;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    IDWriteFactory* dw = nullptr;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw);
    Report("factories");

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                           D2D1_ALPHA_MODE_IGNORE),
        0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    d2d->CreateDCRenderTarget(&props, &g_rt);
    Report("dc render target created");

    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ru-ru", &g_fmt);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f), &g_br);
    Report("text format + brush");

    ShowWindow(hwnd, SW_SHOW);
    Report("shown (no paint yet)");

    UpdateWindow(hwnd);
    Report("first paint done");

    MSG msg;
    for (int i = 0; i < 10; i++) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(100);
    }
    Report("after 1s idle");

    // повторная перерисовка (имитация скролла)
    for (int f = 0; f < 30; f++) {
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
    }
    Report("after 30 full repaints");
    return 0;
}
