#pragma once
#include "common.h"
#include "theme.h"

// Встроенный просмотрщик F3: текст (UTF-8/UTF-16/ANSI).
void ShowViewer(HWND parent, ThemeId theme, const std::wstring& path);
