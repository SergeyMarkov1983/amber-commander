// tests/test_d2d_mem.cpp — изоляция базового потребления D2D/DWrite
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         tests\test_d2d_mem.cpp -ld2d1 -ldwrite -luser32 -lgdi32 -o tests\test_d2d_mem.exe
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <psapi.h>
#include <cstdio>
#include <string>

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static void Report(const char* stage) {
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb = sizeof(c);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&c, sizeof(c));
    printf("%-28s WS=%6.1f MB  Private=%6.1f MB\n", stage,
           c.WorkingSetSize / 1048576.0, c.PrivateUsage / 1048576.0);
}

int main() {
    Report("start");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MemTest";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, L"MemTest", L"m", WS_OVERLAPPEDWINDOW, 0, 0, 1200, 760,
                                nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    Report("window created");

    ID2D1Factory* d2d = nullptr;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    Report("d2d factory");

    IDWriteFactory* dw = nullptr;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        (IUnknown**)&dw);
    Report("dwrite factory");

    ID2D1HwndRenderTarget* rt = nullptr;
    d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(1200, 760),
                                                                 D2D1_PRESENT_OPTIONS_NONE),
                                &rt);
    Report("render target");

    IDWriteTextFormat* fmt = nullptr;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ru-ru", &fmt);
    Report("text format");

    // имитация отрисовки панели: 40 строк текста
    for (int f = 0; f < 10; f++) {
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0.96f, 0.92f, 0.85f));
        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f), &br);
        for (int i = 0; i < 40; i++) {
            D2D1_RECT_F r = { 10, 40 + (float)i * 21, 500, 40 + (float)i * 21 + 20 };
            rt->DrawText(L"Некоторое имя файла в каталоге.txt", 30, fmt, r, br,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        br->Release();
        rt->EndDraw();
    }
    Report("after 10 paints");

    MSG msg;
    for (int i = 0; i < 10; i++) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(200);
    }
    Report("after 2s idle");

    rt->Release();
    fmt->Release();
    dw->Release();
    d2d->Release();
    Report("after release");
    return 0;
}
