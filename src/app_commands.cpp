#include "app.h"
#include "ui/window.h"
#include "ui/panel.h"
#include "dialogs.h"
#include "ui/viewer.h"
#include "pathutil.h"
#include "fsenum.h"
#include "search.h"

namespace {

struct AskCtx {
    std::wstring src, dst;
    uint64_t sizeSrc = 0, sizeDst = 0;
    FILETIME tS{}, tD{};
    AskChoice result = AskChoice::Cancel;
    HANDLE ev = nullptr;
};

std::wstring CopySummary(const std::vector<std::wstring>& srcs) {
    int files = 0, dirs = 0;
    uint64_t bytes = 0;
    for (auto& s : srcs) {
        if (DirExists(s)) {
            dirs++;
            bytes += CalcTreeSize(s, nullptr);
        } else {
            files++;
            WIN32_FILE_ATTRIBUTE_DATA d{};
            if (GetFileAttributesExW(LongPath(s).c_str(), GetFileExInfoStandard, &d))
                bytes += ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
        }
    }
    std::wstring out;
    if (files) out += std::to_wstring(files) + L" файл(ов)";
    if (dirs) out += (out.empty() ? L"" : L", ") + std::to_wstring(dirs) + L" каталог(ов)";
    out += L" (" + FormatSize(bytes) + L")";
    return out;
}

AskChoice AskImpl(HWND mainHwnd, const std::wstring& src, const std::wstring& dst,
                  uint64_t szS, uint64_t szD, FILETIME tS, FILETIME tD) {
    AskCtx ctx;
    ctx.src = src;
    ctx.dst = dst;
    ctx.sizeSrc = szS;
    ctx.sizeDst = szD;
    ctx.tS = tS;
    ctx.tD = tD;
    ctx.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    SendMessageW(mainHwnd, WM_APP_ASKOVERWRITE, 0, (LPARAM)&ctx);
    WaitForSingleObject(ctx.ev, INFINITE);
    CloseHandle(ctx.ev);
    return ctx.result;
}

void DriveCommand(App* app, int side) {
    auto items = GetDriveItems();
    if (items.empty()) {
        app->SetStatusText(L"Диски не найдены");
        return;
    }
    int sel = 0;
    std::wstring cur = RootOf(app->panels[side]->path);
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].path == cur) { sel = (int)i; break; }
    if (DriveDialog(app->win->Hwnd(), app->Theme(), items, sel, side) == 1 &&
        sel >= 0 && sel < (int)items.size()) {
        app->GoToDrive(side, items[sel].path);
    }
}

// Выполняет команду пользовательского меню [usermenu]:
// "cd путь" — переход активной панели, иначе cmd.exe /C.
void RunUserCommand(App* app, const std::wstring& command) {
    std::wstring line = command;
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
        else p = PathJoin(app->ActivePanel()->path, p);
        if (DirExists(p)) app->ActivePanel()->SetPath(p);
        else app->SetStatusText(L"Каталог не найден: " + rest);
        return;
    }

    std::wstring cmd = L"cmd.exe /C " + line;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       app->ActivePanel()->path.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        app->SetStatusText(L"Не удалось запустить команду");
    }
}

