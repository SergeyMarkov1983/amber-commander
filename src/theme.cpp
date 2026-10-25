#include "theme.h"

// Цвета имён по типам: в обычных темах всё = основному тексту,
// скрытые — слегка приглушены (dimText).
static void DefaultTypeTexts(Palette& p) {
    p.textFolder = p.textExe = p.textImage = p.textVideo = p.textAudio =
        p.textArchive = p.textText = p.textFont = p.textLink = p.textSys = p.text;
    p.textHidden = p.dimText;
    p.btnBg = p.panelBg;
    p.titleText = p.headerText;
}

// Палитра «Янтарь» — по макету: почти чёрный тёплый фон, янтарные акценты
// (#EFA33F), кремовый текст, янтарное выделение (#965003).
static Palette MakeLight() {
    Palette p;
    p.bg                = Rgb(14, 8, 1);       // почти чёрный тёплый
    p.panelBg           = Rgb(17, 13, 8);
    p.headerActive      = Rgb(46, 28, 10);     // тёмная янтарная шапка
    p.headerInactive    = Rgb(12, 8, 4);
    p.headerText        = Rgb(236, 234, 229);  // кремовый
    p.headerTextInactive= Rgb(160, 142, 112);
    p.titleText         = Rgb(236, 234, 229);
    p.text              = Rgb(236, 234, 229);
    p.dimText           = Rgb(171, 154, 122);
    p.selBg             = Rgb(126, 68, 6);     // янтарное выделение
    p.cursorBg          = Rgb(150, 80, 3);     // яркий янтарь курсора
    p.selText           = Rgb(254, 253, 236);
    p.border            = Rgb(72, 29, 0);
    p.statusBg          = Rgb(14, 11, 7);
    p.statusText        = Rgb(171, 154, 122);
    p.accent            = Rgb(239, 163, 63);   // янтарь-акцент
    p.accentText        = Rgb(24, 12, 2);
    p.cmdLineBg         = Rgb(12, 8, 4);
    p.cmdLineText       = Rgb(236, 234, 229);
    p.keybarBg          = Rgb(14, 11, 7);
    p.keybarText        = Rgb(171, 154, 122);
    p.error             = Rgb(235, 90, 70);
    p.btnBg             = Rgb(38, 22, 4);      // кнопки в стиле макета
    // иконки — янтарные/тёплые
    p.iconFolder        = Rgb(239, 163, 63);
    p.iconFolderTab     = Rgb(203, 122, 26);
    p.iconFile          = Rgb(120, 100, 70);
    p.iconExe           = Rgb(239, 163, 63);
    p.iconText          = Rgb(255, 233, 136);
    p.iconImage         = Rgb(230, 150, 80);
    p.iconVideo         = Rgb(215, 130, 70);
    p.iconAudio         = Rgb(200, 150, 90);
    p.iconArchive       = Rgb(233, 157, 64);
    p.iconDrive         = Rgb(110, 80, 50);
    p.iconUp            = Rgb(200, 140, 60);
    p.iconLink          = Rgb(180, 140, 90);
    p.iconFont          = Rgb(190, 130, 110);
    p.iconSys           = Rgb(130, 110, 90);
    p.iconOutline       = Rgb(250, 220, 170);
    p.iconShadow        = Rgb(6, 4, 2);
    p.iconHighlight     = Rgb(255, 244, 214);
    p.iconInk           = Rgb(70, 45, 18);
    p.sizeBar           = Rgb(96, 54, 8);      // тёмный янтарь полос размеров
    // имена: папки янтарные, остальное кремовое
    p.textFolder        = Rgb(239, 163, 63);
    p.textExe           = Rgb(236, 234, 229);
    p.textImage         = Rgb(236, 234, 229);
    p.textVideo         = Rgb(236, 234, 229);
    p.textAudio         = Rgb(236, 234, 229);
    p.textArchive       = Rgb(236, 234, 229);
    p.textText          = Rgb(236, 234, 229);
    p.textFont          = Rgb(236, 234, 229);
    p.textLink          = Rgb(255, 233, 136);
    p.textSys           = Rgb(200, 184, 160);
    p.textHidden        = Rgb(120, 108, 90);
    return p;
}

