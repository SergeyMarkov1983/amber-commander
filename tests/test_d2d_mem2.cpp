// tests/test_d2d_mem2.cpp — гипотеза: WARP (SOFTWARE) инициализируется лениво при первом Present
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         tests\test_d2d_mem2.cpp -ld2d1 -ldwrite -luser32 -lgdi32 -lpsapi -o tests\test_d2d_mem2.exe
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <psapi.h>
#include <cstdio>

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static void Report(const char* stage) {
    PROCESS_MEMORY_COUNTERS_EX c{};
    c.cb = sizeof(c);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&c, sizeof(c));
    printf("%-30s WS=%6.1f MB  Private=%6.1f MB\n", stage,
           c.WorkingSetSize / 1048576.0, c.PrivateUsage / 1048576.0);
}

static void Pump(int ms) {
    MSG msg;
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }
}

int main(int argc, char** argv) {
    bool software = argc > 1 && argv[1][0] == 's';
    bool setDpi = argc > 2 && argv[2][0] == 'd';

    Report("start");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MemTest2";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, L"MemTest2", L"m", WS_OVERLAPPEDWINDOW, 0, 0, 1240, 800,
                                nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    Report("window shown");

    ID2D1Factory* d2d = nullptr;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    IDWriteFactory* dw = nullptr;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw);
    Report("factories");

    ID2D1HwndRenderTarget* rt = nullptr;
    D2D1_RENDER_TARGET_PROPERTIES props = software
        ? D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE)
        : D2D1::RenderTargetProperties();
    d2d->CreateHwndRenderTarget(props,
                                D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(1240, 800),
                                                                 D2D1_PRESENT_OPTIONS_NONE),
                                &rt);
    Report(software ? "rt (software) created" : "rt (hardware) created");
    if (setDpi && rt) rt->SetDpi(GetDpiForWindow(hwnd), GetDpiForWindow(hwnd));

    IDWriteTextFormat* fmt = nullptr;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ru-ru", &fmt);
    Report("text format");

    // первая отрисовка
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
    Report("first paint done");

    Pump(1500);
    Report("after 1.5s idle");

    rt->Release();
    fmt->Release();
    dw->Release();
    d2d->Release();
    Report("after release");
    return 0;
}
