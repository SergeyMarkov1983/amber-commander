#include "app.h"
#include "ui/window.h"
#include "ui/panel.h"
#include "dialogs.h"
#include "config.h"
#include "pathutil.h"
#include "core/folderidx.h"
#include <wincodec.h>

// ── Alt+F1/F2: приоритет Amber, пока фокус на его окне ──
// Низкоуровневый клавиатурный хук перехватывает комбинации ДО глобальных
// горячих клавиш других программ (NVIDIA App и т.п.): если фокус на окне
// Amber (или его диалоге) — клавиша достаётся Amber, иначе проходит дальше
// и работает штатная программа.
namespace {

HHOOK g_kbHook = nullptr;
App* g_hookApp = nullptr;

LRESULT CALLBACK LowLevelKbProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && g_hookApp) {
        auto* kbd = (KBDLLHOOKSTRUCT*)l;
        if ((kbd->vkCode == VK_F1 || kbd->vkCode == VK_F2) &&
            (kbd->flags & LLKHF_ALTDOWN) &&
            (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
            HWND fg = GetForegroundWindow();
            DWORD fgPid = 0;
            GetWindowThreadProcessId(fg, &fgPid);
            if (fgPid == GetCurrentProcessId()) {
                // фокус у Amber — забираем клавишу себе
                if (g_hookApp->win)
                    PostMessageW(g_hookApp->win->Hwnd(), WM_APP_DRIVEHOTKEY,
                                 (WPARAM)(kbd->vkCode == VK_F1 ? 0 : 1), 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_kbHook, code, w, l);
}

} // namespace

void App::SetStatusText(const std::wstring& s) {
    statusText = s;
    statusUntil = GetTickCount64() + 2500;
    Redraw();
}

void App::RefreshDrives() {
    drives = GetDriveItems();
    Redraw();
}

void App::Redraw() {
    if (win) win->Redraw();
}

// ── Быстрый просмотр (Ctrl+Q) ──
// Лёгкое превью в пассивной панели: картинки (WIC, с уменьшением до 1024),
// текст (первые 64 КБ), для прочих файлов — карточка с атрибутами и HEX-шапкой.
// PDF/видео сознательно не трогаем: Enter открывает их штатно, а тяжёлые
// библиотеки противоречат идее лёгкого менеджера.

struct QvData {
    HWND hwnd = nullptr;
    uint64_t gen = 0;
    std::wstring path;
    std::mutex m;
    bool canceled = false;
    HBITMAP bmp = nullptr;
    UINT w = 0, h = 0;
    std::wstring error;
    bool isThumb = false;   // первый проход (EXIF-миниатюра)
    ~QvData() {
        if (bmp) DeleteObject(bmp);
    }
};

// Эвристика: похоже ли содержимое на валидный UTF-8 (без BOM).
static bool QvLooksUtf8(const unsigned char* p, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        if (c < 0x80) { i++; continue; }
        size_t need;
        unsigned char lo;
        if ((c & 0xE0) == 0xC0) { need = 2; lo = 0x80; }
        else if ((c & 0xF0) == 0xE0) { need = 3; lo = 0x80; }
        else if ((c & 0xF8) == 0xF0) { need = 4; lo = 0x80; }
        else return false;
        if (i + need > n) return false;
        for (size_t k = 1; k < need; k++)
            if ((p[i + k] & 0xC0) != 0x80) return false;
        if (p[i + 1] < lo) return false;
        i += need;
    }
    return true;
}

// Чтение первых 64 КБ файла с определением кодировки (UTF-16/UTF-8/ANSI-1251).
static void QvReadText(const std::wstring& path, std::wstring& out, bool& cut) {
    HANDLE h = CreateFileW(LongPath(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        out = L"(не удалось открыть)";
        return;
    }
    std::vector<unsigned char> buf(64 * 1024);
    DWORD rd = 0;
    if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, nullptr)) rd = 0;
    cut = rd == buf.size();
    if (cut) {
        // проверим, есть ли ещё байты (тогда файл действительно больше)
        unsigned char extra = 0;
        DWORD rd2 = 0;
        if (ReadFile(h, &extra, 1, &rd2, nullptr) && rd2 == 0) cut = false;
    }
    CloseHandle(h);

    std::wstring text;
    if (rd >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {          // UTF-16 LE
        size_t n = (rd - 2) / 2;
        text.assign((wchar_t*)(buf.data() + 2), n);
    } else if (rd >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {   // UTF-16 BE
        for (DWORD i = 2; i + 1 < rd; i += 2)
            text += (wchar_t)((buf[i] << 8) | buf[i + 1]);
    } else {
        bool bomUtf8 = rd >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF;
        bool utf8 = bomUtf8 || QvLooksUtf8(buf.data(), rd);
        UINT cp = utf8 ? CP_UTF8 : 1251;
        const unsigned char* src = buf.data();
        DWORD len = rd;
        if (bomUtf8) {
            src += 3;
            len -= 3;
        }
        int wlen = MultiByteToWideChar(cp, 0, (const char*)src, len, nullptr, 0);
        text.resize((size_t)std::max(0, wlen));
        if (wlen > 0)
            MultiByteToWideChar(cp, 0, (const char*)src, len, &text[0], wlen);
    }
    out = std::move(text);
}

static std::wstring QvTypeLabel(IconId id) {
    switch (id) {
    case IconId::Folder: return L"Каталог";
    case IconId::Exe: return L"Программа";
    case IconId::Text: return L"Текст";
    case IconId::Image: return L"Изображение";
    case IconId::Video: return L"Видео";
    case IconId::Audio: return L"Аудио";
    case IconId::Archive: return L"Архив";
    case IconId::Font: return L"Шрифт";
    case IconId::Link: return L"Ярлык";
    case IconId::Sys: return L"Системный файл";
    default: return L"Файл";
    }
}

static std::wstring QvFileInfo(const std::wstring& path,
                               const WIN32_FILE_ATTRIBUTE_DATA& fad, IconId id) {
    ULARGE_INTEGER sz;
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    std::wstring s = PathFileName(path) + L"\n";
    s += L"Тип: " + QvTypeLabel(id) + L"\n";
    s += L"Размер: " + FormatSize(sz.QuadPart) + L"\n";
    s += L"Изменён: " + FormatDate(fad.ftLastWriteTime) + L"\n";
    std::wstring a;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) a += L"Скрытый, ";
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) a += L"Системный, ";
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) a += L"Только чтение, ";
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) a += L"Сжатый, ";
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) a += L"Зашифрован, ";
    if (!a.empty()) {
        a.resize(a.size() - 2);
        s += L"Атрибуты: " + a + L"\n";
    }
    // HEX-шапка (первые 16 байт)
    HANDLE h = CreateFileW(LongPath(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        unsigned char b[16];
        DWORD rd = 0;
        ReadFile(h, b, sizeof(b), &rd, nullptr);
        CloseHandle(h);
        if (rd) {
            std::wstring hex = L"Байты: ";
            for (DWORD i = 0; i < rd; i++) {
                wchar_t t[4];
                wsprintfW(t, L"%02X ", b[i]);
                hex += t;
            }
            s += hex;
        }
    }
    return s;
}