// Палитра «Кофе/Какао» (тёмная тёплая)
// Кофейные фоны, какао-шапка, карамельные акценты; контуры — кремовые.
static Palette MakeDark() {
    Palette p;
    p.bg                = Rgb(29, 22, 17);
    p.panelBg           = Rgb(37, 29, 22);
    p.headerActive      = Rgb(142, 93, 58);       // кофейная шапка
    p.headerInactive    = Rgb(47, 37, 29);
    p.headerText        = Rgb(242, 228, 206);
    p.headerTextInactive= Rgb(170, 146, 122);
    p.text              = Rgb(237, 224, 206);
    p.dimText           = Rgb(162, 142, 118);
    p.selBg             = Rgb(96, 72, 52);        // какао-выделение
    p.cursorBg          = Rgb(116, 87, 61);
    p.selText           = Rgb(245, 233, 214);
    p.border            = Rgb(58, 47, 36);
    p.statusBg          = Rgb(44, 35, 27);
    p.statusText        = Rgb(181, 157, 128);
    p.accent            = Rgb(226, 146, 106);     // карамельная терракота
    p.accentText        = Rgb(44, 33, 24);
    p.cmdLineBg         = Rgb(33, 25, 20);
    p.cmdLineText       = Rgb(237, 224, 206);
    p.keybarBg          = Rgb(41, 32, 25);
    p.keybarText        = Rgb(198, 176, 146);
    p.error             = Rgb(232, 126, 94);
    p.iconFolder        = Rgb(212, 142, 64);
    p.iconFolderTab     = Rgb(172, 112, 45);
    p.iconFile          = Rgb(132, 114, 90);
    p.iconExe           = Rgb(206, 116, 76);
    p.iconText          = Rgb(150, 128, 96);
    p.iconImage         = Rgb(130, 140, 80);
    p.iconVideo         = Rgb(162, 78, 68);
    p.iconAudio         = Rgb(180, 128, 60);
    p.iconArchive       = Rgb(156, 122, 66);
    p.iconDrive         = Rgb(130, 112, 90);
    p.iconUp            = Rgb(172, 124, 64);
    p.iconLink          = Rgb(154, 92, 62);
    p.iconFont          = Rgb(142, 94, 106);
    p.iconSys           = Rgb(114, 108, 94);
    p.iconOutline       = Rgb(234, 216, 188);     // кремовый контур
    p.iconShadow        = Rgb(20, 16, 12);        // глубокая тень
    p.iconHighlight     = Rgb(250, 232, 204);     // светлый блик
    p.iconInk           = Rgb(56, 44, 32);        // «чернила» строк/деталей
    p.sizeBar           = Rgb(21, 16, 11);        // полоса размера: темнее фона панели
    DefaultTypeTexts(p);
    return p;
}

