// The media panel's app names: what an AUMID says on its own, and which embedded-Chromium app is
// the one behind the id they all share.
#include "TestHarness.h"

#include "AppName.h"

#include <string>
#include <vector>

namespace {

void TestParse() {
    TEST("App name: an AUMID gives the name it holds");
    CHECK_EQ(AppName::ParseAumid(L"C:\\Apps\\vlc.exe").name, std::wstring(L"vlc"));
    CHECK_EQ(AppName::ParseAumid(L"C:\\Apps\\vlc.exe").exePath, std::wstring(L"C:\\Apps\\vlc.exe"));
    CHECK_EQ(AppName::ParseAumid(L"Spotify.exe").name, std::wstring(L"Spotify"));
    CHECK_EQ(AppName::ParseAumid(L"Chrome").name, std::wstring(L"Google Chrome"));
    CHECK_EQ(AppName::ParseAumid(L"Chrome._crx_ghjmbgleacindaeipamojehijb").name, std::wstring(L"Google Chrome"));
    CHECK_EQ(AppName::ParseAumid(L"MSEdge").name, std::wstring(L"Microsoft Edge"));
    CHECK_EQ(AppName::ParseAumid(L"SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify").name, std::wstring(L"Spotify"));
    CHECK_EQ(AppName::ParseAumid(L"Contoso.Player_abc123!App").name, std::wstring(L"Player"));
    CHECK(AppName::ParseAumid(L"Chromium.XLVA6YKCHBAZ7CNRHUXT7XBGDI").chromium);
    CHECK(!AppName::ParseAumid(L"Chrome").chromium);
    CHECK_EQ(AppName::ParseAumid(L"").name, std::wstring());
}

AppName::Host MakeHost(const wchar_t* path, const wchar_t* name, std::vector<std::wstring> titles) {
    AppName::Host host;
    host.exePath = path;
    host.name = name;
    host.windowTitles = std::move(titles);
    return host;
}

void TestPickHost() {
    TEST("App name: the Chromium id is the app showing the track, else the one that is no helper");
    std::vector<AppName::Host> hosts = {
        MakeHost(L"C:\\Steam\\bin\\cef\\steamwebhelper.exe", L"Steam", {L"Steam"}),
        MakeHost(L"C:\\Spotify\\Spotify.exe", L"Spotify", {L"Spotify Premium"}),
    };
    CHECK_EQ(AppName::PickChromiumHost(hosts, L"Son Al Son"), 1);
    hosts[1].windowTitles = {L"Orquesta Aragón - Son Al Son"};
    CHECK_EQ(AppName::PickChromiumHost(hosts, L"Son Al Son"), 1);
    hosts.push_back(MakeHost(L"C:\\Other\\Other.exe", L"Other", {L"Other"}));
    CHECK_EQ(AppName::PickChromiumHost(hosts, L"Son Al Son"), 1);
    CHECK_EQ(AppName::PickChromiumHost(hosts, L"Nothing shows this"), -1);
    CHECK_EQ(AppName::PickChromiumHost({}, L"Son Al Son"), -1);
}

} // namespace

void RunAppNameTests() {
    TestParse();
    TestPickHost();
}
