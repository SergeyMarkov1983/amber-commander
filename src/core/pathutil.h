#pragma once
#include "common.h"

std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring PathParent(const std::wstring& p);
std::wstring PathFileName(const std::wstring& p);
std::wstring PathExtension(const std::wstring& p);      // с точкой, в нижнем регистре
bool PathIsRoot(const std::wstring& p);
std::wstring PathNormalize(const std::wstring& p);      // без хвостовых слэшей (кроме корня)
std::wstring RootOf(const std::wstring& p);             // "C:\"
std::wstring GetExeDir();
std::wstring GetIniPath();   // amber.ini рядом с exe; если нельзя — %APPDATA%\Amber Commander
std::wstring FormatSize(uint64_t v);                    // "1,2 ГБ"
std::wstring FormatDate(const FILETIME& ft);            // "24.10.2026 14:05"
bool IsSameVolume(const std::wstring& a, const std::wstring& b);
std::wstring LongPath(const std::wstring& p);           // \\?\ префикс
bool FileExists(const std::wstring& p);
bool DirExists(const std::wstring& p);
void Trim(std::wstring& s);