// Палитра «Холодная» (бледно-синие, серо-синие оттенки).
// Полоса размера — комплементарный тёплый янтарь по кругу Иттена.
static Palette MakeCool() {
    Palette p;
    p.bg                = Rgb(206, 213, 222);
    p.panelBg           = Rgb(220, 226, 232);
    p.headerActive      = Rgb(122, 152, 180);
    p.headerInactive    = Rgb(186, 199, 211);
    p.headerText        = Rgb(36, 52, 68);
    p.headerTextInactive= Rgb(92, 112, 132);
    p.text              = Rgb(44, 60, 76);
    p.dimText           = Rgb(110, 128, 146);
    p.selBg             = Rgb(196, 214, 230);
    p.cursorBg          = Rgb(173, 196, 218);
    p.selText           = Rgb(44, 60, 76);
    p.border            = Rgb(159, 174, 190);
    p.statusBg          = Rgb(195, 208, 219);
    p.statusText        = Rgb(72, 90, 106);
    p.accent            = Rgb(66, 110, 152);
    p.accentText        = Rgb(240, 246, 250);
    p.cmdLineBg         = Rgb(216, 222, 228);
    p.cmdLineText       = Rgb(44, 60, 76);
    p.keybarBg          = Rgb(186, 199, 211);
    p.keybarText        = Rgb(72, 90, 106);
    p.error             = Rgb(176, 68, 56);
    p.iconFolder        = Rgb(122, 152, 180);
    p.iconFolderTab     = Rgb(96, 124, 152);
    p.iconFile          = Rgb(196, 205, 214);
    p.iconExe           = Rgb(66, 110, 152);
    p.iconText          = Rgb(222, 230, 238);
    p.iconImage         = Rgb(96, 132, 120);
    p.iconVideo         = Rgb(134, 98, 110);
    p.iconAudio         = Rgb(152, 128, 88);
    p.iconArchive       = Rgb(142, 138, 120);
    p.iconDrive         = Rgb(178, 190, 202);
    p.iconUp            = Rgb(110, 138, 166);
    p.iconLink          = Rgb(110, 126, 150);
    p.iconFont          = Rgb(128, 112, 138);
    p.iconSys           = Rgb(130, 134, 132);
    p.iconOutline       = Rgb(76, 94, 112);
    p.iconShadow        = Rgb(171, 182, 195);
    p.iconHighlight     = Rgb(248, 251, 254);
    p.iconInk           = Rgb(84, 102, 120);
    p.sizeBar           = Rgb(232, 161, 60);   // янтарь — комплемент серо-синему (Иттен)
    DefaultTypeTexts(p);
    return p;
}

// Палитра «Фосфор» — по макету «Зеленая тема»: тёмный зелёный монитор,
// ярко-зелёный текст, зелёные рамки, яркий курсор-блок с тёмным текстом.
static Palette MakeGreen() {
    Palette p;
    p.bg                = Rgb(2, 8, 2);        // почти чёрный зелёный
    p.panelBg           = Rgb(3, 11, 3);
    p.headerActive      = Rgb(24, 52, 24);
    p.headerInactive    = Rgb(4, 10, 4);
    p.headerText        = Rgb(160, 240, 112);
    p.headerTextInactive= Rgb(96, 152, 88);
    p.titleText         = Rgb(160, 240, 112);
    p.text              = Rgb(160, 240, 112);   // ярко-зелёный текст
    p.dimText           = Rgb(88, 140, 80);
    p.selBg             = Rgb(80, 140, 72);     // средний зелёный
    p.cursorBg          = Rgb(96, 216, 64);     // яркий фосфорный курсор
    p.selText           = Rgb(8, 26, 8);        // тёмный текст на выделении
    p.border            = Rgb(56, 104, 56);
    p.statusBg          = Rgb(2, 8, 2);
    p.statusText        = Rgb(96, 152, 88);
    p.accent            = Rgb(96, 216, 64);
    p.accentText        = Rgb(6, 20, 6);
    p.cmdLineBg         = Rgb(1, 6, 1);
    p.cmdLineText       = Rgb(160, 240, 112);
    p.keybarBg          = Rgb(2, 8, 2);
    p.keybarText        = Rgb(96, 152, 88);
    p.error             = Rgb(255, 96, 80);
    p.btnBg             = Rgb(22, 40, 22);
    // иконки — оттенки зелёного фосфора
    p.iconFolder        = Rgb(120, 210, 100);
    p.iconFolderTab     = Rgb(84, 148, 68);
    p.iconFile          = Rgb(88, 120, 84);
    p.iconExe           = Rgb(96, 216, 64);
    p.iconText          = Rgb(150, 220, 130);
    p.iconImage         = Rgb(90, 170, 110);
    p.iconVideo         = Rgb(130, 200, 90);
    p.iconAudio         = Rgb(100, 190, 150);
    p.iconArchive       = Rgb(150, 190, 80);
    p.iconDrive         = Rgb(76, 110, 76);
    p.iconUp            = Rgb(120, 200, 120);
    p.iconLink          = Rgb(120, 210, 160);
    p.iconFont          = Rgb(140, 200, 140);
    p.iconSys           = Rgb(92, 112, 92);
    p.iconOutline       = Rgb(170, 230, 170);
    p.iconShadow        = Rgb(1, 4, 1);
    p.iconHighlight     = Rgb(220, 255, 220);
    p.iconInk           = Rgb(26, 50, 26);
    p.sizeBar           = Rgb(28, 54, 28);      // светлее тёмного фона панели
    // имена — монохромный фосфор, программы и ярлыки чуть ярче
    p.textFolder        = Rgb(160, 240, 112);
    p.textExe           = Rgb(96, 216, 64);
    p.textImage         = Rgb(160, 240, 112);
    p.textVideo         = Rgb(160, 240, 112);
    p.textAudio         = Rgb(160, 240, 112);
    p.textArchive       = Rgb(160, 240, 112);
    p.textText          = Rgb(160, 240, 112);
    p.textFont          = Rgb(160, 240, 112);
    p.textLink          = Rgb(120, 210, 160);
    p.textSys           = Rgb(120, 170, 110);
    p.textHidden        = Rgb(70, 110, 70);
    return p;
}

