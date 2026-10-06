// Covers how a widget's problem report travels to its tab: PanelWidget::ReportProblem
// posts an envelope to a window. The interesting failures are all about ownership -
// a report that reaches nobody must not leak, and one aimed at no window at all must
// not be queued as a thread message that nothing will ever dispatch - so those are
// what is checked, using a message-only window as the stand-in for a tab.
#include "TestHarness.h"

#include "PanelWidget.h"

#include <string>

namespace {

std::wstring g_received;
int g_delivered = 0;

LRESULT CALLBACK NoticeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == PanelWidget::kNoticeMessage) {
        auto* envelope = reinterpret_cast<AsyncEnvelope*>(wParam);
        if (envelope && envelope->Kind() == AsyncKind::Notice) {
            g_received = static_cast<NoticeEnvelope*>(envelope)->text;
        }
        ++g_delivered;
        delete envelope; // the receiver always frees it, as Tab does
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND MakeReceiver() {
    static const wchar_t kClass[] = L"EdgeDeckNoticeTestWindow";
    static bool registered = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = NoticeProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        return RegisterClassW(&wc) != 0;
    }();
    if (!registered) return nullptr;
    return CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

// Dispatches everything queued, and counts any message that was posted with no
// window - the symptom of reporting to a null owner.
int PumpAndCountOrphans() {
    int orphans = 0;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (!msg.hwnd && msg.message == PanelWidget::kNoticeMessage) {
            ++orphans;
            delete reinterpret_cast<AsyncEnvelope*>(msg.wParam); // keep the test itself tidy
            continue;
        }
        DispatchMessageW(&msg);
    }
    return orphans;
}

void TestDeliversTheText() {
    TEST("Notice: a reported problem reaches the window with its text intact");
    HWND window = MakeReceiver();
    CHECK(window != nullptr);
    if (!window) return;

    g_received.clear();
    g_delivered = 0;
    PanelWidget::ReportProblem(window, L"Windows could not change the display layout (error 87).");
    CHECK_EQ(PumpAndCountOrphans(), 0);
    CHECK_EQ(g_delivered, 1);
    CHECK(g_received == L"Windows could not change the display layout (error 87).");

    // Non-ASCII survives the trip: it is a std::wstring in an envelope, not a copy
    // into a fixed buffer.
    g_received.clear();
    PanelWidget::ReportProblem(window, L"café ✓");
    PumpAndCountOrphans();
    CHECK(g_received == L"café ✓");

    DestroyWindow(window);
}

void TestEmptyReportIsIgnored() {
    TEST("Notice: an empty report posts nothing");
    HWND window = MakeReceiver();
    CHECK(window != nullptr);
    if (!window) return;

    g_delivered = 0;
    PanelWidget::ReportProblem(window, L"");
    CHECK_EQ(PumpAndCountOrphans(), 0);
    CHECK_EQ(g_delivered, 0);
    DestroyWindow(window);
}

void TestNullOwnerIsNotQueued() {
    TEST("Notice: a report with no owner window is dropped, not queued as a thread message");
    // PostMessage to a null window succeeds and queues a message nothing
    // dispatches, so the envelope would be lost. The old keyboard path for Quick
    // Actions passed exactly that.
    PanelWidget::ReportProblem(nullptr, L"nobody to tell");
    CHECK_EQ(PumpAndCountOrphans(), 0);
}

void TestDestroyedOwnerDoesNotCrash() {
    TEST("Notice: a report to a window that is already gone is freed, not delivered");
    HWND window = MakeReceiver();
    CHECK(window != nullptr);
    if (!window) return;
    DestroyWindow(window);

    g_delivered = 0;
    PanelWidget::ReportProblem(window, L"too late"); // PostMessage fails; PostOrDelete frees it
    PumpAndCountOrphans();
    CHECK_EQ(g_delivered, 0);
}

} // namespace

void RunNoticeTests() {
    TestDeliversTheText();
    TestEmptyReportIsIgnored();
    TestNullOwnerIsNotQueued();
    TestDestroyedOwnerDoesNotCrash();
}