// Главное меню F9. Возвращает id пункта или 0 при отмене; сам выполняет команду.
void RunMainMenu(App* app) {
    std::vector<MenuItem> items = {
        { L"Файлы", 0, true, 0 },
        { L"1   Просмотр               (F3)", (int)Cmd::View, false, L'П' },
        { L"2   Правка                 (F4)", (int)Cmd::Edit, false, L'В' },
        { L"3   Переименовать          (F2)", (int)Cmd::Rename, false, L'Л' },
        { L"4   Копирование            (F5)", (int)Cmd::Copy, false, L'К' },
        { L"5   Перемещение            (F6)", (int)Cmd::Move, false, L'Е' },
        { L"6   Создать каталог        (F7)", (int)Cmd::MkDir, false, L'С' },
        { L"7   Новый файл             (Shift+F4)", (int)Cmd::NewFile, false, L'О' },
        { L"8   Удаление               (F8)", (int)Cmd::Delete, false, L'У' },
        { L"9   Свойства", (int)Cmd::Props, false, L'Й' },
        { L"Выделение", 0, true, 0 },
        { L"10  Выделить группу        (Num+)", (int)Cmd::SelMask, false, L'Г' },
        { L"11  Снять выделение        (Num−)", (int)Cmd::UnselMask, false, L'Н' },
        { L"12  Снять ВСЁ           (Ctrl+Space)", (int)Cmd::UnselectAll, false, L'Ж' },
        { L"13  Инвертировать          (Num*)", (int)Cmd::InvertSel, false, L'И' },
        { L"Команды", 0, true, 0 },
        { L"14  Поиск файлов           (Alt+F7)", (int)Cmd::Search, false, L'Ф' },
        { L"15  Смена диска            (Alt+F1/F2)", (int)Cmd::DriveLeft, false, L'Д' },
        { L"16  Обмен панелей          (Ctrl+U)", (int)Cmd::Swap, false, L'М' },
        { L"17  Обновить               (Ctrl+R)", (int)Cmd::Refresh, false, L'Б' },
        { L"18  Размеры каталогов      (F11)", (int)Cmd::SizeView, false, L'Ы' },
        { L"19  Быстрый просмотр     (Ctrl+Q)", (int)Cmd::QuickView, false, L'Ц' },
        { L"20  История переходов      (Ctrl+←/→)", (int)Cmd::History, false, L'Р' },
        { L"21  Меню пользователя", (int)Cmd::UserMenu, false, L'З' },
        { L"Настройки", 0, true, 0 },
        { L"22  Тема (Ctrl+F12): сейчас " + std::wstring(ThemeName(app->Theme())),
          (int)Cmd::ThemeToggle, false, L'Т' },
        { L"23  Редактор по умолчанию", -1, false, L'А' },
        { L"24  Выход                  (F10)", (int)Cmd::Quit, false, L'Х' },
    };
    int r = MenuDialog(app->win->Hwnd(), app->Theme(), L"Меню (F9)", items);
    if (r == -1) {
        std::wstring ed = app->Cfg().editor;
        if (InputDialog(app->win->Hwnd(), app->Theme(), L"Редактор по умолчанию",
                        L"Команда редактора (используется F4):", ed) == 1) {
            Trim(ed);
            if (!ed.empty()) {
                app->Cfg().editor = ed;
                SaveConfig(app->Cfg(), GetIniPath());
                app->SetStatusText(L"Редактор: " + ed);
            }
        }
        return;
    }
    if (r > 0) app->Exec((Cmd)r);
}

// Меню пользователя F2 (секция [usermenu] в amber.ini).
void RunUserMenu(App* app) {
    std::vector<UserMenuEntry> entries;
    if (!ReadUserMenu(GetIniPath(), entries)) {
        MessageDialog(app->win->Hwnd(), app->Theme(), L"Меню пользователя",
            L"Секция [usermenu] в amber.ini пуста.\n\n"
            L"Создайте её рядом с amber.exe, например:\n\n"
            L"[usermenu]\n"
            L"1=Блокнот|notepad.exe\n"
            L"2=Калькулятор|calc.exe\n"
            L"3=Каталог Windows|cd C:\\Windows\n\n"
            L"Формат: номер=Подпись|команда\n"
            L"Команда \"cd путь\" переходит в каталог панели,\n"
            L"остальные команды запускаются через cmd.exe /C.");
        return;
    }
    std::vector<MenuItem> items;
    for (size_t i = 0; i < entries.size(); i++) {
        MenuItem mi;
        mi.text = std::to_wstring(i + 1) + L"  " + entries[i].label;
        mi.id = (int)(i + 1);
        items.push_back(mi);
    }
    int r = MenuDialog(app->win->Hwnd(), app->Theme(), L"Меню пользователя (F2)", items);
    if (r > 0 && r <= (int)entries.size()) RunUserCommand(app, entries[(size_t)r - 1].command);
}

} // namespace