// IWICBitmapSourceTransform отсутствует в заголовках MinGW — объявляем вручную.
// Типы параметров для ABI не важны (важны размеры слотов), поэтому WICRect заменён
// на void*, а флаги — на UINT.
struct IWICBitmapSourceTransform : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CopyPixels(const void* prc, UINT uiWidth, UINT uiHeight,
                                                 const GUID* pguidDstFormat, UINT dstTransform,
                                                 UINT nStride, UINT cbBufferSize,
                                                 BYTE* pbBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClosestSize(UINT* puiWidth, UINT* puiHeight) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClosestPixelFormat(GUID* pguidDstFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE DoesSupportTransform(UINT dstTransform,
                                                           BOOL* pfIsSupported) = 0;
};
static const GUID IID_QvTransform = { 0x3b16811b, 0x6a43, 0x4ec9,
                                      { 0xb7, 0x13, 0x3d, 0x5a, 0x0c, 0x13, 0xb9, 0x40 } };

// IWICBitmapSource -> HBITMAP (32bpp, top-down)
static HBITMAP QvSourceToBmp(IWICBitmapSource* src, std::wstring& err) {
    UINT w = 0, h = 0;
    if (!src || FAILED(src->GetSize(&w, &h)) || !w || !h || w > 30000 || h > 30000) {
        err = L"Нет изображения";
        return nullptr;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC sdc = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, sdc);
    if (!bmp) {
        err = L"Недостаточно памяти";
        return nullptr;
    }
    if (FAILED(src->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits))) {
        DeleteObject(bmp);
        err = L"Ошибка чтения пикселей";
        return nullptr;
    }
    return bmp;
}

// Быстрый уменьшенный декод. Для JPEG использует IWICBitmapSourceTransform —
// обратное ДКП сразу в малый размер (как миниатюры в Проводнике), без
// декодирования полного кадра. Возвращает HBITMAP ≤ maxDim по большей стороне.
static HBITMAP QvDecodePreview(IWICImagingFactory* wic, IWICBitmapDecoder* dec,
                               UINT maxDim, UINT& outW, UINT& outH, std::wstring& err) {
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(dec->GetFrame(0, &frame)) || !frame) {
        err = L"В изображении нет кадров";
        return nullptr;
    }
    UINT fw = 0, fh = 0;
    frame->GetSize(&fw, &fh);
    if (!fw || !fh || fw > 30000 || fh > 30000 || (uint64_t)fw * fh > 250000000ull) {
        frame->Release();
        err = L"Изображение слишком велико";
        return nullptr;
    }
    outW = fw;
    outH = fh;
    float s = 1.0f;
    if (fw > maxDim || fh > maxDim) s = (float)maxDim / (fw > fh ? fw : fh);
    UINT dw = std::max(1u, (UINT)(fw * s + 0.5f));
    UINT dh = std::max(1u, (UINT)(fh * s + 0.5f));
    HBITMAP bmp = nullptr;
    // путь 1: декод-и-масштаб за один проход
    IWICBitmapSourceTransform* t = nullptr;
    if (SUCCEEDED(frame->QueryInterface(IID_QvTransform, (void**)&t)) && t) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = (LONG)dw;
        bi.bmiHeader.biHeight = -(LONG)dh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC sdc = GetDC(nullptr);
        bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, sdc);
        if (bmp) {
            HRESULT hr = t->CopyPixels(nullptr, dw, dh, &GUID_WICPixelFormat32bppPBGRA,
                                       0, dw * 4, dw * 4 * dh, (BYTE*)bits);
            if (FAILED(hr)) {
                DeleteObject(bmp);
                bmp = nullptr;
            }
        }
        t->Release();
    }
    // путь 2 (запасной): конвертер + скалер
    if (!bmp) {
        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(wic->CreateFormatConverter(&conv)) && conv &&
            SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
            IWICBitmapSource* src = conv;
            IWICBitmapScaler* scaler = nullptr;
            if ((dw != fw || dh != fh) && SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
                scaler && SUCCEEDED(scaler->Initialize(conv, dw, dh,
                                                       WICBitmapInterpolationModeFant)))
                src = scaler;
            bmp = QvSourceToBmp(src, err);
            if (scaler) scaler->Release();
        }
        if (conv) conv->Release();
    }
    frame->Release();
    if (!bmp && err.empty()) err = L"Не удалось декодировать изображение";
    return bmp;
}

