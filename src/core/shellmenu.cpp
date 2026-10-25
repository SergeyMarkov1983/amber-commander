#include "shellmenu.h"
#include <shlobj.h>

// Контекстное меню оболочки через CDefFolderMenu:
// один и тот же механизм для одного и нескольких выделенных элементов,
// со всеми системными пунктами («Открыть», «Открыть с помощью», «7-Zip»,
// антивирусы, «Копировать/Вырезать», «Удалить», «Свойства» и т.д.).
// Два ключевых момента:
// 1) элементы в DEFCONTEXTMENU.apidl должны быть ОТНОСИТЕЛЬНЫ pidlFolder —
//    иначе шелл выдаёт урезанное fallback-меню без расширений;
// 2) окно-владелец обязано пробрасывать WM_INITMENUPOPUP/WM_DRAWITEM/
//    WM_MEASUREITEM активному меню — иначе расширения не достраивают
//    подменю, а команды дают ошибки.

static IContextMenu2* g_activeCM2 = nullptr;
static IContextMenu3* g_activeCM3 = nullptr;

void SetActiveShellMenu(IContextMenu* cm) {
    if (g_activeCM2) {
        g_activeCM2->Release();
        g_activeCM2 = nullptr;
    }
    if (g_activeCM3) {
        g_activeCM3->Release();
        g_activeCM3 = nullptr;
    }
    if (!cm) return;
    cm->QueryInterface(IID_IContextMenu2, (void**)&g_activeCM2);
    cm->QueryInterface(IID_IContextMenu3, (void**)&g_activeCM3);
}

void HandleShellMenuMsg(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_activeCM3) {
        LRESULT res = 0;
        g_activeCM3->HandleMenuMsg2(msg, wParam, lParam, &res);
    } else if (g_activeCM2) {
        g_activeCM2->HandleMenuMsg(msg, wParam, lParam);
    }
}

bool ShowShellContextMenu(HWND hwnd, int x, int y, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return false;

    std::vector<LPITEMIDLIST> pidls;
    LPITEMIDLIST folder = nullptr;

    for (auto& p : paths) {
        // оболочке нужен обычный путь, без префикса длинных путей
        std::wstring np = p;
        if (np.rfind(L"\\\\?\\", 0) == 0) np = np.substr(4);
        LPITEMIDLIST full = nullptr;
        if (FAILED(SHParseDisplayName(np.c_str(), nullptr, &full, 0, nullptr)))
            continue;
        if (!folder) {
            folder = ILCloneFull(full);
            if (folder && !ILRemoveLastID(folder)) {
                ILFree(folder);
                folder = nullptr;
            }
        }
        // CDefFolderMenu требует элементы ОТНОСИТЕЛЬНО pidlFolder:
        // абсолютные pidl дают урезанное fallback-меню без 7-Zip и т.п.
        LPITEMIDLIST item = ILClone(ILFindLastID(full));
        ILFree(full);
        if (!item) continue;
        pidls.push_back(item);
    }
    if (pidls.empty()) return false;

    IShellFolder* psf = nullptr;
    if (folder) {
        IShellFolder* desktop = nullptr;
        if (SUCCEEDED(SHGetDesktopFolder(&desktop))) {
            desktop->BindToObject(folder, nullptr, IID_IShellFolder, (void**)&psf);
            desktop->Release();
        }
    }

    DEFCONTEXTMENU dcm{};
    dcm.hwnd = hwnd;
    dcm.pidlFolder = folder;
    dcm.psf = psf;
    dcm.cidl = (UINT)pidls.size();
    dcm.apidl = (LPCITEMIDLIST*)pidls.data();

    IContextMenu* cm = nullptr;
    bool executed = false;
    HRESULT hrC = SHCreateDefaultContextMenu(&dcm, IID_IContextMenu, (void**)&cm);
    if (SUCCEEDED(hrC) && cm) {
        HMENU menu = CreatePopupMenu();
        if (SUCCEEDED(cm->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL))) {
            // пока меню открыто, окно пробрасывает сообщения расширениям
            SetActiveShellMenu(cm);
            UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        x, y, hwnd, nullptr);
            SetActiveShellMenu(nullptr);
            if (cmd >= 1 && cmd <= 0x7FFF) {
                CMINVOKECOMMANDINFOEX ici{};
                ici.cbSize = sizeof(ici);
                ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                ici.hwnd = hwnd;
                ici.lpVerb = MAKEINTRESOURCEA(cmd - 1);
                ici.lpVerbW = MAKEINTRESOURCEW(cmd - 1);
                ici.nShow = SW_SHOWNORMAL;
                ici.ptInvoke = { x, y };
                // во время выполнения расширение шлёт окну сообщения —
                // держим указатель активным
                SetActiveShellMenu(cm);
                executed = SUCCEEDED(cm->InvokeCommand((CMINVOKECOMMANDINFO*)&ici));
                SetActiveShellMenu(nullptr);
            }
        }
        DestroyMenu(menu);
        cm->Release();
    }

    if (psf) psf->Release();
    for (auto pidl : pidls) ILFree(pidl);
    if (folder) ILFree(folder);
    return executed;
}