void HandleAskOverwrite(App* app, void* vp) {
    auto* ctx = (AskCtx*)vp;
    std::wstring msg = L"Файл уже существует:\n" + ctx->dst + L"\n\nЗаменить?";
    std::vector<std::pair<std::wstring, int>> btns = {
        { L"Да", 1 }, { L"Да для всех", 2 }, { L"Нет", 3 }, { L"Нет для всех", 4 }, { L"Отмена", 5 }
    };
    int r = ConfirmDialog(app->win->Hwnd(), app->Theme(), L"Подтверждение", msg, btns, 0);
    switch (r) {
    case 1: ctx->result = AskChoice::Yes; break;
    case 2: ctx->result = AskChoice::YesAll; break;
    case 3: ctx->result = AskChoice::No; break;
    case 4: ctx->result = AskChoice::NoAll; break;
    default: ctx->result = AskChoice::Cancel; break;
    }
    SetEvent(ctx->ev);
}

void HandleCopyDone(App* app, CopyCtx* ctx) {
    app->copyCancelPtr = nullptr;
    if (app->progressDlg) {
        app->progressDlg->Close(0);
        app->progressDlg = nullptr;
    }
    if (!ctx->outcome.canceled && !ctx->outcome.errors.empty()) {
        std::wstring msg = L"Завершено с ошибками:\n\n";
        int n = 0;
        for (auto& e : ctx->outcome.errors) {
            if (n++ >= 20) {
                msg += L"…\n";
                break;
            }
            msg += e + L"\n";
        }
        MessageDialog(app->win->Hwnd(), app->Theme(), L"Ошибки", msg);
    }
    app->ActivePanel()->Refresh();
    app->PassivePanel()->Refresh();
    app->SetStatusText(ctx->outcome.canceled ? L"Операция отменена" : L"Готово");
    delete ctx;
}

void StartCopy(App* app, const std::vector<std::wstring>& srcs, const std::wstring& dstRaw,
               bool move, bool ifNewer) {
    if (srcs.empty()) return;
    auto* ctx = new CopyCtx();
    ctx->opt.move = move;
    ctx->opt.overwrite = ifNewer ? Overwrite::IfNewer
                                 : (app->Cfg().confirmOverwrite ? Overwrite::Ask : Overwrite::Always);
    bool singleFileRename = srcs.size() == 1 && !DirExists(srcs[0]) && !DirExists(dstRaw) &&
                            !dstRaw.empty() && dstRaw.back() != L'\\';
    for (auto& s : srcs) {
        std::wstring d = singleFileRename ? dstRaw : PathJoin(dstRaw, PathFileName(s));
        ctx->jobs.push_back({ s, d });
    }
    auto* dlg = new ProgDlg();
    dlg->autoDelete = true;
    dlg->theme = app->Theme();
    dlg->snap = &ctx->snap;
    dlg->cancel = &ctx->cancel;
    dlg->pause = &ctx->pause;
    dlg->Create(move ? L"Перемещение" : L"Копирование", 560, 190);
    dlg->bar.rc = { 14, 68, 546, 92 };
    dlg->bPause.rc = { 240, 122, 352, 152 };
    dlg->bCancel.rc = { 368, 122, 480, 152 };
    dlg->SetTimerMs(1, 120);
    dlg->Show();
    app->progressDlg = dlg;
    app->copyCancelPtr = &ctx->cancel;
    HWND mainHwnd = app->win->Hwnd();
    std::thread([ctx, mainHwnd]() {
        ctx->outcome = RunCopy(ctx->jobs, ctx->opt,
            [ctx, mainHwnd](const CopyProgress& p) {
                {
                    std::lock_guard<std::mutex> g(ctx->snap.m);
                    ctx->snap.p = p;
                }
                ULONGLONG now = GetTickCount64();
                if (now - ctx->lastTick > 60) {
                    ctx->lastTick = now;
                    PostMessageW(mainHwnd, WM_APP_COPYTICK, 0, 0);
                }
            },
            [mainHwnd](const std::wstring& src, const std::wstring& dst,
                       uint64_t szS, uint64_t szD, FILETIME tS, FILETIME tD) {
                return AskImpl(mainHwnd, src, dst, szS, szD, tS, tD);
            },
            ctx->cancel, ctx->pause);
        PostMessageW(mainHwnd, WM_APP_COPYDONE, 0, (LPARAM)ctx);
    }).detach();
}

