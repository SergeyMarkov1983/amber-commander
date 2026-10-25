#pragma once
#include "common.h"
#include <d2d1.h>

// Тёплые палитры менеджера.
struct Palette {
    // общие
    D2D1_COLOR_F bg, panelBg;
    D2D1_COLOR_F headerActive, headerInactive, headerText, headerTextInactive;
    D2D1_COLOR_F titleText;   // текст заголовка окна (обычно = headerText)
    D2D1_COLOR_F text, dimText;
    D2D1_COLOR_F selBg, cursorBg, selText;
    D2D1_COLOR_F border;
    D2D1_COLOR_F statusBg, statusText;
    D2D1_COLOR_F accent, accentText;
    D2D1_COLOR_F cmdLineBg, cmdLineText;
    D2D1_COLOR_F keybarBg, keybarText;
    D2D1_COLOR_F error;
    D2D1_COLOR_F btnBg;   // фон кнопок диалогов (обычно = panelBg)
    // иконки
    D2D1_COLOR_F iconFolder, iconFolderTab, iconFile, iconExe, iconText, iconImage,
        iconVideo, iconAudio, iconArchive, iconDrive, iconUp, iconLink, iconFont, iconSys;
    // полоса размера в режиме «Размеры» (темнее фона панели)
    D2D1_COLOR_F sizeBar;
    // иконки: единый контур, тёплая тень-подложка, блик и «чернила» деталей
    D2D1_COLOR_F iconOutline, iconShadow, iconHighlight, iconInk;
    // цвета имён по типу (для «Спектра»; в остальных темах = text)
    D2D1_COLOR_F textFolder, textExe, textImage, textVideo, textAudio, textArchive,
        textText, textFont, textLink, textSys;
    // приглушённый цвет скрытых файлов
    D2D1_COLOR_F textHidden;
};

enum class ThemeId { Light, Dark, Cool, Green, Gray, Norton, Vivid, Night, Coal };

const Palette& GetPalette(ThemeId t);
const wchar_t* ThemeName(ThemeId t);

inline D2D1_COLOR_F Rgb(int r, int g, int b) {
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f);
}

// D2D1_COLOR_F -> COLORREF (для GDI-отрисовки)
inline COLORREF PalRef(const D2D1_COLOR_F& c) {
    return RGB((int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f),
               (int)(c.b * 255.0f + 0.5f));
}
