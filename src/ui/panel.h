#pragma once
#include "common.h"
#include "render.h"
#include "icons.h"
#include "mftscan.h"

class App;

struct SizeMsg {
    uint64_t gen;
    std::vector<SizeItem>* items;
};

// Файловая панель (как в NC/VC): асинхронная загрузка, курсор, выделение, сортировка.
struct Panel {
    App* app = nullptr;
    HWND hwnd = nullptr;
    int index = 0;

    std::wstring path = L"C:\\";
    std::shared_ptr<const std::vector<DirEntry>> snapshot;
    std::unordered_set<std::wstring> selected;  // по именам
    int cursor = 0, top = 0;
    int sortKey = 0;        // 0 имя, 1 расширение, 2 размер, 3 дата
    bool sortAsc = true;
    std::wstring quick;
    ULONGLONG quickTime = 0;
    std::wstring pendingReveal;
    int pendingCursor = -1;      // позиция курсора после перезагрузки (удаление)
    bool loading = false;
    int rowsVisible = 20;

    // история навигации (Alt+← / Alt+→, как в Double Commander)
    std::vector<std::wstring> backStack, forwardStack;
    void HistoryBack();
    void HistoryForward();

    // режим размеров каталогов (WizTree): панель показывает элементы sizeRoot
    // с размерами поддеревьев, по убыванию
    bool sizeMode = false;
    std::wstring sizeRoot;
    std::wstring sizeTopRoot;
    std::vector<SizeItem> sizeItems;
    bool sizeLoading = false;
    std::atomic<uint64_t> sizeGen{ 0 };
    std::atomic<bool> sizeCancel{ false };
    ULONGLONG sizeStart = 0;             // время начала скана (для индикатора)
    void EnterSizeMode(const std::wstring& root);
    void ExitSizeMode();
    void StartSizeScan();
    void OnSizeReady(uint64_t g, std::vector<SizeItem>* itemsPtr);
    // полный путь элемента под курсором в режиме размеров ("" если нет)
    std::wstring SizeCursorPath() const;
    // полный путь элемента под курсором ("" если нет/"..")
    std::wstring CursorPath() const;

    // асинхронная загрузка
    std::atomic<uint64_t> gen{ 0 };
    std::atomic<bool> cancel{ false };

    void Init(App* a, HWND h, int idx);
    void SetPath(const std::wstring& p, bool record = true);
    void Refresh() { SetPath(path); }
    void OnLoaded(uint64_t g, std::vector<DirEntry>* itemsPtr);
    void Resort();

    int Count() const { return sizeMode ? (int)sizeItems.size() : (snapshot ? (int)snapshot->size() : 0); }
    void MoveCursor(int delta);
    void Page(int dir);
    void Home() { if (snapshot && !snapshot->empty()) { cursor = 0; EnsureVisible(); } }
    void End() { if (snapshot && !snapshot->empty()) { cursor = Count() - 1; EnsureVisible(); } }
    void EnsureVisible();
    void QuickSearch(const std::wstring& ch);

    void ToggleSelect();
    void SelectMask(const std::wstring& mask, bool select);
    void SelectAll(bool sel = true);
    void InvertSel();
    int SelectionCount() const;
    uint64_t SelectionBytes() const;
    // Операции действуют на: выделение (если курсор внутри него), иначе — на файл под курсором.
    std::vector<std::wstring> SelectedPaths(bool includeCursorIfNone = true) const;
    // Живой поиск по префиксу: курсор на первый элемент, начинающийся с prefix.
    bool JumpToPrefix(const std::wstring& prefix);
    // Точное совпадение имени — открыть (каталог) или запустить (файл).
    bool OpenByName(const std::wstring& name);
    // Точное имя или префикс — открыть (Enter после набора текста).
    bool OpenByPrefixOrExact(const std::wstring& text);

    void GoUp();
    void GoRoot();
    void EnterCurrent();
    void RevealFile(const std::wstring& fullPath);

    // хлебные крошки в шапке панели (кликабельные сегменты пути)
    struct Seg {
        D2D1_RECT_F rc;
        std::wstring prefix;   // путь, открываемый по клику
        bool gap = false;      // true = разделитель «›» (список подпапок)
    };
    std::vector<Seg> segs;
    int hoverSeg = -1;
    int HitSeg(float x, float y) const;

    void Draw(Renderer& r, const D2D1_RECT_F& area, bool active);
};