void App::OpenSearchParams() {
    App* app = this;
    if (app->paramsDlg) {
        SetForegroundWindow(app->paramsDlg->hwnd);
        return;
    }
    auto* d = new SearchParamsDlg();
    d->app = app;
    d->theme = app->Theme();
    d->autoDelete = true;
    d->eName.text = app->lastSearchName;
    d->eName.caret = (int)d->eName.text.size();
    d->eText.text = app->lastSearchText;
    d->eText.caret = (int)d->eText.text.size();
    d->eRoot.text = app->ActivePanel()->path;
    d->eRoot.caret = (int)d->eRoot.text.size();
    d->eName.focused = true;
    d->cSub.checked = true;
    d->cFiles.checked = true;
    d->cDirs.checked = true;
    d->Create(L"Поиск файлов (Alt+F7)", 620, 252);
    d->eName.rc = { 210, 12, 604, 34 };
    d->eText.rc = { 210, 46, 604, 68 };
    d->eRoot.rc = { 210, 80, 604, 102 };
    d->cSub.rc = { 210, 112, 340, 134 };
    d->cFiles.rc = { 350, 112, 430, 134 };
    d->cDirs.rc = { 440, 112, 530, 134 };
    d->cCase.rc = { 210, 136, 370, 158 };
    d->bSearch.rc = { 320, 190, 430, 218 };
    d->bClose.rc = { 442, 190, 604, 218 };
    app->paramsDlg = d;
    d->Show();
    SetFocus(d->hwnd);
}

// Запуск поиска по параметрам: закрывает прежние результаты и открывает новые.
// Окно параметров при этом остаётся открытым.
void App::StartSearchWithParams(const SearchQuery& q) {
    App* app = this;
    app->lastSearchName = q.masks;
    app->lastSearchText = q.text;
    if (app->resultsDlg) {
        if (app->resultsDlg->cancel) app->resultsDlg->cancel->store(true);
        app->resultsDlg->Close(0);
    }
    auto dlg = std::make_shared<SearchResultsDlg>();
    dlg->app = app;
    dlg->theme = app->Theme();
    dlg->query = q;
    dlg->cancel = std::make_shared<std::atomic<bool>>(false);
    app->searchCancel = dlg->cancel;
    app->resultsDlg = dlg;
    dlg->list.rc = { 12, 40, 688, 424 };
    dlg->bClose.rc = { 560, 440, 676, 468 };
    if (!dlg->Create(L"Результаты поиска: " + q.root, 700, 480)) {
        app->resultsDlg.reset();
        return;
    }
    dlg->list.ref = &dlg->hits;
    dlg->SetTimerMs(1, 150);
    dlg->Show();

    auto cancel = dlg->cancel;
    std::thread([dlg, q, cancel]() {
        RunSearch(q, [dlg](const std::wstring& p, const DirEntry&) { dlg->AddHit(p); }, *cancel);
        if (dlg->hwnd) PostMessageW(dlg->hwnd, WM_APP_SEARCHDONE, 0, 0);
    }).detach();
}

