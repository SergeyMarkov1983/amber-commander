#include "viewer.h"
#include "dialogs.h"
#include "pathutil.h"
#include <objbase.h>
#include <wincodec.h>
#include <dwmapi.h>

// Просмотрщик F3: текст (ленивая загрузка до 64 МБ блоками по 1 МБ),
// HEX (адрес + 16 байт + ASCII, блоки по 64 КБ), изображения через WIC.
// Tab — текст/HEX, F4 — кодировка (ANSI/UTF-8/UTF-16), F7 — поиск, F3 — далее.

namespace {

constexpr UINT WM_APP_IMGREADY = WM_APP + 30;
constexpr uint64_t kTextCap = 64ull * 1024 * 1024;
constexpr uint64_t kTextBlock = 1ull * 1024 * 1024;
constexpr uint64_t kHexCap = 64ull * 1024 * 1024;
constexpr uint64_t kHexBlock = 64ull * 1024;

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

bool IsImageExt(const std::wstring& path) {
    std::wstring e = PathExtension(path);
    return e == L".png" || e == L".jpg" || e == L".jpeg" || e == L".gif" ||
           e == L".bmp" || e == L".ico" || e == L".webp";
}

// Определение бинарного файла: NUL-байты или >30% управляющих в первых 4 КБ.
bool LooksBinary(const std::wstring& path) {
    HANDLE h = CreateFileW(LongPath(path).c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char buf[4096];
    DWORD rd = 0;
    ReadFile(h, buf, sizeof(buf), &rd, nullptr);
    CloseHandle(h);
    if (rd == 0) return false;
    int ctrl = 0;
    for (DWORD i = 0; i < rd; i++) {
        unsigned char c = buf[i];
        if (c == 0) return true;                       // NUL — точно бинарный
        if (c < 0x09 || (c > 0x0D && c < 0x20)) ctrl++;
    }
    return ctrl * 100 / (int)rd > 30;
}

// IWICBitmapSource -> HBITMAP (32bpp, для GDI-отрисовки)
HBITMAP WicToHBitmap(IWICBitmapSource* src) {
    if (!src) return nullptr;
    UINT w = 0, h = 0;
    src->GetSize(&w, &h);
    if (!w || !h || w > 30000 || h > 30000) return nullptr;
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
    if (!bmp) return nullptr;
    HRESULT hr = src->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits);
    if (FAILED(hr)) {
        DeleteObject(bmp);
        return nullptr;
    }
    return bmp;
}

// ---------- Текстовый загрузчик (ленивая загрузка больших файлов) ----------

struct TextFile {
    HANDLE h = INVALID_HANDLE_VALUE;
    uint64_t size = 0;      // полный размер файла
    uint64_t consumed = 0;  // сколько байт прочитано
    bool eof = true;
    bool failed = false;
    std::wstring enc = L"ANSI";  // ANSI | UTF-8 | UTF-16
    bool be = false;              // UTF-16 big-endian
    std::vector<char> carry;      // неполный хвост многобайтовой последовательности
    std::wstring tail;            // незавершённая строка
    std::vector<std::wstring> lines;

    ~TextFile() { Reset(); }

    void Reset() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
        size = consumed = 0;
        eof = true;
        failed = false;
        be = false;
        carry.clear();
        tail.clear();
        lines.clear();
    }

    void SplitLines(const std::wstring& text) {
        for (wchar_t ch : text) {
            if (ch == L'\r') continue;
            if (ch == L'\n') {
                lines.push_back(std::move(tail));
                tail.clear();
            } else {
                tail += ch;
            }
        }
    }

