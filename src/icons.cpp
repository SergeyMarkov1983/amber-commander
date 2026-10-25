#include "icons.h"
#include "pathutil.h"

// Набор иконок в едином тёплом стиле (GDI-отрисовка).
// Дизайн агента C (сетка 16×16, мягкие скругления, контур + заливка,
// тёплые тени и кремовые блики) перенесён на примитивы GDI.

IconId IconFor(const DirEntry& e) {
    if (e.isDir) return IconId::Folder;
    std::wstring ext = PathExtension(e.name);
    static const std::unordered_map<std::wstring, IconId> map = {
        { L".exe", IconId::Exe }, { L".com", IconId::Exe }, { L".bat", IconId::Exe },
        { L".cmd", IconId::Exe }, { L".msi", IconId::Exe }, { L".scr", IconId::Exe },
        { L".txt", IconId::Text }, { L".ini", IconId::Text }, { L".log", IconId::Text },
        { L".md", IconId::Text }, { L".doc", IconId::Text }, { L".docx", IconId::Text },
        { L".rtf", IconId::Text }, { L".cfg", IconId::Text }, { L".json", IconId::Text },
        { L".xml", IconId::Text }, { L".html", IconId::Text }, { L".htm", IconId::Text },
        { L".css", IconId::Text }, { L".js", IconId::Text }, { L".ts", IconId::Text },
        { L".c", IconId::Text }, { L".cpp", IconId::Text }, { L".h", IconId::Text },
        { L".py", IconId::Text }, { L".cs", IconId::Text }, { L".java", IconId::Text },
        { L".rs", IconId::Text }, { L".go", IconId::Text }, { L".php", IconId::Text },
        { L".sh", IconId::Text }, { L".ps1", IconId::Text }, { L".sql", IconId::Text },
        { L".yaml", IconId::Text }, { L".yml", IconId::Text }, { L".csv", IconId::Text },
        { L".pdf", IconId::Text }, { L".xls", IconId::Text }, { L".xlsx", IconId::Text },
        { L".ppt", IconId::Text }, { L".pptx", IconId::Text },
        { L".png", IconId::Image }, { L".jpg", IconId::Image }, { L".jpeg", IconId::Image },
        { L".gif", IconId::Image }, { L".bmp", IconId::Image }, { L".ico", IconId::Image },
        { L".webp", IconId::Image }, { L".svg", IconId::Image }, { L".tif", IconId::Image },
        { L".jfif", IconId::Image }, { L".avif", IconId::Image }, { L".psd", IconId::Image },
        { L".mp4", IconId::Video }, { L".mkv", IconId::Video }, { L".avi", IconId::Video },
        { L".mov", IconId::Video }, { L".wmv", IconId::Video }, { L".webm", IconId::Video },
        { L".mpg", IconId::Video }, { L".mpeg", IconId::Video }, { L".flv", IconId::Video },
        { L".m4v", IconId::Video }, { L".3gp", IconId::Video },
        { L".mp3", IconId::Audio }, { L".wav", IconId::Audio }, { L".flac", IconId::Audio },
        { L".ogg", IconId::Audio }, { L".m4a", IconId::Audio }, { L".wma", IconId::Audio },
        { L".mid", IconId::Audio }, { L".aac", IconId::Audio }, { L".ape", IconId::Audio },
        { L".opus", IconId::Audio },
        { L".zip", IconId::Archive }, { L".rar", IconId::Archive }, { L".7z", IconId::Archive },
        { L".tar", IconId::Archive }, { L".gz", IconId::Archive }, { L".bz2", IconId::Archive },
        { L".xz", IconId::Archive }, { L".iso", IconId::Archive }, { L".tgz", IconId::Archive },
        { L".jar", IconId::Archive }, { L".zst", IconId::Archive },
        { L".ttf", IconId::Font }, { L".otf", IconId::Font }, { L".woff", IconId::Font },
        { L".woff2", IconId::Font },
        { L".dll", IconId::Sys }, { L".sys", IconId::Sys }, { L".drv", IconId::Sys },
        { L".ocx", IconId::Sys }, { L".cpl", IconId::Sys },
        { L".lnk", IconId::Link }, { L".url", IconId::Link },
    };
    auto it = map.find(ext);
    return it == map.end() ? IconId::File : it->second;
}