bool ExecFileCommand(App* app, Cmd c) {
    using enum Cmd;
    switch (c) {
    case Copy:
    case Move: {
        auto srcs = app->ActivePanel()->SelectedPaths(true);
        if (srcs.empty()) {
            MessageBeep(MB_ICONWARNING);
            return true;
        }
        std::wstring dst = app->PassivePanel()->path;
        bool move = c == Move;
        bool newer = false;
        int r = CopyDialog(app->win->Hwnd(), app->Theme(), move, CopySummary(srcs), dst, newer);
        if (r == 0) return true;
        if (r == 2) move = true;
        if (dst.empty()) return true;
        if (!DirExists(dst)) {
            std::wstring dd = dst;
            if (dd.size() >= 3 && dd[1] == L':' && dd.back() != L'\\') dd += L"\\";
            if (CreateDirectoryW(LongPath(dd).c_str(), nullptr)) {
                dst = dd;
            } else if (!(srcs.size() == 1 && !DirExists(srcs[0]))) {
                app->SetStatusText(L"Не удалось создать каталог: " + dst);
                return true;
            }
        }
        StartCopy(app, srcs, dst, move, newer);
        return true;
    }
    case Rename: {
        auto p = app->ActivePanel()->SelectedPaths(true);
        if (p.empty()) return true;
        std::wstring oldName = PathFileName(p[0]);
        std::wstring newName = oldName;
        if (InputDialog(app->win->Hwnd(), app->Theme(), L"Переименование", L"Новое имя:",
                        newName) == 1 &&
            !newName.empty() && newName != oldName) {
            std::wstring dst = PathJoin(PathParent(p[0]), newName);
            if (FileExists(dst) || DirExists(dst)) {
                app->SetStatusText(L"Уже существует: " + newName);
                return true;
            }
            if (MoveFileExW(LongPath(p[0]).c_str(), LongPath(dst).c_str(), 0)) {
                app->ActivePanel()->Refresh();
            } else {
                app->SetStatusText(L"Не удалось переименовать: " + oldName);
            }
        }
        return true;
    }
    case NewFile: {
        std::wstring name;
        if (InputDialog(app->win->Hwnd(), app->Theme(), L"Новый файл", L"Имя файла:", name) == 1 &&
            !name.empty()) {
            std::wstring full = PathJoin(app->ActivePanel()->path, name);
            if (FileExists(full) || DirExists(full)) {
                app->SetStatusText(L"Уже существует: " + name);
                return true;
            }
            HANDLE hf = CreateFileW(LongPath(full).c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                CloseHandle(hf);
                app->ActivePanel()->Refresh();
            } else {
                app->SetStatusText(L"Не удалось создать файл: " + name);
            }
        }
        return true;
    }
    case MkDir: {
        std::wstring name;
        if (InputDialog(app->win->Hwnd(), app->Theme(), L"Новый каталог", L"Имя каталога:", name) == 1 &&
            !name.empty()) {
            std::wstring full = PathJoin(app->ActivePanel()->path, name);
            if (CreateDirectoryW(LongPath(full).c_str(), nullptr)) {
                app->ActivePanel()->Refresh();
            } else {
                app->SetStatusText(L"Не удалось создать каталог: " + name);
            }
        }
        return true;
    }
    case Delete:
    case DeleteNoConfirm: {
        Panel* ap = app->ActivePanel();
        std::vector<std::wstring> srcs;
        bool sizeModeDelete = false;
        uint64_t sizeModeItemSize = 0;
        if (ap->sizeMode) {
            // режим размеров: удаляем элемент под курсором (файл или каталог)
            std::wstring p = ap->SizeCursorPath();
            if (p.empty()) return true;
            sizeModeDelete = true;
            if (ap->cursor >= 0 && ap->cursor < (int)ap->sizeItems.size())
                sizeModeItemSize = ap->sizeItems[ap->cursor].size;
            srcs.push_back(p);
        } else {
            srcs = ap->SelectedPaths(true);
            if (srcs.empty()) return true;
        }
        auto Summary = [&]() -> std::wstring {
            if (sizeModeDelete) {
                const wchar_t* t = srcs[0].c_str();
                std::wstring name = PathFileName(srcs[0]);
                return name + L"  (" + FormatSize(sizeModeItemSize) + L")";
            }
            return CopySummary(srcs);
        };
        if (c == Delete && app->Cfg().confirmDelete) {
            std::wstring msg = L"Отправить в Корзину?\n\n" + Summary();
            if (ConfirmDialog(app->win->Hwnd(), app->Theme(), L"Удаление", msg,
                              { { L"Да", 1 }, { L"Отмена", 0 } }, 0) != 1)
                return true;
        }
        auto errs = DeleteToRecycleBin(srcs);
        if (!errs.empty()) {
            std::wstring msg =
                L"Не удалось отправить в Корзину.\nУдалить безвозвратно?\n\n" + Summary();
            if (ConfirmDialog(app->win->Hwnd(), app->Theme(), L"Удаление", msg,
                              { { L"Да", 1 }, { L"Нет", 0 } }, 0) == 1) {
                errs = DeleteDirect(srcs);
            } else {
                if (sizeModeDelete) ap->StartSizeScan();
                else ap->Refresh();
                app->SetStatusText(L"Удаление отменено");
                return true;
            }
        }
        if (!errs.empty()) {
            std::wstring m = L"Ошибки:\n";
            int n = 0;
            for (auto& e : errs) {
                if (n++ >= 20) {
                    m += L"…\n";
                    break;
                }
                m += e + L"\n";
            }
            MessageDialog(app->win->Hwnd(), app->Theme(), L"Удаление", m);
        }
        if (sizeModeDelete) ap->StartSizeScan();   // пересчитать размеры родителя
        else {
            // курсор — на место удалённого: следующий файл или предыдущий
            ap->pendingCursor = ap->cursor;
            ap->Refresh();
        }
        return true;
    }
    case View: {
        Panel* ap = app->ActivePanel();
        if (ap->sizeMode) {
            // режим размеров: просмотр файла под курсором
            std::wstring p = ap->SizeCursorPath();
            if (p.empty()) return true;
            if (ap->cursor >= 0 && ap->cursor < (int)ap->sizeItems.size() &&
                ap->sizeItems[ap->cursor].isDir) {
                app->SetStatusText(L"F3 работает для файлов; каталог — Enter (внутрь)");
                return true;
            }
            ShowViewer(app->win->Hwnd(), app->Theme(), p);
            return true;
        }
        auto p = ap->SelectedPaths(true);
        if (p.empty()) return true;
        ShowViewer(app->win->Hwnd(), app->Theme(), p[0]);
        return true;
    }
    case Edit: {
        auto p = app->ActivePanel()->SelectedPaths(true);
        if (p.empty()) return true;
        std::wstring editor = app->Cfg().editor;
        // редактор остался дефолтным (Notepad++ не найден) — один раз спрашиваем
        if (editor == L"notepad.exe") {
            int ch = ConfirmDialog(app->win->Hwnd(), app->Theme(), L"Редактор",
                L"Notepad++ не найден.\nОткрыть файл в Блокноте Windows?",
                { { L"Блокнот", 1 }, { L"Выбрать редактор…", 2 }, { L"Отмена", 0 } }, 1);
            if (ch == 0) return true;
            if (ch == 2) {
                std::wstring e;
                if (InputDialog(app->win->Hwnd(), app->Theme(), L"Редактор по умолчанию",
                                L"Команда редактора (используется F4):", e) == 1) {
                    Trim(e);
                    if (!e.empty()) {
                        app->Cfg().editor = e;
                        SaveConfig(app->Cfg(), GetIniPath());
                        editor = e;
                    } else {
                        return true;
                    }
                } else {
                    return true;
                }
            } else {
                // Блокнот: сохраняем полный путь, чтобы не спрашивать снова
                wchar_t buf[MAX_PATH];
                if (SearchPathW(nullptr, L"notepad.exe", nullptr, MAX_PATH, buf, nullptr))
                    editor = buf;
                app->Cfg().editor = editor;
                SaveConfig(app->Cfg(), GetIniPath());
            }
        }
        // голое имя команды (code, vim, nvim…) — ищем в PATH
        if (editor.find(L'\\') == std::wstring::npos &&
            editor.find(L'/') == std::wstring::npos && !FileExists(editor)) {
            wchar_t buf[MAX_PATH];
            if (SearchPathW(nullptr, editor.c_str(), nullptr, MAX_PATH, buf, nullptr) ||
                SearchPathW(nullptr, editor.c_str(), L".exe", MAX_PATH, buf, nullptr))
                editor = buf;
        }
        std::wstring arg = L"\"" + p[0] + L"\"";
        HINSTANCE r = ShellExecuteW(app->win->Hwnd(), L"open", editor.c_str(), arg.c_str(),
                                    nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)r <= 32) app->SetStatusText(L"Не удалось запустить редактор: " + editor);
        return true;
    }
    case Search:
        app->OpenSearchParams();
        return true;
    case DriveLeft:
        DriveCommand(app, 0);
        return true;
    case DriveRight:
        DriveCommand(app, 1);
        return true;
    case Menu:
        RunMainMenu(app);
        return true;
    case Help:
        MessageDialog(app->win->Hwnd(), app->Theme(), L"Справка — Amber Commander",
            L"Amber Commander — быстрый двухпанельный менеджер в тёплых тонах.\n\n"
            L"F2 — переименовать    F3 — просмотр    F4 — правка (Notepad)\n"
            L"F5 — копирование      F6 — перемещение\n"
            L"F7 — каталог          F8 — удаление (в Корзину)\n"
            L"Shift+F4 — новый файл Shift+F8 — удалить без подтверждения\n"
            L"Alt+F7 — поиск        Alt+F1/F2 — выбор диска\n"
            L"Alt+Enter — полный экран   Свойства — в меню F9\n"
            L"Ctrl+L — размеры каталогов (вид WizTree) в соседней панели\n"
            L"F11 — размеры каталогов   F4 — правка (Notepad++ при наличии)\n"
            L"Ctrl+Q — быстрый просмотр в соседней панели: картинки, текст,\n"
            L"        карточка файла (размер/дата/атрибуты/HEX); Esc — выключить\n"
            L"Tab — панель          Ctrl+U — обмен панелей   Ctrl+O — скрыть панели\n"
            L"← / → — вверх / вход   Ctrl+← / Ctrl+→ — история переходов\n"
            L"Insert/Space — выделение   Shift+↑/↓ — выделять по мере движения\n"
            L"Ctrl+Space — снять всё выделение   Ctrl+A — выделить всё\n"
            L"Шапка панели — хлебные крошки: клик по сегменту — переход,\n"
            L"клик по «›» — список подпапок. Командная строка: набор ≥2 символов —\n"
            L"поиск каталогов на всех дисках (Enter — открыть, Esc — закрыть).\n"
            L"Правая кнопка + перетаскивание — переместить в папку (с подсветкой).\n"
            L"Alt+буква — быстрый переход   Ctrl+F3..F6 — сортировка\n"
            L"Ctrl+F12 — тема       F10 — выход\n\n"
            L"Набор текста — живой переход к файлу; Enter — открыть файл\n"
            L"с таким именем или выполнить команду (как Win+R), cd — каталог.");
        return true;
    case UserMenu:
        RunUserMenu(app);
        return true;
    case Props: {
        auto p = app->ActivePanel()->SelectedPaths(true);
        if (p.empty()) return true;
        if (PropsDialog(app->win->Hwnd(), app->Theme(), p[0]))
            app->ActivePanel()->Refresh();
        return true;
    }
    case History: {
        std::wstring d = HistoryDialog(app->win->Hwnd(), app->Theme(), app->ActivePanel()->path);
        if (!d.empty() && DirExists(d) && d != app->ActivePanel()->path)
            app->ActivePanel()->SetPath(d);
        return true;
    }
    default:
        return false;
    }
}