static void QvImageThread(QvData* d) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto canceled = [&]() -> bool {
        std::lock_guard<std::mutex> g(d->m);
        return d->canceled;
    };
    // Дебаунс: ждём остановки курсора — при пролистывании стрелкой HDD
    // не дёргается на каждый файл, декодируется только последний.
    Sleep(120);
    if (canceled()) {
        CoUninitialize();
        delete d;
        return;
    }
    IWICImagingFactory* wic = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wic));
    IWICBitmapDecoder* dec = nullptr;
    if (SUCCEEDED(hr) && wic)
        hr = wic->CreateDecoderFromFilename(LongPath(d->path).c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &dec);
    if (FAILED(hr) || !dec) {
        if (wic) wic->Release();
        if (!canceled()) {
            d->error = L"Не удалось декодировать изображение";
            PostMessageW(d->hwnd, WM_APP_QVREADY, (WPARAM)d->gen, (LPARAM)d);
        } else {
            delete d;
        }
        CoUninitialize();
        return;
    }
    // Проход 1: встроенная миниатюра (EXIF) — показывается мгновенно
    IWICBitmapSource* thSrc = nullptr;
    if (SUCCEEDED(dec->GetThumbnail(&thSrc)) && thSrc) {
        UINT tw = 0, thh = 0;
        thSrc->GetSize(&tw, &thh);
        if (tw && thh && (uint64_t)tw * thh < 4000000 && !canceled()) {
            QvData* t1 = new QvData();
            t1->hwnd = d->hwnd;
            t1->gen = d->gen;
            t1->path = d->path;
            t1->isThumb = true;
            t1->w = tw;
            t1->h = thh;
            t1->bmp = QvSourceToBmp(thSrc, t1->error);
            PostMessageW(d->hwnd, WM_APP_QVREADY, (WPARAM)t1->gen, (LPARAM)t1);
        }
        thSrc->Release();
    }
    if (canceled()) {
        dec->Release();
        if (wic) wic->Release();
        CoUninitialize();
        delete d;
        return;
    }
    // Проход 2: полное превью (уменьшенный декод ≤768)
    std::wstring err;
    UINT w = 0, h = 0;
    d->bmp = QvDecodePreview(wic, dec, 768, w, h, err);
    d->w = w;
    d->h = h;
    d->error = err;
    dec->Release();
    if (wic) wic->Release();
    if (canceled()) {
        CoUninitialize();
        delete d;
        return;
    }
    PostMessageW(d->hwnd, WM_APP_QVREADY, (WPARAM)d->gen, (LPARAM)d);
    CoUninitialize();
}

void App::QvRememberCurrent() {
    if (!qvIsImage || !qvBmp || qvPath.empty()) return;
    for (auto it = qvCache.begin(); it != qvCache.end();) {
        if (it->path == qvPath) {
            if (it->bmp) DeleteObject(it->bmp);
            it = qvCache.erase(it);   // без «пустых» записей в кэше
        } else {
            ++it;
        }
    }
    qvCache.push_back({ qvPath, qvMtime, qvBmp, (UINT)qvImgW, (UINT)qvImgH });
    qvBmp = nullptr;
    if (qvCache.size() > 2) {
        DeleteObject(qvCache.front().bmp);
        qvCache.erase(qvCache.begin());
    }
}

void App::QuickViewInvalidate() {
    qvGen++;
    if (qvPending) {
        QvData* d = (QvData*)qvPending;
        {
            std::lock_guard<std::mutex> g(d->m);
            d->canceled = true;
        }
        qvPending = nullptr;
    }
    if (qvBmp) {
        DeleteObject(qvBmp);
        qvBmp = nullptr;
    }
    qvText.clear();
    qvInfo.clear();
    qvLoading = false;
    qvIsImage = false;
}

void App::ToggleQuickView() {
    if (quickView) {
        quickView = false;
        QuickViewInvalidate();
        for (auto& e : qvCache)
            if (e.bmp) DeleteObject(e.bmp);
        qvCache.clear();
        qvPath.clear();
        SetStatusText(L"Быстрый просмотр выключен");
    } else {
        quickView = true;
        QuickViewCursorMoved();
        SetStatusText(L"Быстрый просмотр: Ctrl+Q или Esc — выключить");
    }
    Redraw();
}

