#pragma once
#include "app.h"
#include "ui/dialog.h"
#include "fsenum.h"
#include "search.h"
#include <deque>

// Диалоги менеджера (все рисуются в тёплом стиле).

// Возвращает 1 = OK, 0 = отмена. value — начальное/итоговое значение.
int InputDialog(HWND parent, ThemeId theme, const std::wstring& title,
                const std::wstring& prompt, std::wstring& value);

// Возвращает значение выбранной кнопки (buttons: {подпись, значение}).
int ConfirmDialog(HWND parent, ThemeId theme, const std::wstring& title, const std::wstring& text,
                  const std::vector<std::pair<std::wstring, int>>& buttons, int defIdx = 0);

void MessageDialog(HWND parent, ThemeId theme, const std::wstring& title, const std::wstring& text);

// Возвращает 1 = копировать, 2 = переместить, 0 = отмена.
int CopyDialog(HWND parent, ThemeId theme, bool moveDefault, const std::wstring& summary,
               std::wstring& dst, bool& newer);

// Возвращает 1 и индекс выбранного элемента в selIndex, либо 0/-1.
// side: 0 — левая панель, 1 — правая (позиция всплывающего меню).
int DriveDialog(HWND parent, ThemeId theme, const std::vector<DriveItem>& items, int& selIndex,
                int side);

// Немодальное окно параметров поиска (Alt+F7) — остаётся открытым после запуска
// поиска; «Закрыть» в окне результатов закрывает только результаты.
class SearchParamsDlg : public Dialog {
public:
    App* app = nullptr;
    EditBox eName, eText, eRoot;
    CheckBox cSub{ L"в подкаталогах" };
    CheckBox cFiles{ L"файлы" };
    CheckBox cDirs{ L"каталоги" };
    CheckBox cCase{ L"регистр" };
    Btn bSearch{ L"Искать", {}, 1 }, bClose{ L"Закрыть", {}, 0 };
    int focusIdx = 0;   // 0 имя, 1 текст, 2 каталог

    void Draw() override;
    void OnKey(WPARAM vk, DWORD flags, bool sys) override;
    void OnChar(wchar_t ch) override;
    void OnMouse(UINT msg, int x, int y, WPARAM wKey) override;
    EditBox* FocusEdit();
    void Refocus();
    void StartSearch();
};

// Немодальный диалог прогресса копирования (autoDelete = true).
class ProgDlg : public Dialog {
public:
    ProgSnap* snap = nullptr;
    std::atomic<bool>* cancel = nullptr;
    std::atomic<bool>* pause = nullptr;
    ProgressBar bar;
    Btn bPause{ L"Пауза", {}, 1 };
    Btn bCancel{ L"Отмена", {}, 0 };

    void Draw() override;
    void OnKey(WPARAM vk, DWORD flags, bool sys) override;
    void OnMouse(UINT msg, int x, int y, WPARAM wKey) override;
    void OnTimer(UINT id) override;
};

// Немодальный диалог результатов поиска (живёт через shared_ptr).
class SearchResultsDlg : public Dialog {
public:
    App* app = nullptr;
    SearchQuery query;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::deque<std::wstring> hits;
    std::mutex m;
    bool finished = false;
    bool dirty = false;
    ULONGLONG lastPost = 0;
    ListBox list;
    Btn bClose{ L"Закрыть", {}, 1 };

    void Draw() override;
    void OnKey(WPARAM vk, DWORD flags, bool sys) override;
    void OnMouse(UINT msg, int x, int y, WPARAM wKey) override;
    void OnTimer(UINT id) override;
    LRESULT OnMessage(UINT m, WPARAM w, LPARAM l) override;
    void AddHit(const std::wstring& p);
    void Rebuild();
};

// ---------- Дополнения агента D ----------

// Пункт вертикального меню (F9 / меню пользователя).
struct MenuItem {
    std::wstring text;   // текст пункта
    int id = 0;          // >= 0: значение Cmd; -1: пункт «Редактор по умолчанию»
    bool header = false; // заголовок-разделитель (не выбирается)
    wchar_t hotkey = 0;  // горячая буква (0 = нет)
};

// Главное меню (F9) / меню пользователя (F2). Модальный список, работающий от
// клавиатуры: стрелки, Enter, Esc, горячие буквы, мышь.
// Возвращает id выбранного пункта или 0 при отмене.
int MenuDialog(HWND parent, ThemeId theme, const std::wstring& title,
               const std::vector<MenuItem>& items);

// Пункт пользовательского меню [usermenu] (Подпись|команда).
struct UserMenuEntry {
    std::wstring label;
    std::wstring command;
};

// Читает секцию [usermenu] из amber.ini. Возвращает false, если секция пуста.
bool ReadUserMenu(const std::wstring& iniPath, std::vector<UserMenuEntry>& out);

// Alt+Enter: свойства файла/каталога (размер каталога считается в фоне).
// Возвращает true, если атрибуты были изменены.
bool PropsDialog(HWND parent, ThemeId theme, const std::wstring& path);

// Alt+Down: история каталогов за сессию. Возвращает выбранный путь
// (может совпадать с текущим) или пустую строку при отмене.
std::wstring HistoryDialog(HWND parent, ThemeId theme, const std::wstring& currentPath);

// Всплывающий список подкаталогов каталога dir (для хлебных крошек).
// Возвращает выбранный полный путь или "".
std::wstring SubdirPopupDialog(HWND parent, ThemeId theme, int x, int y,
                               const std::wstring& dir);