namespace {

// Страница-лист с загнутым уголком (общая основа для файловых иконок)
void Page(Renderer& r, const D2D1_RECT_F& rc, const D2D1_COLOR_F& fill,
          const D2D1_COLOR_F& outline) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    float f = w * 0.18f; // размер сгиба
    r.RoundRect({ x + 1, y + 1, x + w - 1, y + h - 1 }, w * 0.16f, fill);
    // загнутый уголок
    D2D1_POINT_2F fold[3] = { { x + w - f - 1, y + 1 }, { x + w - f - 1, y + 1 + f },
                              { x + w - 1, y + 1 + f } };
    r.Polygon(fold, 3, outline);
    r.Rect({ x + 1, y + 1, x + w - 1, y + h - 1 }, outline, 1.0f);
}

void FolderIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    // тень-подложка
    r.RoundRect({ x, y + h * 0.22f, x + w, y + h }, w * 0.14f, pal.iconShadow);
    // язычок
    r.RoundRect({ x + 1, y + h * 0.30f, x + w * 0.55f, y + h * 0.52f }, w * 0.10f,
                pal.iconFolderTab);
    // корпус
    r.RoundRect({ x + 1, y + h * 0.28f, x + w - 1, y + h - 1 }, w * 0.13f, pal.iconFolder);
    // блик
    r.Line(x + w * 0.2f, y + h * 0.62f, x + w * 0.5f, y + h * 0.62f, pal.iconHighlight, 1.0f);
    r.Rect({ x + 1, y + h * 0.28f, x + w - 1, y + h - 1 }, pal.iconOutline, 1.0f);
}

void UpIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    // бейдж
    r.RoundRect({ x + 1, y + h * 0.05f, x + w - 1, y + h * 0.95f }, w * 0.2f, pal.iconFolderTab);
    r.Rect({ x + 1, y + h * 0.05f, x + w - 1, y + h * 0.95f }, pal.iconOutline, 1.0f);
    // стрелка
    D2D1_POINT_2F a[3] = { { x + w * 0.5f, y + h * 0.22f }, { x + w * 0.28f, y + h * 0.52f },
                           { x + w * 0.72f, y + h * 0.52f } };
    r.Polygon(a, 3, pal.iconHighlight);
    r.Fill({ x + w * 0.36f, y + h * 0.56f, x + w * 0.64f, y + h * 0.78f }, pal.iconHighlight);
}

void DriveIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    r.RoundRect({ x + w * 0.12f, y + h * 0.06f, x + w * 0.88f, y + h * 0.84f }, w * 0.10f,
                pal.iconDrive);
    r.Rect({ x + w * 0.12f, y + h * 0.06f, x + w * 0.88f, y + h * 0.84f }, pal.iconOutline, 1.0f);
    // светодиод
    r.Ellipse({ x + w * 0.30f, y + h * 0.62f, x + w * 0.44f, y + h * 0.76f }, pal.accent);
    // слот
    r.Line(x + w * 0.52f, y + h * 0.66f, x + w * 0.80f, y + h * 0.66f, pal.iconOutline, 1.0f);
}

void ExeIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    r.RoundRect({ x + 1, y + 1, x + w - 1, y + h - 1 }, w * 0.16f, pal.iconExe);
    r.Rect({ x + 1, y + 1, x + w - 1, y + h - 1 }, pal.iconOutline, 1.0f);
    r.Fill({ x + w * 0.22f, y + h * 0.22f, x + w * 0.78f, y + h * 0.36f }, pal.iconHighlight);
    r.Fill({ x + w * 0.22f, y + h * 0.44f, x + w * 0.78f, y + h * 0.76f }, pal.iconHighlight);
    r.Line(x + w * 0.26f, y + h * 0.52f, x + w * 0.74f, y + h * 0.52f, pal.iconExe, 1.0f);
    r.Line(x + w * 0.26f, y + h * 0.62f, x + w * 0.66f, y + h * 0.62f, pal.iconExe, 1.0f);
}

void TextIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    Page(r, rc, pal.iconText, pal.iconOutline);
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    for (int i = 0; i < 3; i++) {
        float yy = y + h * (0.34f + 0.15f * i);
        r.Fill({ x + w * 0.26f, yy, x + w * (i == 2 ? 0.58f : 0.74f), yy + h * 0.05f },
               pal.iconInk);
    }
}

void ImageIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    Page(r, rc, pal.iconImage, pal.iconOutline);
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    r.Ellipse({ x + w * 0.62f, y + h * 0.24f, x + w * 0.76f, y + h * 0.38f }, pal.iconHighlight);
    D2D1_POINT_2F m[3] = { { x + w * 0.24f, y + h * 0.72f }, { x + w * 0.48f, y + h * 0.40f },
                           { x + w * 0.74f, y + h * 0.72f } };
    r.Polygon(m, 3, pal.iconHighlight);
}

void VideoIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    Page(r, rc, pal.iconVideo, pal.iconOutline);
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    D2D1_POINT_2F t[3] = { { x + w * 0.38f, y + h * 0.30f }, { x + w * 0.38f, y + h * 0.70f },
                           { x + w * 0.70f, y + h * 0.50f } };
    r.Polygon(t, 3, pal.iconHighlight);
}

void AudioIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    Page(r, rc, pal.iconAudio, pal.iconOutline);
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    r.Fill({ x + w * 0.44f, y + h * 0.30f, x + w * 0.50f, y + h * 0.58f }, pal.iconHighlight);
    r.Ellipse({ x + w * 0.28f, y + h * 0.52f, x + w * 0.50f, y + h * 0.72f }, pal.iconHighlight);
}

void ArchiveIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    r.RoundRect({ x + 1, y + 1, x + w - 1, y + h - 1 }, w * 0.10f, pal.iconArchive);
    r.Rect({ x + 1, y + 1, x + w - 1, y + h - 1 }, pal.iconOutline, 1.0f);
    // крышка
    r.Fill({ x + w * 0.14f, y + h * 0.14f, x + w * 0.86f, y + h * 0.26f }, pal.iconHighlight);
    r.Rect({ x + w * 0.14f, y + h * 0.14f, x + w * 0.86f, y + h * 0.26f }, pal.iconOutline, 1.0f);
    // замок
    r.Fill({ x + w * 0.40f, y + h * 0.34f, x + w * 0.60f, y + h * 0.50f }, pal.iconHighlight);
    r.Fill({ x + w * 0.38f, y + h * 0.62f, x + w * 0.62f, y + h * 0.78f }, pal.iconHighlight);
}

void FontIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    Page(r, rc, pal.iconFont, pal.iconOutline);
    D2D1_RECT_F tr = { rc.left, rc.top + 1, rc.right, rc.bottom - 1 };
    r.Text(L"А", tr, pal.iconHighlight, false, true, false, 700);
}

void SysIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float rOut = w * 0.42f, rIn = w * 0.22f;
    // 8 зубцов
    for (int i = 0; i < 8; i++) {
        double a = 3.14159265358979 * i / 4.0;
        float x1 = cx + (float)cos(a) * rIn, y1 = cy + (float)sin(a) * rIn;
        float x2 = cx + (float)cos(a) * rOut, y2 = cy + (float)sin(a) * rOut;
        r.Line(x1, y1, x2, y2, pal.iconSys, std::max(1.0f, w * 0.09f));
    }
    // кольцо
    r.Ellipse({ cx - rIn, cy - rIn, cx + rIn, cy + rIn }, pal.iconSys);
    r.Ellipse({ cx - w * 0.10f, cy - w * 0.10f, cx + w * 0.10f, cy + w * 0.10f },
              pal.iconHighlight);
    r.Rect({ cx - rIn, cy - rIn, cx + rIn, cy + rIn }, pal.iconOutline, 1.0f);
}

void LinkIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    float x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    r.Line(x + w * 0.22f, y + h * 0.78f, x + w * 0.70f, y + h * 0.30f, pal.iconLink,
           std::max(1.0f, w * 0.10f));
    D2D1_POINT_2F t[3] = { { x + w * 0.66f, y + h * 0.10f }, { x + w * 0.66f, y + h * 0.42f },
                           { x + w * 0.34f, y + h * 0.42f } };
    r.Polygon(t, 3, pal.iconLink);
}

void FileIcon(Renderer& r, const D2D1_RECT_F& rc, const Palette& pal) {
    Page(r, rc, pal.iconFile, pal.iconOutline);
}

} // namespace

void DrawIcon(Renderer& r, IconId id, const D2D1_RECT_F& rc, const Palette& pal) {
    switch (id) {
    case IconId::Folder: FolderIcon(r, rc, pal); break;
    case IconId::Up: UpIcon(r, rc, pal); break;
    case IconId::Drive: DriveIcon(r, rc, pal); break;
    case IconId::Exe: ExeIcon(r, rc, pal); break;
    case IconId::Text: TextIcon(r, rc, pal); break;
    case IconId::Image: ImageIcon(r, rc, pal); break;
    case IconId::Video: VideoIcon(r, rc, pal); break;
    case IconId::Audio: AudioIcon(r, rc, pal); break;
    case IconId::Archive: ArchiveIcon(r, rc, pal); break;
    case IconId::Font: FontIcon(r, rc, pal); break;
    case IconId::Sys: SysIcon(r, rc, pal); break;
    case IconId::Link: LinkIcon(r, rc, pal); break;
    default: FileIcon(r, rc, pal); break;
    }
}
