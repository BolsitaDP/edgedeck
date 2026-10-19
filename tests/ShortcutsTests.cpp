// The configurable shortcuts: how they are written in config.txt and read back, what is refused,
// and which sets cannot work because two shortcuts would be the same keys. Registering them with
// Windows needs a window and is left to the app; everything that decides what to register is here.
#include "TestHarness.h"

#include "Config.h"
#include "Shortcuts.h"
#include "Strings.h"

#include <string>
#include <vector>

namespace {

void TestTextRoundTrip() {
    TEST("Shortcuts: a chord and a family are written and read back the same");
    Shortcut s;
    CHECK(Shortcuts::Parse(L"Ctrl+Alt+Q", s));
    CHECK(s.modifiers == (MOD_CONTROL | MOD_ALT) && s.key == 'Q');
    CHECK(Shortcuts::Format(s) == L"Ctrl+Alt+Q");

    // Any order, any case, spaces, and function keys.
    CHECK(Shortcuts::Parse(L" shift + win + f12 ", s));
    CHECK(s.modifiers == (MOD_SHIFT | MOD_WIN) && s.key == VK_F12);
    CHECK(Shortcuts::Format(s) == L"Win+Shift+F12");
    CHECK(Shortcuts::Parse(L"ctrl+alt+7", s) && s.key == '7');

    UINT m = 0;
    CHECK(Shortcuts::ParseModifiers(L"Ctrl+Alt+Shift", m));
    CHECK(m == (MOD_CONTROL | MOD_ALT | MOD_SHIFT));
    CHECK(Shortcuts::FormatModifiers(m) == L"Ctrl+Alt+Shift");

    // Off, both ways.
    CHECK(Shortcuts::Parse(L"none", s) && s.Off());
    CHECK(Shortcuts::Format(Shortcut{}) == L"none");
    CHECK(Shortcuts::ParseModifiers(L"none", m) && m == 0);
    CHECK(Shortcuts::FormatModifiers(0) == L"none");
}

void TestWhatIsRefused() {
    TEST("Shortcuts: a value that does not parse, or has no Ctrl/Alt/Win, is refused and changes nothing");
    Shortcut s{MOD_CONTROL | MOD_ALT, 'Q'};
    const Shortcut before = s;
    for (const wchar_t* bad : {L"", L"Q", L"Shift+Q", L"Ctrl+Alt", L"Ctrl+Alt+Esc", L"Ctrl++Q", L"Ctrl+Alt+F25",
                               L"Ctrl+Alt+QQ", L"Hyper+Q"}) {
        CHECK(!Shortcuts::Parse(bad, s));
    }
    CHECK(s == before); // a typo in config.txt keeps the default

    UINT m = MOD_CONTROL | MOD_ALT;
    CHECK(!Shortcuts::ParseModifiers(L"Shift", m));
    CHECK(!Shortcuts::ParseModifiers(L"Ctrl+Q", m));
    CHECK(m == (MOD_CONTROL | MOD_ALT));

    CHECK(Shortcuts::HasRequiredModifier(MOD_WIN));
    CHECK(!Shortcuts::HasRequiredModifier(MOD_SHIFT));
    CHECK(!Shortcuts::HasRequiredModifier(0));
}

void TestClashes() {
    TEST("Shortcuts: two shortcuts that would be the same keys are named, and the defaults have none");
    Strings::SetCurrent(Language::English);
    Shortcuts::Set set;
    CHECK(Shortcuts::Problem(set).empty());

    Shortcuts::Set same = set;
    same.pause = same.exit;
    CHECK(!Shortcuts::Problem(same).empty());

    Shortcuts::Set families = set;
    families.displayModifiers = families.tabModifiers;
    CHECK(Shortcuts::Problem(families).find(L"Open tab") != std::wstring::npos);

    // Exit on Ctrl+Alt+3 is tab 3; on Ctrl+Alt+Shift+2, display layout 2; Ctrl+Alt+Shift+Q is not
    // one of the layouts' digits, and is fine.
    Shortcuts::Set chord = set;
    chord.exit = Shortcut{MOD_CONTROL | MOD_ALT, '3'};
    CHECK(!Shortcuts::Problem(chord).empty());
    chord.exit = Shortcut{MOD_CONTROL | MOD_ALT | MOD_SHIFT, '2'};
    CHECK(!Shortcuts::Problem(chord).empty());
    chord.exit = Shortcut{MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'Q'};
    CHECK(Shortcuts::Problem(chord).empty());

    // Off is never a clash, even when two are off.
    Shortcuts::Set off;
    off.exit = Shortcut{};
    off.pause = Shortcut{};
    off.tabModifiers = 0;
    off.displayModifiers = 0;
    CHECK(Shortcuts::Problem(off).empty());

    // A chord without Ctrl/Alt/Win (only possible by building one by hand) is refused.
    Shortcuts::Set bare = set;
    bare.pause = Shortcut{MOD_SHIFT, 'P'};
    CHECK(!Shortcuts::Problem(bare).empty());
}

void TestShownInTheUsersLanguage() {
    TEST("Shortcuts: keys are shown in the interface's language, and off says so");
    Strings::SetCurrent(Language::English);
    CHECK(Shortcuts::Display(Shortcut{MOD_CONTROL | MOD_SHIFT, 'K'}) == L"Ctrl+Shift+K");
    CHECK(Shortcuts::DisplayFamily(MOD_CONTROL | MOD_ALT, 9) == L"Ctrl+Alt+1..9");
    CHECK(Shortcuts::Display(Shortcut{}) == L"no shortcut");
    Strings::SetCurrent(Language::Spanish);
    CHECK(Shortcuts::Display(Shortcut{MOD_CONTROL | MOD_SHIFT, 'K'}) == L"Ctrl+Mayús+K");
    // What config.txt holds does not change with the language.
    CHECK(Shortcuts::Format(Shortcut{MOD_CONTROL | MOD_SHIFT, 'K'}) == L"Ctrl+Shift+K");
    Strings::SetCurrent(Language::English);
}

void TestKeptInTheSettings() {
    TEST("Shortcuts: the set is saved with the settings and read back; a file without it has the defaults");
    std::vector<TabSettings> tabs = {{WidgetType::QuickActions, 0.5f, 26.0f, 76.0f, 300.0f}};
    Shortcuts::Set set;
    set.exit = Shortcut{MOD_WIN | MOD_ALT, 'X'};
    set.pause = Shortcut{};
    set.tabModifiers = MOD_CONTROL | MOD_SHIFT;
    set.displayModifiers = 0;
    Config::SetShortcutSet(set);
    CHECK(Config::Save(tabs));
    Config::SetShortcutSet(Shortcuts::Set{});
    Config::LoadOrDefault();
    CHECK(Config::ShortcutSet() == set);

    Config::SetShortcutSet(Shortcuts::Set{});
    CHECK(Config::Save(tabs));
    Config::SetShortcutSet(set);
    Config::LoadOrDefault();
    CHECK(Config::ShortcutSet() == Shortcuts::Set{});
}

} // namespace

void RunShortcutsTests() {
    TestTextRoundTrip();
    TestWhatIsRefused();
    TestClashes();
    TestShownInTheUsersLanguage();
    TestKeptInTheSettings();
}
