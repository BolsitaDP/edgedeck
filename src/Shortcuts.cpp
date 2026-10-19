#include "Shortcuts.h"

#include "Strings.h"

#include <cwctype>
#include <vector>

namespace Shortcuts {
namespace {

std::wstring Trimmed(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t");
    return text.substr(first, last - first + 1);
}

std::wstring Lower(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
    return text;
}

std::vector<std::wstring> Parts(const std::wstring& text) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t plus = text.find(L'+', start);
        parts.push_back(Lower(Trimmed(text.substr(start, plus == std::wstring::npos ? plus : plus - start))));
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    return parts;
}

bool ModifierOf(const std::wstring& part, UINT& modifier) {
    if (part == L"ctrl" || part == L"control") modifier = MOD_CONTROL;
    else if (part == L"alt") modifier = MOD_ALT;
    else if (part == L"shift") modifier = MOD_SHIFT;
    else if (part == L"win" || part == L"windows") modifier = MOD_WIN;
    else return false;
    return true;
}

bool KeyOf(const std::wstring& part, UINT& key) {
    if (part.size() == 1 && ((part[0] >= L'a' && part[0] <= L'z') || (part[0] >= L'0' && part[0] <= L'9'))) {
        key = static_cast<UINT>(towupper(part[0]));
        return true;
    }
    if (part.size() >= 2 && part.size() <= 3 && part[0] == L'f') {
        int n = 0;
        for (size_t i = 1; i < part.size(); ++i) {
            if (part[i] < L'0' || part[i] > L'9') return false;
            n = n * 10 + (part[i] - L'0');
        }
        if (n < 1 || n > 24) return false;
        key = static_cast<UINT>(VK_F1 + n - 1);
        return true;
    }
    return false;
}

std::wstring KeyName(UINT key) {
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
    return L"?";
}

// Modifiers in the order Windows itself writes them.
std::wstring ModifierText(UINT modifiers, bool forPeople) {
    std::wstring out;
    auto add = [&](const wchar_t* name) {
        if (!out.empty()) out += L"+";
        out += name;
    };
    if (modifiers & MOD_WIN) add(L"Win");
    if (modifiers & MOD_CONTROL) add(L"Ctrl");
    if (modifiers & MOD_ALT) add(L"Alt");
    if (modifiers & MOD_SHIFT) add(forPeople ? TR(KeyShift) : L"Shift");
    return out;
}

} // namespace

bool Parse(const std::wstring& text, Shortcut& out) {
    const std::wstring all = Lower(Trimmed(text));
    if (all == L"none" || all == L"off") {
        out = Shortcut{};
        return true;
    }
    const std::vector<std::wstring> parts = Parts(text);
    UINT modifiers = 0;
    UINT key = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        UINT modifier = 0;
        if (i + 1 < parts.size()) {
            if (!ModifierOf(parts[i], modifier)) return false;
            modifiers |= modifier;
        } else if (!KeyOf(parts[i], key)) {
            return false;
        }
    }
    if (key == 0 || !HasRequiredModifier(modifiers)) return false;
    out = Shortcut{modifiers, key};
    return true;
}

bool ParseModifiers(const std::wstring& text, UINT& out) {
    const std::wstring all = Lower(Trimmed(text));
    if (all == L"none" || all == L"off") {
        out = 0;
        return true;
    }
    UINT modifiers = 0;
    for (const std::wstring& part : Parts(text)) {
        UINT modifier = 0;
        if (!ModifierOf(part, modifier)) return false;
        modifiers |= modifier;
    }
    if (!HasRequiredModifier(modifiers)) return false;
    out = modifiers;
    return true;
}

std::wstring Format(const Shortcut& shortcut) {
    if (shortcut.Off()) return L"none";
    return ModifierText(shortcut.modifiers, false) + L"+" + KeyName(shortcut.key);
}

std::wstring FormatModifiers(UINT modifiers) {
    if ((modifiers & kModifierMask) == 0) return L"none";
    return ModifierText(modifiers, false);
}

std::wstring Display(const Shortcut& shortcut) {
    if (shortcut.Off()) return TR(ShortcutNone);
    return ModifierText(shortcut.modifiers, true) + L"+" + KeyName(shortcut.key);
}

std::wstring DisplayFamily(UINT modifiers, int count) {
    if ((modifiers & kModifierMask) == 0) return TR(ShortcutNone);
    return ModifierText(modifiers, true) + L"+1.." + std::to_wstring(count);
}

bool HasRequiredModifier(UINT modifiers) {
    return (modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)) != 0;
}

std::wstring Problem(const Set& set) {
    using Strings::Id;
    if (!set.exit.Off() && !HasRequiredModifier(set.exit.modifiers)) {
        return Strings::Format(Id::ShortcutNeedsModifier, TR(ShortcutNameExit));
    }
    if (!set.pause.Off() && !HasRequiredModifier(set.pause.modifiers)) {
        return Strings::Format(Id::ShortcutNeedsModifier, TR(ShortcutNamePause));
    }
    if (set.tabModifiers != 0 && !HasRequiredModifier(set.tabModifiers)) {
        return Strings::Format(Id::ShortcutNeedsModifier, TR(ShortcutNameTabs));
    }
    if (set.displayModifiers != 0 && !HasRequiredModifier(set.displayModifiers)) {
        return Strings::Format(Id::ShortcutNeedsModifier, TR(ShortcutNameDisplays));
    }

    if (!set.exit.Off() && set.exit == set.pause) {
        return Strings::Format(Id::ShortcutClash, TR(ShortcutNameExit), TR(ShortcutNamePause));
    }
    if (set.tabModifiers != 0 && set.tabModifiers == set.displayModifiers) {
        return Strings::Format(Id::ShortcutClash, TR(ShortcutNameTabs), TR(ShortcutNameDisplays));
    }
    // A chord that is one of a family's own: same modifiers and one of its digits.
    auto inFamily = [](const Shortcut& s, UINT modifiers, int count) {
        return !s.Off() && modifiers != 0 && s.modifiers == modifiers && s.key >= '1' &&
               s.key < static_cast<UINT>('1' + count);
    };
    struct Single {
        const Shortcut* shortcut;
        Strings::Id name;
    };
    for (const Single single : {Single{&set.exit, Id::ShortcutNameExit}, Single{&set.pause, Id::ShortcutNamePause}}) {
        if (inFamily(*single.shortcut, set.tabModifiers, kTabCount)) {
            return Strings::Format(Id::ShortcutClash, Strings::Get(single.name), TR(ShortcutNameTabs));
        }
        if (inFamily(*single.shortcut, set.displayModifiers, kDisplayCount)) {
            return Strings::Format(Id::ShortcutClash, Strings::Get(single.name), TR(ShortcutNameDisplays));
        }
    }
    return {};
}

} // namespace Shortcuts
