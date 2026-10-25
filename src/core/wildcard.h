#pragma once
#include "common.h"

// Сопоставление масок имён файлов. Поддерживает '*' и '?'.
bool WildMatch(const wchar_t* pat, const wchar_t* str);
bool WildMatchI(const wchar_t* pat, const wchar_t* str);       // без учёта регистра
bool WildMatchI(const std::wstring& pat, const std::wstring& str);

// Разбивает "*.cpp; *.h, *.hpp" на отдельные маски (без пробелов)
void SplitMasks(const std::wstring& s, std::vector<std::wstring>& out);
// true, если имя подходит хотя бы под одну маску (пустой список = всё)
bool MatchAnyMask(const std::vector<std::wstring>& masks, const std::wstring& name);
