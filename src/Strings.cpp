#include "Strings.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace Strings {
namespace {

struct Entry {
    const wchar_t* text[kLanguageCount];
};

const Entry kTable[] = {
#define EDGEDECK_STRING_ENTRY(id, en, es) {{en, es}},
    EDGEDECK_STRINGS(EDGEDECK_STRING_ENTRY)
#undef EDGEDECK_STRING_ENTRY
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(Id::Count),
              "one table entry per string id");

Language g_current = Language::English;

} // namespace

Language Current() { return g_current; }
void SetCurrent(Language language) {
    const int index = static_cast<int>(language);
    g_current = (index >= 0 && index < kLanguageCount) ? language : Language::English;
}

const wchar_t* Code(Language language) { return language == Language::Spanish ? L"es" : L"en"; }

Language FromCode(const std::wstring& code) {
    return (code == L"es" || code == L"ES") ? Language::Spanish : Language::English;
}

Language ForWindowsLanguage(unsigned short langId) {
    return PRIMARYLANGID(langId) == LANG_SPANISH ? Language::Spanish : Language::English;
}

Language FromWindows() { return ForWindowsLanguage(GetUserDefaultUILanguage()); }

const wchar_t* NativeName(Language language) {
    return language == Language::Spanish ? L"Español" : L"English";
}

const wchar_t* Get(Id id, Language language) {
    const int index = static_cast<int>(id);
    const int lang = static_cast<int>(language);
    if (index < 0 || index >= static_cast<int>(Id::Count)) return L"";
    if (lang < 0 || lang >= kLanguageCount) return kTable[index].text[0];
    return kTable[index].text[lang];
}

const wchar_t* Get(Id id) { return Get(id, g_current); }

std::wstring Format(Id id, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, id);
    const int written = _vsnwprintf_s(buffer, _TRUNCATE, Get(id), args);
    va_end(args);
    return written < 0 && buffer[0] == L'\0' ? std::wstring() : std::wstring(buffer);
}

} // namespace Strings
