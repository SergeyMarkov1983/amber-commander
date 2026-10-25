// tests/test_d2d_mem5.cpp — изоляция скачка на ShowWindow:
// режимы: 0=ничего, 1=только DWriteFactory, 2=только D2DFactory, 3=фабрики+rt(HW), 4=фабрики+rt(DC)
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>

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

int main(int argc, char** argv) {
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Report("start");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MemTest5";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, L"MemTest5", L"m", WS_OVERLAPPEDWINDOW, 0, 0, 1240, 800,
                                nullptr, nullptr, wc.hInstance, nullptr);
    Report("window created");

    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dw = nullptr;
    if (mode >= 2) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    if (mode == 1 || mode >= 3)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw);
    Report("factories per mode");

    ID2D1HwndRenderTarget* hwrt = nullptr;
    ID2D1DCRenderTarget* dcrt = nullptr;
    if (mode == 3) {
        d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),
                                    D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(1240, 800),
                                                                     D2D1_PRESENT_OPTIONS_NONE),
                                    &hwrt);
    } else if (mode == 4) {
        D2D1_RENDER_TARGET_PROPERTIES dcp = D2D1::RenderTargetProperties();
        d2d->CreateDCRenderTarget(&dcp, &dcrt);
    }
    Report("target created");

    ShowWindow(hwnd, SW_SHOW);
    Report("shown");

    UpdateWindow(hwnd);
    Report("paint done");

    MSG msg;
    for (int i = 0; i < 10; i++) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(100);
    }
    Report("1s idle");
    return 0;
}
