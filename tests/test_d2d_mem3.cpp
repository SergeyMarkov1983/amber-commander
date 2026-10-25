// tests/test_d2d_mem3.cpp — точный порядок инициализации как в приложении:
// окно (без Show) -> фабрики -> rt -> шрифты -> ShowWindow -> первый кадр
// Сборка: clang++ -std=c++20 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX
//         -D_WIN32_WINNT=0x0A00 tests\test_d2d_mem3.cpp -ld2d1 -ldwrite -luser32 -lgdi32
//         -lole32 -lpsapi -o tests\test_d2d_mem3.exe
// + манифест: windres tests\test.rc tests\test_res.o и долинковать
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
    printf("%-32s WS=%6.1f MB  Private=%6.1f MB\n", stage,
           c.WorkingSetSize / 1048576.0, c.PrivateUsage / 1048576.0);
}

int main(int argc, char** argv) {
    bool software = argc > 1 && argv[1][0] == 's';
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Report("start (coinit done)");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MemTest3";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, L"MemTest3", L"m", WS_OVERLAPPEDWINDOW, 0, 0, 1240, 800,
                                nullptr, nullptr, wc.hInstance, nullptr);
    Report("window created (hidden)");

    ID2D1Factory* d2d = nullptr;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    IDWriteFactory* dw = nullptr;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw);
    Report("factories");

    D2D1_RENDER_TARGET_PROPERTIES props = software
        ? D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE)
        : D2D1::RenderTargetProperties();
    ID2D1HwndRenderTarget* rt = nullptr;
    d2d->CreateHwndRenderTarget(props,
                                D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(1240, 800),
                                                                 D2D1_PRESENT_OPTIONS_NONE),
                                &rt);
    Report(software ? "rt (SW) created while hidden" : "rt (HW) created while hidden");

    IDWriteTextFormat* fmt = nullptr;
    dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                         DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ru-ru", &fmt);
    Report("text format");

    ShowWindow(hwnd, SW_SHOW);
    Report("shown (no paint yet)");

    UpdateWindow(hwnd); // синхронный WM_PAINT
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
    return 0;
}