// Палитра «Сталь» — нейтрально-серая (светлый серебристый).
static Palette MakeGray() {
    Palette p;
    p.bg                = Rgb(206, 209, 214);
    p.panelBg           = Rgb(219, 222, 227);
    p.headerActive      = Rgb(140, 148, 158);
    p.headerInactive    = Rgb(192, 197, 204);
    p.headerText        = Rgb(32, 36, 42);
    p.headerTextInactive= Rgb(90, 96, 104);
    p.text              = Rgb(40, 44, 50);
    p.dimText           = Rgb(118, 124, 132);
    p.selBg             = Rgb(196, 202, 212);
    p.cursorBg          = Rgb(178, 186, 198);
    p.selText           = Rgb(40, 44, 50);
    p.border            = Rgb(165, 170, 179);
    p.statusBg          = Rgb(198, 203, 210);
    p.statusText        = Rgb(80, 86, 94);
    p.accent            = Rgb(90, 100, 116);
    p.accentText        = Rgb(244, 246, 248);
    p.cmdLineBg         = Rgb(212, 215, 220);
    p.cmdLineText       = Rgb(40, 44, 50);
    p.keybarBg          = Rgb(192, 197, 204);
    p.keybarText        = Rgb(80, 86, 94);
    p.error             = Rgb(190, 80, 70);
    p.iconFolder        = Rgb(150, 156, 166);
    p.iconFolderTab     = Rgb(124, 130, 140);
    p.iconFile          = Rgb(186, 190, 196);
    p.iconExe           = Rgb(90, 100, 116);
    p.iconText          = Rgb(214, 218, 224);
    p.iconImage         = Rgb(120, 140, 130);
    p.iconVideo         = Rgb(140, 112, 118);
    p.iconAudio         = Rgb(150, 132, 96);
    p.iconArchive       = Rgb(138, 134, 118);
    p.iconDrive         = Rgb(164, 170, 178);
    p.iconUp            = Rgb(118, 130, 146);
    p.iconLink          = Rgb(116, 128, 142);
    p.iconFont          = Rgb(128, 116, 142);
    p.iconSys           = Rgb(124, 128, 130);
    p.iconOutline       = Rgb(70, 76, 84);
    p.iconShadow        = Rgb(155, 162, 171);
    p.iconHighlight     = Rgb(250, 251, 252);
    p.iconInk           = Rgb(74, 80, 88);
    p.sizeBar           = Rgb(110, 116, 124);   // тёмно-серый, темнее фона
    DefaultTypeTexts(p);
    return p;
}