void App::QuickViewCursorMoved() {
    if (!quickView) return;
    Panel* ap = ActivePanel();
    std::wstring p = ap->CursorPath();
    if (p.empty()) {
        QuickViewInvalidate();
        qvPath.clear();
        qvInfo = L"Нет файла под курсором";
        Redraw();
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(LongPath(p).c_str(), GetFileExInfoStandard, &fad)) {
        QuickViewInvalidate();
        qvPath.clear();
        qvInfo = L"Не удалось получить данные файла";
        Redraw();
        return;
    }
    if (p == qvPath && fad.ftLastWriteTime.dwLowDateTime == qvMtime.dwLowDateTime &&
        fad.ftLastWriteTime.dwHighDateTime == qvMtime.dwHighDateTime)
        return;   // кэш актуален
    QvRememberCurrent();   // старый кадр — в LRU-кэш (мгновенный возврат)
    QuickViewInvalidate();
    qvPath = p;
    qvMtime = fad.ftLastWriteTime;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        qvInfo = L"Каталог\n\n" + p;
        Redraw();
        return;
    }
    IconId id = IconId::File;
    if (ap->snapshot && ap->cursor >= 0 && ap->cursor < (int)ap->snapshot->size())
        id = IconFor((*ap->snapshot)[ap->cursor]);
    if (id == IconId::Image) {
        // сначала — LRU-кэш: возврат на просмотренный файл мгновенен
        for (auto it = qvCache.begin(); it != qvCache.end();) {
            QvCacheEntry& e = *it;
            if (!e.bmp) {
                it = qvCache.erase(it);   // битые записи не оставляем
                continue;
            }
            if (e.path == p && e.mtime.dwLowDateTime == qvMtime.dwLowDateTime &&
                e.mtime.dwHighDateTime == qvMtime.dwHighDateTime) {
                qvBmp = e.bmp;
                qvImgW = (float)e.w;
                qvImgH = (float)e.h;
                qvIsImage = true;
                qvLoading = false;
                qvCache.erase(it);
                Redraw();
                return;
            }
            ++it;
        }
        qvLoading = true;
        qvIsImage = true;
        QvData* d = new QvData();
        d->hwnd = win->Hwnd();
        d->gen = qvGen;
        d->path = p;
        qvPending = d;
        std::thread(QvImageThread, d).detach();
        Redraw();
        return;
    }
    if (id == IconId::Text) {
        bool cut = false;
        QvReadText(p, qvText, cut);
        if (cut) qvText += L"\n… (файл показан не полностью)";
        Redraw();
        return;
    }
    qvInfo = QvFileInfo(p, fad, id);
    Redraw();
}

void App::OnQuickViewReady(uint64_t g, void* data) {
    std::unique_ptr<QvData> d((QvData*)data);
    if (g != qvGen || !quickView) return;   // устарело — просто удаляем
    if (d->isThumb) {
        // первый проход: EXIF-миниатюра — показываем мгновенно, полное превью следом
        if (d->bmp && qvLoading && !qvBmp) {
            qvBmp = d->bmp;
            d->bmp = nullptr;
            qvImgW = (float)d->w;
            qvImgH = (float)d->h;
            Redraw();
        }
        return;
    }
    qvPending = nullptr;
    if (d->bmp) {
        if (qvBmp) DeleteObject(qvBmp);   // заменяем миниатюру полным кадром
        qvBmp = d->bmp;
        d->bmp = nullptr;   // владение передано App
        qvImgW = (float)d->w;
        qvImgH = (float)d->h;
    } else {
        qvInfo = L"Ошибка: " + d->error;
    }
    qvLoading = false;
    Redraw();
}

void App::DrawQuickView(Renderer& r, const D2D1_RECT_F& area) {
    const Palette& pal = GetPalette(theme);
    float rowH = r.RowHeight();
    float headerH = rowH + 4;
    float statusH = rowH + 2;
    float rowsAreaH = area.bottom - area.top - headerH - statusH;
    if (rowsAreaH < rowH) return;

    r.Fill({ area.left, area.top, area.right, area.top + headerH }, pal.headerInactive);
    std::wstring title = L"Быстрый просмотр";
    if (!qvPath.empty()) title += L": " + PathFileName(qvPath);
    r.Text(title, { area.left + 6, area.top, area.right - 6, area.top + headerH },
           pal.headerTextInactive, false, true, true);
    D2D1_RECT_F body = { area.left, area.top + headerH, area.right,
                         area.top + headerH + rowsAreaH };
    r.Fill(body, pal.panelBg);

    float sy = area.top + headerH + rowsAreaH;
    r.Fill({ area.left, sy, area.right, area.bottom }, pal.statusBg);
    r.Text(L"Ctrl+Q или Esc — выключить",
           { area.left + 6, sy, area.right - 6, area.bottom }, pal.statusText, false, true, true);

    if (qvIsImage && qvBmp) {
        float availW = body.right - body.left - 28.0f;
        float availH = body.bottom - body.top - 28.0f;
        if (availW >= 8 && availH >= 8) {
            float s = std::min(availW / qvImgW, availH / qvImgH);
            if (s > 1.0f) s = 1.0f;
            float w = qvImgW * s, h = qvImgH * s;
            float x = body.left + (body.right - body.left - w) / 2;
            float y = body.top + (body.bottom - body.top - h) / 2;
            r.DrawImage(qvBmp, { x, y, x + w, y + h });
        }
        std::wstring cap = std::to_wstring((int)qvImgW) + L" × " +
                           std::to_wstring((int)qvImgH);
        r.Text(cap, { body.left + 8, body.bottom - rowH - 4, body.right - 8, body.bottom - 4 },
               pal.dimText, false, true, false);
        return;
    }
    if (qvLoading) {
        r.Text(L"Загрузка…", { body.left + 12, body.top + 4, body.right - 8, body.top + rowH },
               pal.dimText, false, true, false);
        return;
    }
    if (!qvText.empty()) {
        size_t pos = 0;
        float y = body.top + 2;
        float mrh = r.MonoRowHeight();
        while (pos < qvText.size() && y + mrh <= body.bottom) {
            size_t eol = qvText.find(L'\n', pos);
            std::wstring line =
                qvText.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            r.TextMono(line, { body.left + 8, y, body.right - 8, y + mrh }, pal.text);
            if (eol == std::wstring::npos) break;
            pos = eol + 1;
            y += mrh;
        }
        return;
    }
    if (!qvInfo.empty()) {
        size_t pos = 0;
        float y = body.top + 6;
        int lineNo = 0;
        while (pos < qvInfo.size() && y + rowH <= body.bottom) {
            size_t eol = qvInfo.find(L'\n', pos);
            std::wstring line =
                qvInfo.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            r.Text(line, { body.left + 12, y, body.right - 8, y + rowH },
                   lineNo == 0 ? pal.text : pal.dimText, false, true, false,
                   lineNo == 0 ? 600 : 400);
            if (eol == std::wstring::npos) break;
            pos = eol + 1;
            y += rowH;
            lineNo++;
        }
        return;
    }
}

