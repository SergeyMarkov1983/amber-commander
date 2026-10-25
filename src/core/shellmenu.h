#pragma once
#include "common.h"

struct IContextMenu;

// Показывает системное контекстное меню оболочки (как в Проводнике —
// «классическое» меню Windows 11) для списка файлов/каталогов.
// x, y — экранные координаты. Возвращает true, если выполнена какая-либо
// команда (после этого панели стоит обновить).
bool ShowShellContextMenu(HWND hwnd, int x, int y, const std::vector<std::wstring>& paths);

// Активное меню — окно пробрасывает ему WM_INITMENUPOPUP/WM_DRAWITEM/WM_MEASUREITEM
// (без этого расширения оболочки не достраивают подменю, а команды дают ошибки).
void SetActiveShellMenu(IContextMenu* cm);
void HandleShellMenuMsg(UINT msg, WPARAM wParam, LPARAM lParam);
