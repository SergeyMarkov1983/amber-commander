#pragma once
#include "common.h"
#include "theme.h"
#include "config.h"
#include "fileops.h"
#include "search.h"
#include "volumes.h"
#include "panel.h"
#include "shellmenu.h"

class MainWindow;
class ProgDlg;
class SearchParamsDlg;
class SearchResultsDlg;

enum class Cmd {
    None, Up, Root, Parent, Enter, Tab, Swap, Refresh,
    Copy, Move, MkDir, Delete, DeleteNoConfirm,
    View, Edit, Search, Menu, Help, UserMenu, Quit,
    DriveLeft, DriveRight, Props,
    SelToggle, SelAll, SelMask, UnselMask, InvertSel, UnselectAll,
    SortName, SortExt, SortSize, SortDate,
    ThemeToggle, PanelsToggle, History, QuickClear,
    Rename, NewFile, HistBack, HistFwd, SizeView, QuickView
};

struct ProgSnap {
    CopyProgress p;
    std::mutex m;
};

struct CopyCtx {
    std::vector<CopyJob> jobs;
    CopyOptions opt;
    std::atomic<bool> cancel{ false };
    std::atomic<bool> pause{ false };
    CopyOutcome outcome;
    ProgSnap snap;
    ULONGLONG lastTick = 0;
};

class App {
public:
    Panel* panels[2] = { nullptr, nullptr };
    MainWindow* win = nullptr;
    std::unique_ptr<Config> cfg;
    ThemeId theme = ThemeId::Light;
    int activePanel = 0;
    bool panelsHidden = false;
    // подсветка цели перетаскивания правой кнопкой (панель/строка или -1)
    int dropPanel = -1, dropRow = -1;
    // попап глобального поиска каталогов (набор в командной строке)
    bool qpOpen = false;
    std::vector<std::wstring> qpResults;
    int qpSel = 0, qpTop = 0;
    std::wstring cmdLine;
    std::wstring statusText;
    ULONGLONG statusUntil = 0;
    std::atomic<bool>* copyCancelPtr = nullptr;
    std::shared_ptr<std::atomic<bool>> searchCancel;
    ProgDlg* progressDlg = nullptr;
    std::vector<DriveItem> drives;          // линейка дисков
    void RefreshDrives();

    bool Run();
    void Shutdown();
    // Сохранить состояние (тема, каталоги панелей, геометрия окна, настройки)
    void SaveState();

    void OnKey(WPARAM vk, DWORD flags, bool sys);
    void OnChar(wchar_t ch);
    void OnSysChar(wchar_t ch);
    bool Exec(Cmd c);

    void OnLoadedAsync(int idx, LoadMsg* msg);
    void OnPanelLoaded(Panel* p);
    void UpdateTitle();
    void OnSizeReady(int idx, SizeMsg* msg);

    // поиск: немодальное окно параметров + окно результатов
    SearchParamsDlg* paramsDlg = nullptr;
    std::shared_ptr<SearchResultsDlg> resultsDlg;
    std::wstring lastSearchName, lastSearchText;
    void OpenSearchParams();
    void StartSearchWithParams(const SearchQuery& q);

    // системное контекстное меню для файла/папки под курсором (x, y — экранные)
    bool ShowContextMenu(int x, int y);

    Panel* ActivePanel() { return panels[activePanel]; }
    Panel* PassivePanel() { return panels[1 - activePanel]; }
    ThemeId Theme() const { return theme; }
    void ClearCmdLine() { cmdLine.clear(); }
    void SwapPanels();
    // Переход панели side на диск root. Как в Double Commander: если соседняя
    // панель уже на этом диске — открыть её каталог и встать курсором на её файл.
    void GoToDrive(int side, const std::wstring& root);
    void SetStatusText(const std::wstring& s);
    void ShowError(const std::wstring& s) { SetStatusText(s); }
    void Redraw();
    void SetTheme(ThemeId t);
    Config& Cfg() { return *cfg; }
    void ExecuteCommandLine();

    // ── Быстрый просмотр (Ctrl+Q): превью файла под курсором в пассивной панели ──
    bool quickView = false;
    std::wstring qvPath;
    FILETIME qvMtime{};
    uint64_t qvGen = 1;
    bool qvLoading = false;
    bool qvIsImage = false;
    HBITMAP qvBmp = nullptr;
    float qvImgW = 0, qvImgH = 0;   // исходные размеры картинки (для подписи)
    std::wstring qvText;            // текст превью (первые 64 КБ)
    std::wstring qvInfo;            // карточка прочих файлов
    void* qvPending = nullptr;      // текущая фоновая загрузка (QvData*)
    // LRU-кэш последних превью (≤2 записи) — мгновенный возврат на просмотренное
    struct QvCacheEntry {
        std::wstring path;
        FILETIME mtime{};
        HBITMAP bmp = nullptr;
        UINT w = 0, h = 0;
    };
    std::vector<QvCacheEntry> qvCache;
    void QvRememberCurrent();
    void ToggleQuickView();
    void QuickViewCursorMoved();    // перезапуск загрузки при смене курсора
    void OnQuickViewReady(uint64_t g, void* data);
    void DrawQuickView(Renderer& r, const D2D1_RECT_F& area);
    void QuickViewInvalidate();
};

// Реализованы в app_commands.cpp
bool ExecFileCommand(App* app, Cmd c);
void HandleAskOverwrite(App* app, void* ctx);
void HandleCopyDone(App* app, CopyCtx* ctx);
void StartCopy(App* app, const std::vector<std::wstring>& srcs, const std::wstring& dstRaw,
               bool move, bool ifNewer);