void App::UpdateTitle() {
    if (!win) return;
    std::wstring t = L"Amber Commander — " + panels[0]->path + L"  |  " + panels[1]->path;
    SetWindowTextW(win->Hwnd(), t.c_str());
    Redraw();
}

void App::OnPanelLoaded(Panel* p) {
    (void)p;
    UpdateTitle();
    if (quickView && p == ActivePanel()) QuickViewCursorMoved();
}

void App::SwapPanels() {
    std::swap(panels[0], panels[1]);
    panels[0]->index = 0;
    panels[1]->index = 1;
    Redraw();
    if (quickView) QuickViewCursorMoved();
}

void App::GoToDrive(int side, const std::wstring& root) {
    Panel* p = panels[side];
    Panel* other = panels[1 - side];
    std::wstring target = root;
    // Double Commander: соседняя панель на этом же диске — открываем её каталог
    // и встаём курсором на тот же файл/папку
    if (other && !other->path.empty() &&
        WcsICmp(RootOf(other->path).c_str(), root.c_str()) == 0 &&
        PathNormalize(other->path) != root) {
        target = other->path;
        std::wstring cur = other->CursorPath();
        if (!cur.empty()) p->pendingReveal = PathFileName(cur);
    }
    p->SetPath(target);
    activePanel = side;
    Redraw();
}

void App::SetTheme(ThemeId t) {
    theme = t;
    cfg->theme = (int)t;
    Redraw();
}

void App::OnLoadedAsync(int idx, LoadMsg* msg) {
    if (idx < 0 || idx > 1) {
        if (msg) {
            delete msg->items;
            delete msg;
        }
        return;
    }
    if (msg) {
        panels[idx]->OnLoaded(msg->gen, msg->items);
        delete msg;
    }
}

void App::OnSizeReady(int idx, SizeMsg* msg) {
    if (idx < 0 || idx > 1 || !msg) {
        if (msg) {
            delete msg->items;
            delete msg;
        }
        return;
    }
    panels[idx]->OnSizeReady(msg->gen, msg->items);
    delete msg;
}

bool App::ShowContextMenu(int x, int y) {
    Panel* ap = ActivePanel();
    std::vector<std::wstring> paths;
    if (ap->sizeMode) {
        // режим размеров: системное меню для элемента под курсором
        std::wstring p = ap->SizeCursorPath();
        if (p.empty()) return false;
        paths.push_back(p);
    } else {
        paths = ap->SelectedPaths(true);
        if (paths.empty()) return false;
    }
    bool executed = ShowShellContextMenu(win->Hwnd(), x, y, paths);
    if (executed) {
        if (ap->sizeMode) ap->StartSizeScan();   // пересчитать размеры
        else {
            ActivePanel()->Refresh();
            PassivePanel()->Refresh();
        }
    }
    Redraw();
    return executed;
}

void App::SaveState() {
    if (!cfg) return;
    cfg->leftDir = panels[0]->path;
    cfg->rightDir = panels[1]->path;
    cfg->theme = (int)theme;
    if (win && win->Hwnd()) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(win->Hwnd(), &wp)) {
            cfg->winMax = (wp.showCmd == SW_SHOWMAXIMIZED) || IsZoomed(win->Hwnd());
            if (wp.showCmd != SW_SHOWMAXIMIZED) {
                RECT r = wp.rcNormalPosition;
                cfg->winX = r.left;
                cfg->winY = r.top;
                cfg->winW = r.right - r.left;
                cfg->winH = r.bottom - r.top;
            }
        }
    }
    SaveConfig(*cfg, GetIniPath());
}

void App::Shutdown() {
    if (copyCancelPtr) copyCancelPtr->store(true);
    if (searchCancel) searchCancel->store(true);
    quickView = false;
    QuickViewInvalidate();   // отменить фоновую загрузку и освободить bitmap
    SaveState();             // окно ещё живо — сохраняем состояние с геометрией
    if (win) DestroyWindow(win->Hwnd());
}

