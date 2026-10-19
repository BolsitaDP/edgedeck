#include "RecentFilesWidget.h"

#include "Actions.h"
#include "BadgeColor.h"
#include "Config.h"
#include "Renderer.h"
#include "Strings.h"

#include <cwctype>

namespace {
constexpr float kPaddingX = 18.0f;
constexpr float kRowHeight = 48.0f;
constexpr float kFolderRowHeight = 40.0f;
constexpr float kMessageHeight = 44.0f;
constexpr float kBadgeSize = 28.0f;
constexpr float kFolderIconSize = 24.0f;
constexpr float kGap = 12.0f;
constexpr float kNameHeight = 34.0f;
// A line of the panel's item text is about this tall; the label of the "open folder" row is
// started this far below its top to sit level with its icon, as in the Quick Actions rows.
constexpr float kLabelHeight = 18.0f;
// U+25CB WHITE CIRCLE, inside the WGL4 set Segoe UI carries: the fallback when the icon font is
// not there. Numeric so the source stays ASCII.
constexpr wchar_t kTabGlyph[] = {0x25CB, 0};

// The first letter of a name, upper-cased: the badge of a file that has no extension.
std::wstring FirstLetter(const std::wstring& name) {
    for (wchar_t c : name) {
        if (std::iswalnum(c)) return std::wstring(1, static_cast<wchar_t>(std::towupper(c)));
    }
    return L"?";
}

// "just now", "3 h ago", and for a file a week old or more, its date in the user's own format.
std::wstring AgeText(const FILETIME& written) {
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    const auto ticks = [](const FILETIME& t) {
        return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    const unsigned long long now = ticks(nowFileTime);
    const unsigned long long then = ticks(written);
    const long long seconds = now >= then ? static_cast<long long>((now - then) / 10000000ULL) : 0;

    const std::wstring relative = RecentFiles::DescribeAge(seconds);
    if (!relative.empty()) return relative;

    FILETIME local{};
    SYSTEMTIME st{};
    wchar_t date[48] = {};
    if (FileTimeToLocalFileTime(&written, &local) && FileTimeToSystemTime(&local, &st) &&
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 48, nullptr) > 0) {
        return date;
    }
    return TR(RecentAWhileAgo);
}
} // namespace

RecentFilesWidget::RecentFilesWidget() = default;

const wchar_t* RecentFilesWidget::TabGlyph() const { return kTabGlyph; }

// U+E81C, a clock with a returning arrow ("History"), present in both icon fonts.
const wchar_t* RecentFilesWidget::TabIcon() const {
    static constexpr wchar_t icon[] = {0xE81C, 0};
    return icon;
}

const wchar_t* RecentFilesWidget::PanelTitle() const { return TR(RecentTitle); }

std::wstring RecentFilesWidget::Folder() const {
    std::wstring folder = RecentFiles::SanitizeFolder(Config::RecentFolder());
    if (folder.empty()) folder = RecentFiles::DefaultFolder();
    return Actions::ExpandVariables(folder);
}

int RecentFilesWidget::ControlCount() const {
    return static_cast<int>(m_rows.size()) + ((m_loaded && m_folderFound) ? 1 : 0);
}

float RecentFilesWidget::BlockHeight() const {
    return m_rows.empty() ? kMessageHeight : static_cast<float>(m_rows.size()) * kRowHeight;
}

float RecentFilesWidget::PreferredContentHeight(float /*logicalWidth*/) const {
    return BlockHeight() + ((m_loaded && m_folderFound) ? kFolderRowHeight : 0.0f);
}

void RecentFilesWidget::OnPanelOpening(HWND ownerHwnd) {
    m_owner = ownerHwnd;
    Request();
}

void RecentFilesWidget::OnSettingsChanged() {
    // The folder may be another one now: what is held is of the old one.
    m_rows.clear();
    m_loaded = false;
    m_folderFound = false;
    m_folderImage.reset();
    m_hovered = -1;
    m_focused = -1;
    m_requests.Clear();
    if (m_owner) Request(); // a pinned panel that is open now
}

void RecentFilesWidget::Request() {
    if (!m_owner) return;
    m_requests.Clear(); // an older listing, if it is still on its way, is no longer wanted
    RecentFiles::LoadAsync(Folder(), RecentFiles::kRows, m_owner, kListingMessage, m_requests.Begin());
}

void RecentFilesWidget::OnAsyncResult(UINT message, WPARAM wParam) {
    auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
    if (!envelope) return;

    // A reply for an older open, or for a widget Settings replaced, is dropped - and always freed.
    if (message != kListingMessage || envelope->Kind() != AsyncKind::RecentFiles) {
        delete envelope;
        return;
    }
    auto* batch = static_cast<RecentFiles::RecentBatch*>(envelope);
    if (m_requests.Accept(batch->requestId)) {
        m_folder = std::move(batch->folder);
        m_folderFound = batch->listing.folderFound;
        m_folderImage = std::move(batch->folderImage);
        m_rows.clear();
        for (size_t i = 0; i < batch->listing.newest.size(); ++i) {
            Row row;
            row.entry = std::move(batch->listing.newest[i]);
            row.age = AgeText(row.entry.written);
            if (i < batch->images.size()) row.image = std::move(batch->images[i]);
            m_rows.push_back(std::move(row));
        }
        m_loaded = true;
        // Whatever was under the pointer may be a different row now.
        if (m_hovered >= ControlCount()) m_hovered = -1;
        if (m_focused >= ControlCount()) m_focused = -1;
    }
    delete batch;
}