// Палитра «FAR» — классическая VGA-тема FAR Manager: синие панели #000080,
// циановая шапка активной, синие выделения, белый текст, жёлтые папки.
static Palette MakeNorton() {
    Palette p;
    p.bg                = Rgb(0, 0, 128);
    p.panelBg           = Rgb(0, 0, 128);
    p.headerActive      = Rgb(0, 255, 255);    // циан — активная панель
    p.headerInactive    = Rgb(0, 0, 128);
    p.headerText        = Rgb(0, 0, 0);        // чёрный на циане
    p.headerTextInactive= Rgb(150, 150, 190);
    p.titleText         = Rgb(232, 232, 232);  // заголовок окна — белый на синем
    p.text              = Rgb(255, 255, 255);
    p.dimText           = Rgb(128, 128, 128);
    p.selBg             = Rgb(0, 0, 255);
    p.cursorBg          = Rgb(30, 30, 255);
    p.selText           = Rgb(255, 255, 255);
    p.border            = Rgb(0, 255, 255);    // циановые рамки
    p.statusBg          = Rgb(0, 0, 255);
    p.statusText        = Rgb(255, 255, 255);
    p.accent            = Rgb(255, 255, 0);    // жёлтый акцент
    p.accentText        = Rgb(0, 0, 0);
    p.cmdLineBg         = Rgb(0, 0, 96);
    p.cmdLineText       = Rgb(255, 255, 255);
    p.keybarBg          = Rgb(0, 0, 255);
    p.keybarText        = Rgb(255, 255, 255);
    p.error             = Rgb(255, 80, 80);
    p.btnBg             = Rgb(24, 24, 168);
    // иконки в духе FAR (папки жёлтые)
    p.iconFolder        = Rgb(255, 255, 0);
    p.iconFolderTab     = Rgb(200, 200, 0);
    p.iconFile          = Rgb(180, 180, 190);
    p.iconExe           = Rgb(255, 255, 255);
    p.iconText          = Rgb(200, 200, 210);
    p.iconImage         = Rgb(0, 255, 255);
    p.iconVideo         = Rgb(255, 128, 255);
    p.iconAudio         = Rgb(0, 255, 128);
    p.iconArchive       = Rgb(255, 200, 0);
    p.iconDrive         = Rgb(150, 150, 200);
    p.iconUp            = Rgb(255, 255, 0);
    p.iconLink          = Rgb(0, 255, 255);
    p.iconFont          = Rgb(255, 128, 255);
    p.iconSys           = Rgb(170, 170, 180);
    p.iconOutline       = Rgb(220, 220, 255);
    p.iconShadow        = Rgb(0, 0, 60);
    p.iconHighlight     = Rgb(255, 255, 255);
    p.iconInk           = Rgb(0, 0, 80);
    p.sizeBar           = Rgb(0, 0, 255);      // синие полосы размеров
    // имена: папки жёлтые (как в FAR), остальное белым
    p.textFolder        = Rgb(255, 255, 0);
    p.textExe           = Rgb(255, 255, 255);
    p.textImage         = Rgb(255, 255, 255);
    p.textVideo         = Rgb(255, 255, 255);
    p.textAudio         = Rgb(255, 255, 255);
    p.textArchive       = Rgb(255, 255, 255);
    p.textText          = Rgb(255, 255, 255);
    p.textFont          = Rgb(255, 255, 255);
    p.textLink          = Rgb(0, 255, 255);
    p.textSys           = Rgb(180, 180, 190);
    p.textHidden        = Rgb(120, 120, 140);
    return p;
}