    void Decode(const char* p, size_t n) {
        if (enc == L"UTF-16") {
            std::vector<char> buf(carry.begin(), carry.end());
            buf.insert(buf.end(), p, p + n);
            carry.clear();
            size_t pairs = buf.size() / 2;
            std::wstring text;
            text.reserve(pairs);
            for (size_t i = 0; i < pairs; i++) {
                unsigned char lo = (unsigned char)buf[i * 2];
                unsigned char hi = (unsigned char)buf[i * 2 + 1];
                text += be ? (wchar_t)((hi << 8) | lo) : (wchar_t)(lo | (hi << 8));
            }
            if (buf.size() % 2) carry.push_back(buf.back());
            SplitLines(text);
        } else if (enc == L"UTF-8") {
            std::vector<char> buf(carry.begin(), carry.end());
            buf.insert(buf.end(), p, p + n);
            carry.clear();
            size_t len = buf.size();
            int need = 0;
            while (len > 0) {
                need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf.data(),
                                           (int)len, nullptr, 0);
                if (need > 0) break;
                len--;
            }
            if (len == 0) return;  // целиком недопустимые байты — пропускаем
            if (len < buf.size()) carry.assign(buf.begin() + (ptrdiff_t)len, buf.end());
            std::wstring text((size_t)need, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)len, text.data(), need);
            SplitLines(text);
        } else {  // ANSI (CP1251) — однобайтовая, хвостов не бывает
            int need = MultiByteToWideChar(1251, 0, p, (int)n, nullptr, 0);
            std::wstring text((size_t)need, L'\0');
            if (need) MultiByteToWideChar(1251, 0, p, (int)n, text.data(), need);
            SplitLines(text);
        }
    }

    // Дозагрузка блоков, пока не набрано need строк (или конец файла/лимит).
    void EnsureLines(int need) {
        while ((int)lines.size() < need && !eof && h != INVALID_HANDLE_VALUE &&
               consumed < kTextCap) {
            uint64_t take = std::min<uint64_t>(kTextBlock, kTextCap - consumed);
            take = std::min<uint64_t>(take, size - consumed);
            std::vector<char> buf((size_t)take);
            DWORD rd = 0;
            if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, nullptr) || rd == 0) {
                eof = true;
                break;
            }
            consumed += rd;
            buf.resize(rd);
            Decode(buf.data(), buf.size());
            if (rd < (DWORD)take) eof = true;
        }
        if (consumed >= size && h != INVALID_HANDLE_VALUE) {
            eof = true;
            Finalize();
        }
    }

    void Finalize() {
        if (!carry.empty()) {
            std::vector<char> rest = std::move(carry);
            carry.clear();
            if (enc == L"UTF-8") {
                int need = MultiByteToWideChar(CP_UTF8, 0, rest.data(), (int)rest.size(),
                                               nullptr, 0);
                if (need > 0) {
                    std::wstring text((size_t)need, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, rest.data(), (int)rest.size(),
                                        text.data(), need);
                    SplitLines(text);
                }
            }
            // UTF-16: нечётный хвостовой байт — отбрасываем
        }
        if (!tail.empty()) {
            lines.push_back(std::move(tail));
            tail.clear();
        }
    }

    void Start(const std::wstring& path, const std::wstring& encOverride) {
        Reset();
        h = CreateFileW(LongPath(path).c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            failed = true;
            lines.push_back(L"Не удалось открыть файл.");
            return;
        }
        LARGE_INTEGER sz{};
        GetFileSizeEx(h, &sz);
        size = (uint64_t)sz.QuadPart;
        eof = size == 0;
        if (size == 0) return;

        uint64_t take = std::min<uint64_t>(kTextBlock, size);
        std::vector<char> buf((size_t)take);
        DWORD rd = 0;
        if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, nullptr) || rd == 0) {
            failed = true;
            eof = true;
            lines.push_back(L"Не удалось прочитать файл.");
            return;
        }
        consumed = rd;
        if (rd >= size) eof = true;

        size_t off = 0;
        if (!encOverride.empty()) {
            enc = encOverride;
            be = false;
            if (enc == L"UTF-16" && rd >= 2 && (unsigned char)buf[0] == 0xFF &&
                (unsigned char)buf[1] == 0xFE)
                off = 2;
            else if (enc == L"UTF-16" && rd >= 2 && (unsigned char)buf[0] == 0xFE &&
                     (unsigned char)buf[1] == 0xFF) {
                off = 2;
                be = true;
            } else if (enc == L"UTF-8" && rd >= 3 && (unsigned char)buf[0] == 0xEF &&
                       (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
                off = 3;
        } else {
            if (rd >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) {
                enc = L"UTF-16";
                off = 2;
            } else if (rd >= 2 && (unsigned char)buf[0] == 0xFE &&
                       (unsigned char)buf[1] == 0xFF) {
                enc = L"UTF-16";
                be = true;
                off = 2;
            } else if (rd >= 3 && (unsigned char)buf[0] == 0xEF &&
                       (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
                enc = L"UTF-8";
                off = 3;
            } else {
                int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf.data(),
                                               (int)rd, nullptr, 0);
                enc = need > 0 ? L"UTF-8" : L"ANSI";
            }
        }
        Decode(buf.data() + off, buf.size() - off);
        if (eof) Finalize();
    }

    bool LoadedAll() const { return consumed >= size; }
    std::wstring EncLabel() const {
        if (enc == L"UTF-16") return be ? L"UTF-16BE" : L"UTF-16LE";
        if (enc == L"UTF-8") return L"UTF-8";
        return L"ANSI (CP1251)";
    }
};

