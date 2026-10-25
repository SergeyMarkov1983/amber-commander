#pragma once
#include "common.h"
#include "render.h"

// Собственный векторный набор иконок в тёплом стиле (рисуется Direct2D).
enum class IconId {
    Folder, Up, Drive, Exe, Text, Image, Video, Audio, Archive, Font, Sys, Link, File
};

IconId IconFor(const DirEntry& e);
void DrawIcon(Renderer& r, IconId id, const D2D1_RECT_F& rect, const Palette& pal);