// Палитра «Спектр» — многоцветная, на основе проверенных тёмных схем
// (One Dark / Dracula / Nord): тёмная сине-серая база, сочные, но мягкие
// цвета по типам файлов, контраст текста ≥ 4.5:1. Скрытые файлы рисуются
// приглушённо (смешение цвета типа с фоном). Полоса размера — индиго.
static Palette MakeVivid() {
    Palette p;
    p.bg                = Rgb(16, 18, 26);      // глубокий тёмный сине-графит
    p.panelBg           = Rgb(21, 23, 32);
    p.headerActive      = Rgb(90, 110, 210);    // сочный индиго
    p.headerInactive    = Rgb(28, 32, 43);
    p.headerText        = Rgb(244, 248, 255);
    p.headerTextInactive= Rgb(156, 166, 188);
    p.titleText         = Rgb(244, 248, 255);
    p.text              = Rgb(224, 230, 244);   // нейтральный светлый
    p.dimText           = Rgb(132, 142, 162);
    p.selBg             = Rgb(50, 58, 82);
    p.cursorBg          = Rgb(70, 82, 118);
    p.selText           = Rgb(250, 252, 255);
    p.border            = Rgb(38, 44, 58);
    p.statusBg          = Rgb(18, 20, 28);
    p.statusText        = Rgb(160, 170, 194);
    p.accent            = Rgb(255, 190, 48);    // сочный янтарь — фирменный акцент
    p.accentText        = Rgb(32, 24, 8);
    p.cmdLineBg         = Rgb(13, 15, 22);
    p.cmdLineText       = Rgb(230, 236, 250);
    p.keybarBg          = Rgb(18, 20, 28);
    p.keybarText        = Rgb(160, 170, 194);
    p.error             = Rgb(255, 96, 84);
    // кнопки диалогов: светлее фона диалога, иначе сольются
    p.btnBg             = Rgb(52, 62, 88);
    // иконки — в цвет своего типа
    p.iconFolder        = Rgb(255, 208, 64);
    p.iconFolderTab     = Rgb(224, 172, 48);
    p.iconFile          = Rgb(128, 138, 158);
    p.iconExe           = Rgb(96, 235, 90);
    p.iconText          = Rgb(210, 216, 232);
    p.iconImage         = Rgb(235, 110, 255);
    p.iconVideo         = Rgb(255, 116, 86);
    p.iconAudio         = Rgb(64, 226, 214);
    p.iconArchive       = Rgb(255, 170, 64);
    p.iconDrive         = Rgb(140, 150, 170);
    p.iconUp            = Rgb(255, 208, 64);
    p.iconLink          = Rgb(92, 168, 255);
    p.iconFont          = Rgb(180, 130, 255);
    p.iconSys           = Rgb(168, 176, 192);
    p.iconOutline       = Rgb(228, 236, 250);
    p.iconShadow        = Rgb(10, 12, 18);
    p.iconHighlight     = Rgb(255, 255, 255);
    p.iconInk           = Rgb(48, 56, 76);
    // Полосы размеров: индиго (родня шапки) — читаем белым текстом
    // (контраст ~7.7:1), заметен на сине-сером фоне панели.
    p.sizeBar           = Rgb(58, 84, 164);
    // цвета имён по типам: сочные, читаемые на тёмном
    p.textFolder        = Rgb(255, 208, 64);    // золото — каталоги
    p.textExe           = Rgb(96, 235, 90);     // зелёный — программы
    p.textImage         = Rgb(235, 110, 255);   // фуксия — картинки
    p.textVideo         = Rgb(255, 116, 86);    // коралл — видео
    p.textAudio         = Rgb(64, 226, 214);    // бирюза — музыка
    p.textArchive       = Rgb(255, 170, 64);    // оранж — архивы
    p.textText          = Rgb(224, 230, 244);   // светлый — документы/код
    p.textFont          = Rgb(180, 130, 255);   // лаванда — шрифты
    p.textLink          = Rgb(92, 168, 255);    // голубой — ярлыки
    p.textSys           = Rgb(168, 176, 192);   // серый — системные
    p.textHidden        = Rgb(120, 126, 140);   // скрытые: базовая приглушка
    return p;
}