// ---------- HEX-загрузчик ----------

struct HexFile {
    HANDLE h = INVALID_HANDLE_VALUE;
    uint64_t size = 0;
    uint64_t loaded = 0;
    std::vector<unsigned char> raw;
    bool failed = false;

    ~HexFile() { Reset(); }

    void Reset() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
        size = loaded = 0;
        raw.clear();
        failed = false;
    }

    void Start(const std::wstring& path) {
        Reset();
        h = CreateFileW(LongPath(path).c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            failed = true;
            return;
        }
        LARGE_INTEGER sz{};
        GetFileSizeEx(h, &sz);
        size = (uint64_t)sz.QuadPart;
    }

    void Ensure(uint64_t needBytes) {
        uint64_t need = std::min<uint64_t>(needBytes, std::min<uint64_t>(kHexCap, size));
        while (loaded < need && h != INVALID_HANDLE_VALUE) {
            uint64_t take = std::min<uint64_t>(kHexBlock, need - loaded);
            size_t old = raw.size();
            raw.resize(old + (size_t)take);
            DWORD rd = 0;
            if (!ReadFile(h, raw.data() + old, (DWORD)take, &rd, nullptr) || rd == 0) break;
            loaded += rd;
            raw.resize(old + rd);
        }
    }

    bool LoadedAll() const { return loaded >= size; }
};

// ---------- Декодирование изображения (фоновый поток) ----------

struct ImgDecode {
    HWND hwnd = nullptr;
    std::mutex m;
    bool done = false;
    bool canceled = false;
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    UINT w = 0, h = 0;
    std::wstring error;

    ~ImgDecode() {
        SafeRelease(conv);
        SafeRelease(frame);
        SafeRelease(dec);
        SafeRelease(wic);
    }
};

void DecodeImageThread(std::wstring path, ImgDecode* sd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* wic = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wic));
    if (FAILED(hr) || !wic) {
        sd->error = L"Не удалось создать WIC";
        goto commit;
    }
    sd->wic = wic;
    {
        IWICBitmapDecoder* dec = nullptr;
        hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &dec);
        if (FAILED(hr) || !dec) {
            sd->error = L"Не удалось декодировать изображение";
            goto commit;
        }
        sd->dec = dec;
        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(dec->GetFrame(0, &frame)) || !frame) {
            sd->error = L"В изображении нет кадров";
            goto commit;
        }
        sd->frame = frame;
        IWICFormatConverter* conv = nullptr;
        if (FAILED(wic->CreateFormatConverter(&conv)) || !conv) {
            sd->error = L"Не удалось создать конвертер";
            goto commit;
        }
        sd->conv = conv;
        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                              nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            sd->error = L"Формат изображения не поддерживается";
            goto commit;
        }
        conv->GetSize(&sd->w, &sd->h);
        if (sd->w == 0 || sd->h == 0 || sd->w > 30000 || sd->h > 30000 ||
            (uint64_t)sd->w * sd->h > 250000000ull)
            sd->error = L"Изображение слишком велико";
    }
commit:;
    HWND hw = sd->hwnd;
    bool del = false;
    {
        std::lock_guard<std::mutex> g(sd->m);
        if (sd->canceled) del = true;
        else sd->done = true;
    }
    if (del) {
        // окно закрыто: удаляем сами
        CoUninitialize();
        delete sd;
        return;
    }
    // Если окно уже исчезло — сообщение потеряется, а sd удалит WM_NCDESTROY (done=true).
    PostMessageW(hw, WM_APP_IMGREADY, 0, (LPARAM)sd);
    CoUninitialize();
}

// ---------- Состояние просмотрщика ----------

struct ViewerState {
    HWND hwnd = nullptr;
    Renderer r;
    std::wstring path;
    ThemeId theme = ThemeId::Light;

