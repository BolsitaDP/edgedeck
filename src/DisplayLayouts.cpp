#include "DisplayLayouts.h"

#include "Strings.h"

#include <algorithm>
#include <cwctype>

namespace DisplayLayouts {
namespace {

std::wstring Trimmed(std::wstring text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](wchar_t c) { return c == L'\r' || c == L'\n'; }),
               text.end());
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const size_t last = text.find_last_not_of(L" \t");
    return text.substr(first, last - first + 1);
}

std::wstring Upper(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(towupper(c));
    return text;
}

std::vector<std::wstring> Sorted(std::vector<std::wstring> ids) {
    std::sort(ids.begin(), ids.end());
    return ids;
}

const DisplayTopology::Monitor* Find(const std::vector<DisplayTopology::Monitor>& monitors, const std::wstring& id) {
    for (const auto& m : monitors) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

std::wstring Cut(std::wstring name) {
    if (name.size() > kMaxNameLength) name.resize(kMaxNameLength);
    return name;
}

} // namespace

std::vector<Layout> Automatic(const std::vector<DisplayTopology::Monitor>& monitors) {
    std::vector<Layout> out;
    if (monitors.size() < 2) return out;
    Layout all{TR(ProfileAll), {}};
    for (const auto& m : monitors) all.ids.push_back(m.id);
    out.push_back(std::move(all));
    for (const auto& m : monitors) {
        if (out.size() >= kMaxLayouts) break;
        out.push_back({Cut(Strings::Format(Strings::Id::LayoutOnly, m.name.c_str())), {m.id}});
    }
    return out;
}

std::vector<Layout> FromProfiles(const std::vector<DisplayTopology::Monitor>& monitors, const std::wstring& tvId,
                                 const std::wstring& mainId) {
    using DisplayTopology::Profile;
    struct Old {
        Profile profile;
        Strings::Id name;
    };
    // The order of the old buttons, which is the order of their shortcuts.
    const Old olds[] = {
        {Profile::MainMonitors, Strings::Id::ProfileMainMonitors},
        {Profile::TvOnly, Strings::Id::ProfileTvOnly},
        {Profile::AllMonitors, Strings::Id::ProfileAll},
        {Profile::MainOnly, Strings::Id::ProfileMainOnly},
    };
    std::vector<Layout> out;
    for (const Old& old : olds) {
        const DisplayTopology::ProfileState state = DisplayTopology::Evaluate(old.profile, monitors, tvId, mainId);
        if (state.ids.empty()) continue;
        const auto same = std::find_if(out.begin(), out.end(),
                                       [&](const Layout& l) { return Sorted(l.ids) == Sorted(state.ids); });
        if (same != out.end()) continue;
        out.push_back({Strings::Get(old.name), state.ids});
    }
    return out;
}

State Evaluate(const Layout& layout, const std::vector<DisplayTopology::Monitor>& monitors) {
    State state;
    for (const std::wstring& id : layout.ids) {
        if (!Find(monitors, id)) state.missing.push_back(id);
    }
    state.available = !layout.ids.empty() && state.missing.empty();
    if (!state.available) return state;

    std::vector<std::wstring> on;
    for (const auto& m : monitors) {
        if (m.active) on.push_back(m.id);
    }
    state.active = Sorted(on) == Sorted(layout.ids);
    return state;
}

std::vector<Layout> Sanitize(std::vector<Layout> layouts) {
    std::vector<Layout> out;
    for (Layout& layout : layouts) {
        Layout clean;
        clean.name = Cut(Trimmed(std::move(layout.name)));
        for (std::wstring& id : layout.ids) {
            std::wstring upper = Upper(Trimmed(std::move(id)));
            if (upper.empty() || std::find(clean.ids.begin(), clean.ids.end(), upper) != clean.ids.end()) continue;
            clean.ids.push_back(std::move(upper));
        }
        if (clean.name.empty() || clean.ids.empty()) continue;
        out.push_back(std::move(clean));
        if (out.size() == kMaxLayouts) break;
    }
    return out;
}

std::wstring JoinIds(const std::vector<std::wstring>& ids) {
    std::wstring out;
    for (const std::wstring& id : ids) {
        if (!out.empty()) out += L",";
        out += id;
    }
    return out;
}

std::vector<std::wstring> SplitIds(const std::wstring& text) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(L',', start);
        std::wstring id = Trimmed(text.substr(start, comma == std::wstring::npos ? comma : comma - start));
        if (!id.empty()) out.push_back(Upper(std::move(id)));
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return out;
}

std::wstring NameFor(const std::vector<std::wstring>& ids, const std::vector<DisplayTopology::Monitor>& monitors) {
    std::wstring name;
    for (const std::wstring& id : ids) {
        const DisplayTopology::Monitor* m = Find(monitors, id);
        if (!name.empty()) name += L" + ";
        name += (m && !m->name.empty()) ? m->name : id;
    }
    return Cut(name);
}

} // namespace DisplayLayouts