// Палитра «Ночной циан» — по мотивам макета NexusFile: почти чёрный
// сине-графитовый фон, яркий циан-акцент (#17DBF9), пурпурная вторичка,
// золотые папки, цвета имён по типам файлов.
static Palette MakeNight() {
    Palette p;
    p.bg                = Rgb(3, 8, 13);
    p.panelBg           = Rgb(10, 17, 24);
    p.headerActive      = Rgb(18, 58, 68);     // тёмный циан
    p.headerInactive    = Rgb(9, 15, 21);
    p.headerText        = Rgb(232, 250, 252);
    p.headerTextInactive= Rgb(120, 140, 150);
    p.titleText         = Rgb(232, 250, 252);
    p.text              = Rgb(240, 246, 248);
    p.dimText           = Rgb(110, 122, 128);
    p.selBg             = Rgb(24, 56, 70);
    p.cursorBg          = Rgb(30, 70, 86);
    p.selText           = Rgb(23, 219, 249);  // циан — как подсветка имени в макете
    p.border            = Rgb(26, 38, 48);
    p.statusBg          = Rgb(8, 15, 23);
    p.statusText        = Rgb(120, 140, 152);
    p.accent            = Rgb(23, 219, 249);  // циан-акцент
    p.accentText        = Rgb(4, 12, 16);
    p.cmdLineBg         = Rgb(7, 14, 21);
    p.cmdLineText       = Rgb(240, 246, 248);
    p.keybarBg          = Rgb(8, 15, 23);
    p.keybarText        = Rgb(150, 170, 182);
    p.error             = Rgb(235, 60, 55);
    p.btnBg             = Rgb(34, 52, 66);    // кнопки заметны на фоне диалога
    // иконки
    p.iconFolder        = Rgb(255, 203, 83);  // золото папок
    p.iconFolderTab     = Rgb(212, 162, 52);
    p.iconFile          = Rgb(110, 120, 130);
    p.iconExe           = Rgb(120, 220, 120);
    p.iconText          = Rgb(200, 210, 220);
    p.iconImage         = Rgb(170, 110, 250);
    p.iconVideo         = Rgb(234, 176, 13);
    p.iconAudio         = Rgb(100, 210, 200);
    p.iconArchive       = Rgb(255, 120, 40);
    p.iconDrive         = Rgb(100, 120, 135);
    p.iconUp            = Rgb(200, 170, 110);
    p.iconLink          = Rgb(80, 140, 250);
    p.iconFont          = Rgb(240, 120, 190);
    p.iconSys           = Rgb(130, 140, 150);
    p.iconOutline       = Rgb(220, 235, 240);
    p.iconShadow        = Rgb(3, 6, 10);
    p.iconHighlight     = Rgb(240, 250, 255);
    p.iconInk           = Rgb(30, 48, 58);
    p.sizeBar           = Rgb(136, 66, 110);  // пурпур — вторичный акцент макета
    // цвета имён по типам (по макету: .md розовый, .yml оранж, .pdf красный…)
    p.textFolder        = Rgb(255, 203, 83);
    p.textExe           = Rgb(120, 220, 120);
    p.textImage         = Rgb(170, 110, 250);
    p.textVideo         = Rgb(234, 176, 13);
    p.textAudio         = Rgb(100, 210, 200);
    p.textArchive       = Rgb(255, 120, 40);
    p.textText          = Rgb(240, 246, 248);
    p.textFont          = Rgb(240, 120, 190);
    p.textLink          = Rgb(80, 140, 250);
    p.textSys           = Rgb(130, 140, 150);
    p.textHidden        = Rgb(70, 80, 88);
    return p;
}

