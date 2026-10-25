#pragma once
#include "common.h"

// Глобальный индекс каталогов (мгновенный поиск папок на всех дисках,
// в духе Everything): фоновая сборка по MFT (NTFS) при первом запуске,
// файл индекса лежит рядом с amber.ini, запрос — бинарный поиск
// по memory-mapped файлу (память приложения не растёт).

// Запуск фоновой сборки/загрузки индекса (безопасно вызывать многократно).
void StartFolderIndexBuild();

// Индекс готов к запросам.
bool FolderIndexReady();

// Идёт сборка (можно показать пользователю).
bool FolderIndexBusy();

// Папки, чьё ИМЯ начинается с prefix (регистронезависимо).
// Возвращает число найденных (out — не больше limit записей, полные пути).
size_t FolderIndexQuery(const std::wstring& prefix, std::vector<std::wstring>& out,
                        size_t limit);
