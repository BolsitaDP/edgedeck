// The interface's texts: every one exists in every language, a translated format takes the same
// arguments as the English one (or swprintf reads the wrong thing off the stack), and the language
// is a setting that survives a restart.
#include "TestHarness.h"

#include "Config.h"
#include "QuickAction.h"
#include "RecentFiles.h"
#include "Strings.h"

#include <string>
#include <vector>

namespace {

// The conversion specifiers of a printf format, in order: "%ls", "%zu", "%lld"... A "%%" is a
// literal percent and not a specifier, and a percent sign not followed by a conversion is text.
std::vector<std::wstring> Specifiers(const wchar_t* format) {
    std::vector<std::wstring> out;
    for (const wchar_t* p = format; *p; ++p) {
        if (*p != L'%') continue;
        if (p[1] == L'%') {
            ++p;
            continue;
        }
        std::wstring spec = L"%";
        const wchar_t* q = p + 1;
        while (*q && (*q == L'l' || *q == L'z' || *q == L'h' || *q == L'-' || (*q >= L'0' && *q <= L'9'))) {
            spec.push_back(*q++);
        }
        // Only a real conversion counts: "%ProgramFiles%" in a help text is text, never formatted.
        if (*q && wcschr(L"diouxXscpfeEgGaA", *q)) {
            spec.push_back(*q);
            out.push_back(spec);
        }
        p = *q ? q : q - 1;
    }
    return out;
}

void TestEveryTextExistsInEveryLanguage() {
    TEST("Strings: every text has every language, and no translation is left blank");
    int missing = 0;
    for (int i = 0; i < static_cast<int>(Strings::Id::Count); ++i) {
        for (int lang = 0; lang < Strings::kLanguageCount; ++lang) {
            const wchar_t* text = Strings::Get(static_cast<Strings::Id>(i), static_cast<Language>(lang));
            if (!text || !*text) ++missing;
        }
    }
    CHECK_EQ(missing, 0);
}

void TestTranslationsTakeTheSameArguments() {
    TEST("Strings: a translated format has the same specifiers, in the same order, as the English one");
    int mismatched = 0;
    for (int i = 0; i < static_cast<int>(Strings::Id::Count); ++i) {
        const auto id = static_cast<Strings::Id>(i);
        const auto english = Specifiers(Strings::Get(id, Language::English));
        for (int lang = 1; lang < Strings::kLanguageCount; ++lang) {
            if (Specifiers(Strings::Get(id, static_cast<Language>(lang))) != english) {
                ++mismatched;
                std::printf("    mismatch in string #%d: %ls\n", i, Strings::Get(id, static_cast<Language>(lang)));
            }
        }
    }
    CHECK_EQ(mismatched, 0);

    // The checker itself: it does tell formats apart.
    CHECK(Specifiers(L"%ls and %zu") != Specifiers(L"%zu and %ls"));
    CHECK(Specifiers(L"100%% sure: %d") == Specifiers(L"%d"));
}

void TestTheLanguageIsUsedAndKept() {
    TEST("Strings: the language changes what is said, and is saved and read back with the settings");
    Strings::SetCurrent(Language::English);
    CHECK(std::wstring(TR(MenuExit)) == L"Exit");
    CHECK(RecentFiles::DescribeAge(5 * 60) == L"5 min ago");

    Strings::SetCurrent(Language::Spanish);
    CHECK(std::wstring(TR(MenuExit)) == L"Salir");
    CHECK(RecentFiles::DescribeAge(5 * 60) == L"hace 5 min");
    CHECK(Strings::Format(Strings::Id::CouldNotOpen, L"x", L"y") == L"No se pudo abrir \"x\": y.");
    // The default actions are not saved, so they are named in the language of the moment.
    CHECK(QuickActions::Defaults().front().name == L"Abrir Bloc de notas");

    std::vector<TabSettings> tabs = {{WidgetType::QuickActions, 0.5f, 26.0f, 76.0f, 300.0f}};
    CHECK(Config::Save(tabs));
    Strings::SetCurrent(Language::English);
    Config::LoadOrDefault();
    CHECK(Strings::Current() == Language::Spanish);

    // A file without the key - every file written before there was a choice - is English.
    Strings::SetCurrent(Language::English);
    CHECK(Config::Save(tabs)); // writes language=en
    Strings::SetCurrent(Language::Spanish);
    Config::LoadOrDefault();
    CHECK(Strings::Current() == Language::English);

    CHECK(Strings::FromCode(L"es") == Language::Spanish);
    CHECK(Strings::FromCode(L"fr") == Language::English); // unknown: the default
    CHECK(std::wstring(Strings::Code(Language::Spanish)) == L"es");
    CHECK(std::wstring(Strings::NativeName(Language::Spanish)) == L"Español");
    Strings::SetCurrent(Language::English);
}

void TestAFirstStartSpeaksWindowsLanguage() {
    TEST("Strings: a first start is in Windows' language when EdgeDeck speaks it, English otherwise");
    CHECK(Strings::ForWindowsLanguage(0x0C0A) == Language::Spanish); // es-ES
    CHECK(Strings::ForWindowsLanguage(0x080A) == Language::Spanish); // es-MX
    CHECK(Strings::ForWindowsLanguage(0x240A) == Language::Spanish); // es-CO
    CHECK(Strings::ForWindowsLanguage(0x0409) == Language::English); // en-US
    CHECK(Strings::ForWindowsLanguage(0x040C) == Language::English); // fr-FR: not spoken, the default
    CHECK(Strings::ForWindowsLanguage(0) == Language::English);

    // No config.txt at all: Windows' language. Whatever this machine's is, that is what is used.
    wchar_t dir[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    const std::wstring config = std::wstring(dir) + L"\\EdgeDeck\\config.txt";
    DeleteFileW(config.c_str());
    for (const Language before : {Language::English, Language::Spanish}) {
        Strings::SetCurrent(before);
        Config::LoadOrDefault();
        CHECK(Strings::Current() == Strings::FromWindows());
    }
    Strings::SetCurrent(Language::English);
}

void TestNotificationsFitTheirBox() {
    TEST("Strings: what a notification says on its own fits the 255 characters Windows shows");
    for (int lang = 0; lang < Strings::kLanguageCount; ++lang) {
        for (Strings::Id id : {Strings::Id::Welcome, Strings::Id::CrashRestarted, Strings::Id::AutostartMissing,
                               Strings::Id::AutostartBroken, Strings::Id::SaveFailedSettings}) {
            CHECK(wcslen(Strings::Get(id, static_cast<Language>(lang))) <= 255);
        }
        // A settings problem is said with the copy's name, and after a crash, in the same balloon.
        const auto language = static_cast<Language>(lang);
        for (Strings::Id id : {Strings::Id::ConfigUnreadable, Strings::Id::ConfigNewer}) {
            const size_t length = wcslen(Strings::Get(Strings::Id::CrashRestarted, language)) + 1 +
                                  wcslen(Strings::Get(id, language)) + 1 +
                                  wcslen(Strings::Get(Strings::Id::ConfigBackupKept, language)) + 32;
            CHECK(length <= 255);
        }
    }
}

} // namespace

void RunStringsTests() {
    TestNotificationsFitTheirBox();
    TestEveryTextExistsInEveryLanguage();
    TestTranslationsTakeTheSameArguments();
    TestTheLanguageIsUsedAndKept();
    TestAFirstStartSpeaksWindowsLanguage();
}