// Палитра «Уголь» — по мотивам TUI OpenCode: антрацитовый фон #141414,
// блоки #0A0A0A, серый текст #EEEEEE, синий акцент #5C9CF5,
// тёплый оранж #97682C (как ключевые слова).
static Palette MakeCoal() {
    Palette p;
    p.bg                = Rgb(20, 20, 20);
    p.panelBg           = Rgb(14, 14, 14);
    p.headerActive      = Rgb(40, 66, 110);    // тёмно-синяя шапка
    p.headerInactive    = Rgb(26, 26, 26);
    p.headerText        = Rgb(238, 238, 238);
    p.headerTextInactive= Rgb(128, 128, 128);
    p.titleText         = Rgb(238, 238, 238);
    p.text              = Rgb(238, 238, 238);
    p.dimText           = Rgb(128, 128, 128);
    p.selBg             = Rgb(38, 50, 66);
    p.cursorBg          = Rgb(48, 74, 110);
    p.selText           = Rgb(238, 238, 238);
    p.border            = Rgb(34, 34, 34);
    p.statusBg          = Rgb(24, 24, 24);
    p.statusText        = Rgb(150, 150, 150);
    p.accent            = Rgb(92, 156, 245);   // синий — активные элементы
    p.accentText        = Rgb(238, 238, 238);
    p.cmdLineBg         = Rgb(12, 12, 12);
    p.cmdLineText       = Rgb(238, 238, 238);
    p.keybarBg          = Rgb(30, 30, 30);
    p.keybarText        = Rgb(150, 150, 150);
    p.error             = Rgb(220, 80, 70);
    p.btnBg             = Rgb(46, 46, 46);
    // иконки — серые, как файлы в TUI; программы — оранж, ярлыки — синий
    p.iconFolder        = Rgb(150, 150, 150);
    p.iconFolderTab     = Rgb(120, 120, 120);
    p.iconFile          = Rgb(110, 110, 110);
    p.iconExe           = Rgb(151, 104, 44);
    p.iconText          = Rgb(200, 200, 200);
    p.iconImage         = Rgb(160, 160, 160);
    p.iconVideo         = Rgb(160, 160, 160);
    p.iconAudio         = Rgb(160, 160, 160);
    p.iconArchive       = Rgb(160, 160, 160);
    p.iconDrive         = Rgb(130, 130, 130);
    p.iconUp            = Rgb(170, 170, 170);
    p.iconLink          = Rgb(92, 156, 245);
    p.iconFont          = Rgb(160, 160, 160);
    p.iconSys           = Rgb(120, 120, 120);
    p.iconOutline       = Rgb(60, 60, 60);
    p.iconShadow        = Rgb(8, 8, 8);
    p.iconHighlight     = Rgb(250, 250, 250);
    p.iconInk           = Rgb(70, 70, 70);
    p.sizeBar           = Rgb(151, 104, 44);   // тёплый оранж
    // имена — монохромно (как в TUI), лишь программы и ярлыки подкрашены
    p.textFolder        = Rgb(238, 238, 238);
    p.textExe           = Rgb(151, 104, 44);
    p.textImage         = Rgb(238, 238, 238);
    p.textVideo         = Rgb(238, 238, 238);
    p.textAudio         = Rgb(238, 238, 238);
    p.textArchive       = Rgb(238, 238, 238);
    p.textText          = Rgb(238, 238, 238);
    p.textFont          = Rgb(238, 238, 238);
    p.textLink          = Rgb(92, 156, 245);
    p.textSys           = Rgb(160, 160, 160);
    p.textHidden        = Rgb(100, 100, 100);
    return p;
}

const Palette& GetPalette(ThemeId t) {
    static const Palette light = MakeLight();
    static const Palette dark = MakeDark();
    static const Palette cool = MakeCool();
    static const Palette green = MakeGreen();
    static const Palette gray = MakeGray();
    static const Palette norton = MakeNorton();
    static const Palette vivid = MakeVivid();
    static const Palette night = MakeNight();
    static const Palette coal = MakeCoal();
    switch (t) {
    case ThemeId::Light: return light;
    case ThemeId::Dark: return dark;
    case ThemeId::Cool: return cool;
    case ThemeId::Green: return green;
    case ThemeId::Gray: return gray;
    case ThemeId::Norton: return norton;
    case ThemeId::Vivid: return vivid;
    case ThemeId::Night: return night;
    default: return coal;
    }
}

const wchar_t* ThemeName(ThemeId t) {
    switch (t) {
    case ThemeId::Light: return L"Янтарь";
    case ThemeId::Dark: return L"Кофе/Какао";
    case ThemeId::Cool: return L"Холодная";
    case ThemeId::Green: return L"Фосфор";
    case ThemeId::Gray: return L"Сталь";
    case ThemeId::Norton: return L"FAR";
    case ThemeId::Vivid: return L"Спектр";
    case ThemeId::Night: return L"Ночной циан";
    default: return L"Уголь";
    }
}