    enum class Mode { Text, Hex, Image };
    Mode mode = Mode::Text;

    // текст
    TextFile tf;
    int top = 0;
    int hl = -1;
    std::wstring find;

    // HEX
    HexFile hf;
    int hexTop = 0;

    // изображение
    IWICImagingFactory* imgWic = nullptr;
    IWICBitmapDecoder* imgDec = nullptr;
    IWICBitmapFrameDecode* imgFrame = nullptr;
    IWICFormatConverter* imgConv = nullptr;
    HBITMAP imgBmp = nullptr;
    float imgW = 0, imgH = 0;
    float panX = 0, panY = 0;
    bool imgDecoding = false;
    bool imgFailed = false;
    std::wstring imgError;
    ImgDecode* imgPending = nullptr;

    bool ContainsI(const std::wstring& hay, const std::wstring& needle) const {
        if (needle.size() > hay.size()) return false;
        for (size_t i = 0; i + needle.size() <= hay.size(); i++) {
            bool ok = true;
            for (size_t j = 0; j < needle.size(); j++) {
                if (LowerChar(hay[i + j]) != LowerChar(needle[j])) {
                    ok = false;
                    break;
                }
            }
            if (ok) return true;
        }
        return false;
    }

    void FindNext(bool fromTop) {
        if (find.empty() || tf.lines.empty()) return;
        int start = fromTop ? top : hl + 1;
        if (start < 0) start = 0;
        for (int i = start; i < (int)tf.lines.size(); i++) {
            if (ContainsI(tf.lines[i], find)) {
                hl = i;
                top = std::max(0, i - 5);
                return;
            }
        }
        hl = -1;
    }

    void ReleaseImage() {
        if (imgBmp) {
            DeleteObject(imgBmp);
            imgBmp = nullptr;
        }
        SafeRelease(imgConv);
        SafeRelease(imgFrame);
        SafeRelease(imgDec);
        SafeRelease(imgWic);
        imgW = imgH = 0;
        panX = panY = 0;
    }

    void MakeImageBitmap() {
        if (imgBmp) {
            DeleteObject(imgBmp);
            imgBmp = nullptr;
        }
        if (imgConv) imgBmp = WicToHBitmap(imgConv);
        if (!imgBmp) {
            imgFailed = true;
            imgError = L"Не удалось создать растровое изображение";
        }
    }

    void UpdateTitle() {
        std::wstring t = L"Просмотр — " + PathFileName(path);
        t += mode == Mode::Hex ? L" [HEX]"
           : mode == Mode::Image ? L" [изображение]"
                                 : L" [текст]";
        SetWindowTextW(hwnd, t.c_str());
    }

    void SwitchTab() {
        if (mode == Mode::Hex) {
            mode = Mode::Text;
        } else {
            if (hf.h == INVALID_HANDLE_VALUE) hf.Start(path);
            mode = Mode::Hex;
        }
        UpdateTitle();
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
    }