void RecentFilesWidget::Draw(IPanelPainter& painter, float width, float /*contentHeight*/) {
    const PanelTheme& theme = PanelTheme::Current();

    if (!m_loaded) {
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kMessageHeight), TR(Checking),
                          true);
        return;
    }
    if (!m_folderFound) {
        painter.DrawLabelPair(D2D1::RectF(kPaddingX, 4.0f, width - kPaddingX, kMessageHeight),
                              TR(RecentFolderNotFound), m_folder.c_str());
        return;
    }
    if (m_rows.empty()) {
        painter.DrawLabel(D2D1::RectF(kPaddingX, 0.0f, width - kPaddingX, kMessageHeight),
                          TR(RecentNoFiles), true);
    }

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& row = m_rows[static_cast<size_t>(i)];
        const float top = static_cast<float>(i) * kRowHeight;

        painter.DrawRow(D2D1::RectF(kPaddingX, top, width - kPaddingX, top + kRowHeight), L"",
                        i == m_hovered || i == m_focused);

        const float badgeTop = top + (kRowHeight - kBadgeSize) / 2.0f;
        const D2D1_RECT_F badge =
            D2D1::RectF(kPaddingX, badgeTop, kPaddingX + kBadgeSize, badgeTop + kBadgeSize);
        if (row.image) {
            painter.DrawImage(badge, *row.image);
        } else {
            const std::wstring extension = RecentFiles::ExtensionBadge(row.entry.name);
            const std::wstring letters = extension.empty() ? FirstLetter(row.entry.name) : extension;
            painter.DrawBadge(badge, letters.c_str(), HashedBadgeColor(extension.empty() ? row.entry.name : extension));
        }

        const float nameTop = top + (kRowHeight - kNameHeight) / 2.0f;
        painter.DrawLabelPair(
            D2D1::RectF(kPaddingX + kBadgeSize + kGap, nameTop, width - kPaddingX, nameTop + kNameHeight),
            row.entry.name.c_str(), row.age.c_str());
    }

    // The last row: the folder itself.
    const int folderControl = static_cast<int>(m_rows.size());
    const float top = BlockHeight();
    painter.DrawRow(D2D1::RectF(kPaddingX, top, width - kPaddingX, top + kFolderRowHeight), L"",
                    folderControl == m_hovered || folderControl == m_focused);
    const float iconTop = top + (kFolderRowHeight - kFolderIconSize) / 2.0f;
    const D2D1_RECT_F icon =
        D2D1::RectF(kPaddingX, iconTop, kPaddingX + kFolderIconSize, iconTop + kFolderIconSize);
    if (m_folderImage) {
        painter.DrawImage(icon, *m_folderImage);
    } else {
        painter.DrawBadge(icon, L"F", theme.controlBg);
    }
    const float labelTop = top + (kFolderRowHeight - kLabelHeight) / 2.0f;
    painter.DrawLabel(D2D1::RectF(icon.right + kGap, labelTop, width - kPaddingX, labelTop + kLabelHeight),
                      TR(RecentOpenFolder), false);
}

int RecentFilesWidget::HitTest(float x, float y, float width, float /*contentHeight*/) const {
    if (!m_loaded || !m_folderFound) return -1;
    if (x < 0.0f || x > width || y < 0.0f) return -1;

    const float rowsEnd = static_cast<float>(m_rows.size()) * kRowHeight;
    if (y < rowsEnd) return static_cast<int>(y / kRowHeight);

    const float folderTop = BlockHeight();
    if (y >= folderTop && y < folderTop + kFolderRowHeight) return static_cast<int>(m_rows.size());
    return -1; // the "no files" message, or past the end
}

void RecentFilesWidget::Activate(int controlId, HWND ownerHwnd) {
    if (controlId < 0 || controlId >= ControlCount()) return;

    std::wstring problem;
    bool ok = true;
    if (controlId < static_cast<int>(m_rows.size())) {
        const RecentFiles::Entry& entry = m_rows[static_cast<size_t>(controlId)].entry;
        ok = Actions::Open(entry.path, entry.name, problem);
    } else {
        ok = Actions::Open(m_folder, TR(RecentTheFolder), problem);
    }
    if (!ok) ReportProblem(ownerHwnd ? ownerHwnd : m_owner, problem);
}

std::wstring RecentFilesWidget::AccessibleSummary() const {
    if (!m_loaded) return TR(RecentSummaryLoading);
    if (!m_folderFound) return TR(RecentSummaryNotFound);
    return Strings::Format(Strings::Id::RecentSummary, m_rows.size());
}

std::wstring RecentFilesWidget::AccessibleControlText(int controlId) const {
    if (controlId < 0 || controlId >= ControlCount()) return {};
    if (controlId == static_cast<int>(m_rows.size())) return TR(RecentOpenFolder);
    const Row& row = m_rows[static_cast<size_t>(controlId)];
    return Strings::Format(Strings::Id::RecentItem, row.entry.name.c_str(), row.age.c_str());
}

bool RecentFilesWidget::OnKeyDown(UINT key, int focusedControl) {
    const int count = ControlCount();
    if (count == 0) return false;
    switch (key) {
        case VK_UP:
            m_focused = (focusedControl <= 0) ? count - 1 : focusedControl - 1;
            break;
        case VK_DOWN:
            m_focused = (focusedControl < 0 || focusedControl >= count - 1) ? 0 : focusedControl + 1;
            break;
        case VK_HOME:
            m_focused = 0;
            break;
        case VK_END:
            m_focused = count - 1;
            break;
        case VK_SPACE:
        case VK_RETURN:
            if (focusedControl >= 0) Activate(focusedControl, nullptr);
            return true;
        default:
            return false;
    }
    m_hovered = m_focused;
    return true;
}