bool App::Run() {
    cfg = std::make_unique<Config>();
    LoadConfig(*cfg, GetIniPath());
    theme = (ThemeId)(cfg->theme >= 0 && cfg->theme <= 8 ? cfg->theme : 0);

    win = new MainWindow();
    if (!win->Create(this)) return false;
    win->R().SetFont(cfg->fontFace, cfg->fontSizePx);
    // ширина панелей всегда стартует с 50/50 (не сохраняется)

    panels[0] = new Panel();
    panels[1] = new Panel();
    panels[0]->Init(this, win->Hwnd(), 0);
    panels[1]->Init(this, win->Hwnd(), 1);
    panels[0]->SetPath(cfg->leftDir.empty() ? L"C:\\" : cfg->leftDir);
    panels[1]->SetPath(cfg->rightDir.empty() ? L"C:\\" : cfg->rightDir);
    RefreshDrives();
    StartFolderIndexBuild(cfg->folderIndexRefresh);   // фоновая сборка/актуализация индекса каталогов

    // восстановление геометрии окна (если она была сохранена и монитор на месте)
    if (cfg->winX != -1 && cfg->winY != -1) {
        POINT c{ cfg->winX + cfg->winW / 2, cfg->winY + cfg->winH / 2 };
        if (MonitorFromPoint(c, MONITOR_DEFAULTTONULL))
            SetWindowPos(win->Hwnd(), nullptr, cfg->winX, cfg->winY, cfg->winW, cfg->winH,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(win->Hwnd(), cfg->winMax ? SW_MAXIMIZE : SW_SHOW);
    UpdateWindow(win->Hwnd());

    // Alt+F1/F2 — приоритет Amber при фокусе на его окне (перехват до NVIDIA App и др.)
    g_hookApp = this;
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbProc, GetModuleHandleW(nullptr), 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    g_kbHook = nullptr;
    g_hookApp = nullptr;

    SaveState();
    return true;
}

void App::OnKey(WPARAM vk, DWORD flags, bool sys) {
    (void)flags;
    (void)sys;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // попап глобального поиска каталогов перехватывает навигацию
    if (qpOpen) {
        int n = (int)qpResults.size();
        const int rows = 12;
        auto syncTop = [&]() {
            if (qpSel < qpTop) qpTop = qpSel;
            if (qpSel >= qpTop + rows) qpTop = qpSel - rows + 1;
            if (qpTop < 0) qpTop = 0;
        };
        switch (vk) {
        case VK_UP:
            if (qpSel > 0) { qpSel--; syncTop(); Redraw(); }
            return;
        case VK_DOWN:
            if (qpSel < n - 1) { qpSel++; syncTop(); Redraw(); }
            return;
        case VK_PRIOR:
            qpSel = std::max(0, qpSel - rows); syncTop(); Redraw();
            return;
        case VK_NEXT:
            qpSel = std::min(n - 1, qpSel + rows); syncTop(); Redraw();
            return;
        case VK_HOME:
            qpSel = 0; qpTop = 0; Redraw();
            return;
        case VK_END:
            qpSel = n - 1; syncTop(); Redraw();
            return;
        case VK_ESCAPE:
            // попап закрывается, текст команды остаётся
            qpOpen = false;
            qpResults.clear();
            Redraw();
            return;
        case VK_RETURN:
            if (qpSel >= 0 && qpSel < n) ActivePanel()->SetPath(qpResults[qpSel]);
            qpOpen = false;
            qpResults.clear();
            ClearCmdLine();
            Redraw();
            return;
        case VK_TAB:
            qpOpen = false;
            qpResults.clear();
            break;   // Tab обработается как обычно
        default:
            if (vk < 0x20 || vk > 0x5A) {   // не буква — закрыть попап
                qpOpen = false;
                qpResults.clear();
                Redraw();
            }
            break;
        }
    }

    if (vk >= VK_F1 && vk <= VK_F12) {
        if (alt && vk == VK_F1) { Exec(Cmd::DriveLeft); return; }
        if (alt && vk == VK_F2) { Exec(Cmd::DriveRight); return; }
        if (alt && vk == VK_F7) { Exec(Cmd::Search); return; }
        if (alt && vk == VK_F4) { Exec(Cmd::Quit); return; }
        if (alt) return;
        switch (vk) {
        case VK_F1: Exec(Cmd::Help); return;
        case VK_F2: Exec(Cmd::Rename); return;
        case VK_F3: Exec(ctrl ? Cmd::SortName : Cmd::View); return;
        case VK_F4:
            if (shift) Exec(Cmd::NewFile);                  // Shift+F4 — новый файл
            else Exec(ctrl ? Cmd::SortExt : Cmd::Edit);
            return;
        case VK_F5: Exec(ctrl ? Cmd::SortDate : Cmd::Copy); return;
        case VK_F6: Exec(ctrl ? Cmd::SortSize : Cmd::Move); return;
        case VK_F7: Exec(Cmd::MkDir); return;
        case VK_F8: Exec(shift ? Cmd::DeleteNoConfirm : Cmd::Delete); return;
        case VK_F9: Exec(Cmd::Menu); return;
        case VK_F10: Exec(Cmd::Quit); return;
        case VK_F11: Exec(Cmd::SizeView); return;   // размеры каталогов (WizTree)
        case VK_F12:
            if (ctrl) Exec(Cmd::ThemeToggle);
            return;
        default: break;
        }
        if (shift && vk == VK_F4) { Exec(Cmd::NewFile); return; }
        return;
    }
    if (alt && vk == VK_RETURN) {
        // Alt+Enter — полный экран / восстановление (как в DC)
        if (win) win->ToggleMaximize();
        return;
    }
    if (alt) return; // Alt+буква обрабатывается через WM_SYSCHAR (быстрый поиск)

    switch (vk) {
    case VK_RETURN:
        if (!cmdLine.empty()) { ExecuteCommandLine(); return; }
        Exec(Cmd::Enter);
        return;
    case VK_TAB:
        Exec(Cmd::Tab);
        return;
    case VK_BACK:
        if (!cmdLine.empty()) {
            cmdLine.pop_back();
            Redraw();
            if (!cmdLine.empty()) ActivePanel()->JumpToPrefix(cmdLine);
            return;
        }
        Exec(Cmd::Up);
        return;
    case VK_ESCAPE:
        if (!cmdLine.empty()) {
            cmdLine.clear();
            Redraw();
            return;
        }
        if (quickView) {
            ToggleQuickView();
            return;
        }
        ActivePanel()->quick.clear();
        Redraw();
        return;
    case VK_INSERT:
        Exec(Cmd::SelToggle);
        return;
    case VK_SPACE:
        if (ctrl) { Exec(Cmd::UnselectAll); return; }   // Ctrl+Space — снять всё выделение
        Exec(Cmd::SelToggle);
        return;
    case VK_DELETE:
        Exec(Cmd::Delete);
        return;
    case VK_UP:
    case VK_DOWN: {
        Panel* p = ActivePanel();
        int d = vk == VK_UP ? -1 : 1;
        if (shift) {
            // как в Double Commander: строка под курсором выделяется тоже
            if (p->snapshot && p->cursor >= 0 && p->cursor < (int)p->snapshot->size()) {
                const DirEntry& e = (*p->snapshot)[p->cursor];
                if (e.name != L".." && !p->selected.count(e.name))
                    p->selected.insert(e.name);
            }
        }
        p->MoveCursor(d);
        if (shift) p->ToggleSelect();   // Shift+стрелки — выделение по мере движения
        Redraw();
        QuickViewCursorMoved();
        return;
    }
    case VK_LEFT:
        // Shift+← — сдвинуть разделитель панелей влево (левая уже)
        if (shift && win) { win->NudgeSplit(-1); return; }
        // Ctrl+← — история переходов назад (Alt+← занят Windows)
        if (ctrl && !shift) { Exec(Cmd::HistBack); return; }
        // ← — выход на уровень вверх (как в Double Commander)
        Exec(Cmd::Up);
        return;
    case VK_RIGHT:
        // Shift+→ — сдвинуть разделитель вправо (левая шире)
        if (shift && win) { win->NudgeSplit(1); return; }
        // Ctrl+→ — история переходов вперёд
        if (ctrl && !shift) { Exec(Cmd::HistFwd); return; }
        // → — вход в каталог / запуск файла
        Exec(Cmd::Enter);
        return;
    case VK_PRIOR:
        if (ctrl) { Exec(Cmd::Parent); return; }
        ActivePanel()->Page(-1);
        Redraw();
        QuickViewCursorMoved();
        return;
    case VK_NEXT:
        if (ctrl) { Exec(Cmd::Root); return; }
        ActivePanel()->Page(1);
        Redraw();
        QuickViewCursorMoved();
        return;
    case VK_HOME:
        ActivePanel()->Home();
        Redraw();
        QuickViewCursorMoved();
        return;
    case VK_END:
        ActivePanel()->End();
        Redraw();
        QuickViewCursorMoved();
        return;
    case VK_ADD:
        Exec(Cmd::SelMask);
        return;
    case VK_SUBTRACT:
        Exec(Cmd::UnselMask);
        return;
    case VK_MULTIPLY:
        Exec(Cmd::InvertSel);
        return;
    case 0x41: if (ctrl) { Exec(Cmd::SelAll); return; } break;       // Ctrl+A
    case 0x52: if (ctrl) { Exec(Cmd::Refresh); return; } break;      // Ctrl+R
    case 0x55: if (ctrl) { Exec(Cmd::Swap); return; } break;         // Ctrl+U
    case 0x4F: if (ctrl) { Exec(Cmd::PanelsToggle); return; } break; // Ctrl+O
    case 0x51: if (ctrl) { Exec(Cmd::QuickView); return; } break;    // Ctrl+Q — быстрый просмотр
    case 0xDC: if (ctrl) { Exec(Cmd::Root); return; } break;         // Ctrl+\
    case 0x4C: if (ctrl) { Exec(Cmd::SizeView); return; } break;     // Ctrl+L — размеры (WizTree)
    default: break;
    }
}

void App::OnChar(wchar_t ch) {
    if (ch < 0x20) return;
    cmdLine += ch;
    Redraw();
    // Живой поиск: по мере набора курсор перемещается на первый файл/каталог,
    // имя которого начинается с набранного текста.
    if (!cmdLine.empty()) ActivePanel()->JumpToPrefix(cmdLine);
    QuickViewCursorMoved();
    // Глобальный поиск каталогов: ≥2 символа — попап с вариантами на всех дисках.
    // Команды вида «cd …» попап не открывают (Enter должен выполнить команду).
    bool isCd = cmdLine.size() >= 3 && WcsICmp(cmdLine.substr(0, 3).c_str(), L"cd ") == 0;
    if (cmdLine.size() >= 2 && !isCd && FolderIndexBusy() && !FolderIndexReady())
        SetStatusText(L"Индекс каталогов собирается в фоне…");
    if (!isCd && cmdLine.size() >= 2 && FolderIndexReady()) {
        std::vector<std::wstring> res;
        size_t total = FolderIndexQuery(cmdLine, res, 60);
        if (total) {
            qpResults = std::move(res);
            qpSel = 0;
            qpTop = 0;
            qpOpen = true;
        } else {
            qpOpen = false;
            qpResults.clear();
            if (cmdLine.size() >= 2)
                SetStatusText(L"Каталоги не найдены: " + cmdLine);
        }
    } else {
        qpOpen = false;
        qpResults.clear();
    }
}

void App::OnSysChar(wchar_t ch) {
    if (ch < 0x20 || ch == L' ') return;
    ActivePanel()->QuickSearch(std::wstring(1, ch));
    Redraw();
    QuickViewCursorMoved();
}

bool App::Exec(Cmd c) {
    switch (c) {
    case Cmd::Up:
        ActivePanel()->GoUp();
        break;
    case Cmd::Root:
        ActivePanel()->GoRoot();
        break;
    case Cmd::Parent:
        ActivePanel()->GoUp();
        break;
    case Cmd::Enter:
        ActivePanel()->EnterCurrent();
        break;
    case Cmd::Tab:
        activePanel = 1 - activePanel;
        cmdLine.clear();   // набранный текст сбрасывается при переключении панелей
        Redraw();
        QuickViewCursorMoved();
        break;
    case Cmd::HistBack:
        ActivePanel()->HistoryBack();
        break;
    case Cmd::HistFwd:
        ActivePanel()->HistoryForward();
        break;
    case Cmd::QuickView:
        ToggleQuickView();
        break;
    case Cmd::SizeView: {
        // WizTree-вид: F11 — тумблер. Вход: пассивная панель показывает размеры
        // содержимого активной, фокус переходит на панель размеров.
        Panel* sizePanel = panels[0]->sizeMode ? panels[0]
                         : (panels[1]->sizeMode ? panels[1] : nullptr);
        if (sizePanel) {
            sizePanel->ExitSizeMode();
            break;
        }
        Panel* p = PassivePanel();
        p->EnterSizeMode(ActivePanel()->path);
        activePanel = 1 - activePanel;   // фокус на панель размеров
        Redraw();
        break;
    }
    case Cmd::Swap:
        SwapPanels();
        break;
    case Cmd::Refresh:
        ActivePanel()->Refresh();
        break;
    case Cmd::SelToggle:
        ActivePanel()->ToggleSelect();
        Redraw();
        break;
    case Cmd::SelAll:
        ActivePanel()->SelectAll(true);
        Redraw();
        break;
    case Cmd::InvertSel:
        ActivePanel()->InvertSel();
        Redraw();
        break;
    case Cmd::UnselectAll:
        ActivePanel()->SelectAll(false);
        Redraw();
        break;
    case Cmd::SelMask:
    case Cmd::UnselMask: {
        std::wstring m = L"*.*";
        std::wstring t = c == Cmd::SelMask ? L"Выделить по маске" : L"Снять выделение по маске";
        if (InputDialog(win->Hwnd(), theme, t, L"Маска:", m) == 1) {
            ActivePanel()->SelectMask(m, c == Cmd::SelMask);
            Redraw();
        }
        break;
    }
    case Cmd::SortName:
    case Cmd::SortExt:
    case Cmd::SortSize:
    case Cmd::SortDate: {
        int key = (int)c - (int)Cmd::SortName;
        Panel* p = ActivePanel();
        if (p->sortKey == key) p->sortAsc = !p->sortAsc;
        else {
            p->sortKey = key;
            p->sortAsc = true;
        }
        p->Resort();
        Redraw();
        break;
    }
    case Cmd::ThemeToggle:
        // цикл: Крем/Янтарь → Кофе/Какао → Холодная → Терминал → Сталь → Norton →
        // Спектр → Ночной циан → Уголь → …
        if (theme == ThemeId::Light) SetTheme(ThemeId::Dark);
        else if (theme == ThemeId::Dark) SetTheme(ThemeId::Cool);
        else if (theme == ThemeId::Cool) SetTheme(ThemeId::Green);
        else if (theme == ThemeId::Green) SetTheme(ThemeId::Gray);
        else if (theme == ThemeId::Gray) SetTheme(ThemeId::Norton);
        else if (theme == ThemeId::Norton) SetTheme(ThemeId::Vivid);
        else if (theme == ThemeId::Vivid) SetTheme(ThemeId::Night);
        else if (theme == ThemeId::Night) SetTheme(ThemeId::Coal);
        else SetTheme(ThemeId::Light);
        break;
    case Cmd::PanelsToggle:
        panelsHidden = !panelsHidden;
        Redraw();
        break;
    case Cmd::Quit:
        Shutdown();
        break;
    case Cmd::QuickClear:
        break;
    default:
        return ExecFileCommand(this, c);
    }
    return true;
}

void App::ExecuteCommandLine() {
    std::wstring line = cmdLine;
    cmdLine.clear();
    Redraw();
    Trim(line);
    if (line.empty()) return;

    auto startsI = [&](const wchar_t* p) {
        size_t n = wcslen(p);
        if (line.size() < n) return false;
        for (size_t i = 0; i < n; i++)
            if (LowerChar(line[i]) != LowerChar(p[i])) return false;
        return true;
    };

    if (startsI(L"cd")) {
        std::wstring rest = line.substr(2);
        Trim(rest);
        if (rest.empty()) return;
        if (rest.size() >= 2 && rest.front() == L'"' && rest.back() == L'"')
            rest = rest.substr(1, rest.size() - 2);
        std::wstring p = rest;
        if (p.size() >= 2 && p[1] == L':') p = PathNormalize(p);
        else p = PathJoin(ActivePanel()->path, p);
        if (DirExists(p)) ActivePanel()->SetPath(p);
        else SetStatusText(L"Каталог не найден: " + rest);
        return;
    }

    // Живой переход: набранное имя (точное или префикс) открывает файл/каталог.
    if (ActivePanel()->OpenByPrefixOrExact(line)) return;

    // Запуск как в «Выполнить» (Win+R): ShellExecute сам разрешает команды через
    // App Paths реестра, PATH, а также открывает документы, папки и URL.
    size_t sp = line.find_first_of(L" \t");
    std::wstring head = line.substr(0, sp);
    std::wstring args = (sp == std::wstring::npos) ? std::wstring() : line.substr(sp);
    Trim(args);
    HINSTANCE r = ShellExecuteW(win->Hwnd(), L"open", head.c_str(),
                                args.empty() ? nullptr : args.c_str(),
                                ActivePanel()->path.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        // Запасной вариант: встроенные консольные команды (dir, echo, set…)
        std::wstring cmd = L"cmd.exe /C " + line;
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           ActivePanel()->path.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            SetStatusText(L"Не удалось запустить: " + head);
        }
    }
}