    void SwitchEnc() {
        if (mode != Mode::Text) {
            mode = Mode::Text;
            UpdateTitle();
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
            return;
        }
        std::wstring e = tf.enc == L"ANSI" ? L"UTF-8"
                       : tf.enc == L"UTF-8" ? L"UTF-16"
                                            : L"ANSI";
        tf.Start(path, e);
        top = 0;
        hl = -1;
        find.clear();
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE);
    }

    void DrawTextMode(const Palette& pal) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        float W = (float)rc.right, H = (float)rc.bottom;
        float rowH = r.MonoRowHeight();
        float statusH = rowH + 6;
        int rows = (int)((H - statusH) / rowH);
        if (rows < 1) rows = 1;
        tf.EnsureLines(top + rows + 30);
        int maxTop = std::max(0, (int)tf.lines.size() - rows);
        if (top > maxTop) top = maxTop;
        if (top < 0) top = 0;
        for (int i = top; i < (int)tf.lines.size() && i < top + rows; i++) {
            float y = (float)(i - top) * rowH;
            if (i == hl) r.Fill({ 0, y, W, y + rowH }, pal.selBg);
            r.TextMono(tf.lines[i], { 6, y, W - 6, y + rowH }, pal.text);
        }
        std::wstring st = L"строк: " + std::to_wstring(tf.lines.size()) +
                          (tf.LoadedAll() ? L"" : L"…") + L" | " + tf.EncLabel() +
                          L" | F7 поиск | F3 далее | F4 кодировка | Tab HEX | Esc выход | строка " +
                          std::to_wstring(top + 1);
        r.Fill({ 0, H - statusH, W, H }, pal.statusBg);
        r.Text(st, { 8, H - statusH, W - 8, H }, pal.statusText, false, true, false);
    }

    void DrawHexMode(const Palette& pal) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        float W = (float)rc.right, H = (float)rc.bottom;
        float rowH = r.MonoRowHeight();
        float statusH = rowH + 6;
        int rows = (int)((H - statusH) / rowH);
        if (rows < 1) rows = 1;
        if (!hf.failed) {
            hf.Ensure((uint64_t)(hexTop + rows) * 16);
            uint64_t totalRows = (std::min<uint64_t>(hf.size, kHexCap) + 15) / 16;
            int maxTop = (int)std::max(0ll, (int64_t)totalRows - rows);
            if (hexTop > maxTop) hexTop = maxTop;
            if (hexTop < 0) hexTop = 0;
            wchar_t buf[256];
            for (int i = hexTop; i < hexTop + rows; i++) {
                uint64_t base = (uint64_t)i * 16;
                if (base >= hf.loaded) break;
                float y = (float)(i - hexTop) * rowH;
                uint64_t rem = hf.loaded - base;
                int off = swprintf(buf, 256, L"%08llX  ", (unsigned long long)base);
                // левая широкая панель: байты в системной кодировке (CP1251),
                // по одному символу в ячейке из 3 (непечатные — ·)
                for (int b = 0; b < 16; b++) {
                    if ((uint64_t)b >= rem) {
                        off += swprintf(buf + off, 256 - off, L"   ");
                        continue;
                    }
                    unsigned char c = hf.raw[(size_t)(base + b)];
                    wchar_t ch;
                    if (c >= 0x20) {
                        wchar_t wch = 0;
                        if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                                                (LPCCH)&c, 1, &wch, 1) == 1)
                            ch = wch;
                        else
                            ch = L'·';
                    } else {
                        ch = L'·';
                    }
                    off += swprintf(buf + off, 256 - off, L"%c  ", ch);
                }
                off += swprintf(buf + off, 256 - off, L"│ ");
                // правая панель: HEX-коды
                for (int b = 0; b < 16; b++) {
                    if ((uint64_t)b < rem)
                        off += swprintf(buf + off, 256 - off, L"%02X ",
                                        hf.raw[(size_t)(base + b)]);
                    else
                        off += swprintf(buf + off, 256 - off, L"   ");
                }
                buf[off] = 0;
                r.TextMono(std::wstring(buf, off), { 6, y, W - 6, y + rowH }, pal.text);
            }
        } else {
            r.Text(L"Не удалось открыть файл.", { 12, 12, W - 12, 40 }, pal.error, false, true, false);
        }
        wchar_t off[32];
        swprintf(off, 32, L"0x%llX", (unsigned long long)((uint64_t)std::max(0, hexTop) * 16));
        std::wstring st = L"HEX | смещение " + std::wstring(off) + L" | размер " +
                          FormatSize(hf.size) + (hf.LoadedAll() ? L"" : L"…") +
                          L" | Tab текст | Esc выход";
        r.Fill({ 0, H - statusH, W, H }, pal.statusBg);
        r.Text(st, { 8, H - statusH, W - 8, H }, pal.statusText, false, true, false);
    }

    void DrawImageMode(const Palette& pal) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        float W = (float)rc.right, H = (float)rc.bottom;
        float rowH = r.MonoRowHeight();
        float statusH = rowH + 6;
        float dh = H - statusH;
        if (imgDecoding) {
            r.Text(L"Декодирование изображения…", { 0, 0, W, dh }, pal.text, false, true, false);
        } else if (imgFailed) {
            r.Text(imgError, { 12, 0, W - 12, dh }, pal.error, false, true, false);
        } else if (imgBmp) {
            float iw = (float)imgW, ih = (float)imgH;
            float scale = std::min(W / iw, dh / ih);
            if (scale > 1.0f) scale = 1.0f;  // маленькие не растягиваем
            float dw = iw * scale, dhh = ih * scale;
            float ox, oy;
            if (dw > W) {
                panX = std::clamp(panX, W - dw, 0.0f);
                ox = panX;
            } else {
                panX = 0;
                ox = (W - dw) / 2;
            }
            if (dhh > dh) {
                panY = std::clamp(panY, dh - dhh, 0.0f);
                oy = panY;
            } else {
                panY = 0;
                oy = (dh - dhh) / 2;
            }
            r.DrawImage(imgBmp, { ox, oy, ox + dw, oy + dhh });
        }
        std::wstring st;
        if (imgDecoding) st = L"Декодирование изображения…";
        else if (imgFailed) st = L"Изображение | Tab — текст | Esc — выход";
        else
            st = L"Изображение " + std::to_wstring((int)imgW) + L"×" +
                 std::to_wstring((int)imgH) +
                 L" | ←→↑↓ — прокрутка | Tab — текст | Esc — выход";
        r.Fill({ 0, H - statusH, W, H }, pal.statusBg);
        r.Text(st, { 8, H - statusH, W - 8, H }, pal.statusText, false, true, false);
    }

    void DrawAll(HDC hdc, const RECT& rcPaint) {
        const Palette& pal = GetPalette(theme);
        r.BeginDraw(hdc, rcPaint, pal.panelBg);
        switch (mode) {
        case Mode::Hex: DrawHexMode(pal); break;
        case Mode::Image: DrawImageMode(pal); break;
        default: DrawTextMode(pal); break;
        }
        r.EndDraw();
    }

    void Scroll(int delta) {
        switch (mode) {
        case Mode::Hex:
            hexTop += delta;
            if (hexTop < 0) hexTop = 0;
            break;
        case Mode::Image:
            panY -= (float)delta * 24.0f;
            break;
        default:
            top += delta;
            if (top < 0) top = 0;
            break;
        }
    }

    void ScrollPage(int delta) {
        switch (mode) {
        case Mode::Hex: hexTop += delta * 20; if (hexTop < 0) hexTop = 0; break;
        case Mode::Image: panY -= (float)delta * 200.0f; break;
        default: top += delta * 20; if (top < 0) top = 0; break;
        }
    }

    void Pan(int dx, int dy) {
        panX += (float)dx;
        panY += (float)dy;
    }

    void ToHome() {
        if (mode == Mode::Hex) hexTop = 0;
        else top = 0;
    }

    void ToEnd() {
        switch (mode) {
        case Mode::Hex: {
            uint64_t totalRows = (std::min<uint64_t>(hf.size, kHexCap) + 15) / 16;
            hexTop = (int)totalRows;
            break;
        }
        case Mode::Image:
            panY = -1000000.0f;  // зажмётся в DrawImageMode
            break;
        default:
            tf.EnsureLines(1000000000);
            top = (int)tf.lines.size();
            break;
        }
    }
};

