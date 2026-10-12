#include "QuickAction.h"

#include <algorithm>

namespace QuickActions {
namespace {

std::wstring Clean(std::wstring text, size_t maxLength) {
    // The file holds one value per line, so a line break inside a value would end it early and
    // turn the rest into something else.
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](wchar_t c) { return c == L'\r' || c == L'\n'; }),
               text.end());
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t");
    text = text.substr(first, last - first + 1);
    if (text.size() > maxLength) text.resize(maxLength);
    return text;
}

} // namespace

const std::vector<QuickAction>& Defaults() {
    static const std::vector<QuickAction> defaults = {
        {L"Open Notepad", L"notepad.exe", L""},
        {L"Open Calculator", L"calc.exe", L""},
        {L"Show Desktop", kShowDesktop, L""},
    };
    return defaults;
}

std::vector<QuickAction> Sanitize(std::vector<QuickAction> list) {
    std::vector<QuickAction> out;
    for (QuickAction& action : list) {
        QuickAction cleaned;
        cleaned.name = Clean(std::move(action.name), kMaxNameLength);
        cleaned.target = Clean(std::move(action.target), kMaxTargetLength);
        cleaned.arguments = Clean(std::move(action.arguments), kMaxArgumentsLength);
        if (cleaned.name.empty() || cleaned.target.empty()) continue;
        out.push_back(std::move(cleaned));
        if (out.size() == kMaxActions) break;
    }
    return out;
}

std::wstring NameFromPath(const std::wstring& path, bool isFolder) {
    std::wstring name = path;
    // "C:\Games\" and "C:\Games" are the same folder; a drive's own trailing slash goes too, which
    // leaves "D:" - the name Explorer's address bar would show for it.
    while (!name.empty() && (name.back() == L'\\' || name.back() == L'/')) name.pop_back();
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name.erase(0, slash + 1);
    if (!isFolder) {
        const size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    }
    return Clean(std::move(name), kMaxNameLength);
}

} // namespace QuickActions