// ---------- Оконная процедура ----------

LRESULT CALLBACK ViewerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    ViewerState* v = (ViewerState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        v = (ViewerState*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)v);
        if (v) v->hwnd = h;
    }
    if (!v) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (v->r.IsReady()) v->DrawAll(ps.hdc, ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        v->r.Resize(LOWORD(l), HIWORD(l));
        if (v->mode == ViewerState::Mode::Image && v->imgConv) v->MakeImageBitmap();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (m == WM_SYSKEYDOWN) {
            // Alt+F4 — закрыть просмотрщик (не передавать в DefWindowProc —
            // это раньше приводило к аварийному завершению программы)
            if (w == VK_F4) DestroyWindow(h);
            return 0;
        }
        switch (w) {
        case VK_ESCAPE:
        case VK_F10:
            DestroyWindow(h);
            return 0;
        case VK_TAB:
            v->SwitchTab();
            return 0;
        case VK_F4:
            v->SwitchEnc();
            return 0;
        case VK_F7:
            if (v->mode == ViewerState::Mode::Text) {
                std::wstring q;
                if (InputDialog(h, v->theme, L"Поиск в файле", L"Текст:", q) == 1 && !q.empty()) {
                    v->find = q;
                    v->FindNext(true);
                }
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case VK_F3:
            if (v->mode == ViewerState::Mode::Text) {
                v->FindNext(true);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case VK_UP:
            v->Scroll(-1);
            break;
        case VK_DOWN:
            v->Scroll(1);
            break;
        case VK_PRIOR:
            v->ScrollPage(-1);
            break;
        case VK_NEXT:
            v->ScrollPage(1);
            break;
        case VK_HOME:
            v->ToHome();
            break;
        case VK_END:
            v->ToEnd();
            break;
        case VK_LEFT:
            if (v->mode == ViewerState::Mode::Image) v->Pan(40, 0);
            break;
        case VK_RIGHT:
            if (v->mode == ViewerState::Mode::Image) v->Pan(-40, 0);
            break;
        default:
            return DefWindowProcW(h, m, w, l);
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        short d = GET_WHEEL_DELTA_WPARAM(w);
        int steps = d / WHEEL_DELTA;
        if (v->mode == ViewerState::Mode::Image) v->Pan(0, (float)(steps * 40));
        else v->Scroll(-steps * 3);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_APP_IMGREADY: {
        ImgDecode* sd = (ImgDecode*)l;
        if (sd && v->imgPending == sd) {
            v->imgW = (float)sd->w;
            v->imgH = (float)sd->h;
            if (!sd->error.empty()) {
                v->imgFailed = true;
                v->imgError = sd->error;
            } else if (v->mode == ViewerState::Mode::Image) {
                v->imgWic = sd->wic;
                v->imgDec = sd->dec;
                v->imgFrame = sd->frame;
                v->imgConv = sd->conv;
                sd->wic = nullptr;
                sd->dec = nullptr;
                sd->frame = nullptr;
                sd->conv = nullptr;
                v->MakeImageBitmap();
            }
            v->imgPending = nullptr;
            v->imgDecoding = false;
            delete sd;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_NCDESTROY: {
        if (v->imgPending) {
            ImgDecode* sd = v->imgPending;
            ImgDecode* toDelete = nullptr;
            {
                std::lock_guard<std::mutex> g(sd->m);
                sd->canceled = true;
                if (sd->done) toDelete = sd;  // поток завершил commit и больше не трогает sd
            }
            if (toDelete) delete toDelete;
            v->imgPending = nullptr;
        }
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete v;
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

void ShowViewer(HWND parent, ThemeId theme, const std::wstring& path) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = ViewerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.hIconSm = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"AmberViewer";
        RegisterClassExW(&wc);
        registered = true;
    }
    auto* v = new ViewerState();
    v->path = path;
    v->theme = theme;

    bool isImage = !DirExists(path) && IsImageExt(path);
    bool isBinary = false;
    if (!isImage && !DirExists(path)) isBinary = LooksBinary(path);
    if (isImage) {
        v->mode = ViewerState::Mode::Image;
        v->imgDecoding = true;
    } else if (isBinary) {
        // бинарный файл (dll/exe/…) — сразу HEX с двумя панелями
        v->mode = ViewerState::Mode::Hex;
        v->hf.Start(path);
    } else {
        v->tf.Start(path, L"");
    }

    int w = 1000, h = 700;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT pr{};
    if (parent && GetWindowRect(parent, &pr)) {
        x = pr.left + (pr.right - pr.left - w) / 2;
        y = pr.top + (pr.bottom - pr.top - h) / 2;
    }
    std::wstring t = L"Просмотр — " + PathFileName(path) +
                     (isImage ? L" [изображение]" : isBinary ? L" [HEX]" : L" [текст]");
    HWND hv = CreateWindowExW(0, L"AmberViewer", t.c_str(), WS_OVERLAPPEDWINDOW, x, y, w, h,
                              parent, nullptr, GetModuleHandleW(nullptr), v);
    if (!hv) {
        delete v;
        return;
    }
    v->r.Init(hv);

    // тёплая системная шапка просмотрщика (Win11)
    {
        const Palette& pal = GetPalette(theme);
        COLORREF cap = PalRef(pal.headerInactive);
        COLORREF txt = PalRef(pal.headerText);
        COLORREF brd = PalRef(pal.border);
        DwmSetWindowAttribute(hv, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
        DwmSetWindowAttribute(hv, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
        DwmSetWindowAttribute(hv, 34 /*DWMWA_BORDER_COLOR*/, &brd, sizeof(brd));
    }

    if (isImage) {
        auto* sd = new ImgDecode();
        sd->hwnd = hv;
        v->imgPending = sd;
        std::thread([path, sd]() { DecodeImageThread(path, sd); }).detach();
    }

    ShowWindow(hv, SW_SHOW);
}
