#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// One envelope type for every asynchronous job, so the ownership rule lives in
// exactly one place.
//
// Background jobs (WinRT media sessions, DDC/CI writes, HTTPS to LRCLIB) hand
// their result to the UI thread as the WPARAM of a WM_APP-range message
// addressed to the tab window that requested it. Three failure modes used to
// exist here, and all three are now structurally impossible:
//
//  1. LEAK - the receiving window is gone (Settings -> Save rebuilds every
//     tab, or the app is exiting), so PostMessage fails. Callers must check
//     the return value and delete the envelope themselves; see PostOrDelete.
//
//  2. LEAK - the message *is* delivered but to a recycled HWND belonging to a
//     freshly built Tab, whose widget never asked for this job. The envelope
//     carries a requestId the receiver doesn't recognise, so it drops the
//     payload - but it still deletes the envelope.
//
//  3. STALE DATA - a refresh is superseded by a newer one, or a widget instance
//     is destroyed and replaced. The receiver keeps the set of requestIds it
//     currently has in flight; anything else is stale and ignored.
//
// Every widget therefore keeps a small in-flight set and answers "is this mine
// and is it still current?" before touching any state.
// ---------------------------------------------------------------------------

struct AsyncEnvelope {
    // Monotonic, process-wide, never reused. Assigned by NextRequestId() at
    // the moment the job is launched and echoed back untouched.
    std::uint64_t requestId = 0;

    // Widget-specific outcome, interpreted by the widget that asked for the
    // job: MediaControls::Result, LrcLyrics::Status, a bool for a DDC/CI
    // write, etc. Generic here so the envelope needs no knowledge of them.
    int code = 0;

    virtual ~AsyncEnvelope() = default;
};

inline std::uint64_t NextRequestId() {
    static std::uint64_t counter = 0;
    return ++counter;
}

// Posts `envelope` to `target` and takes ownership unconditionally: the
// receiver deletes it, or we delete it here if the post failed. Without this,
// a tab that is torn down mid-job leaks whatever the job had allocated.
inline void PostOrDelete(HWND target, UINT message, AsyncEnvelope* envelope) {
    if (!PostMessageW(target, message, reinterpret_cast<WPARAM>(envelope), 0)) {
        delete envelope;
    }
}

// The set of jobs a widget currently cares about. Tiny on purpose: a widget
// has at most a handful of refreshes or slider writes outstanding, so a sorted
// vector beats any map and never allocates after the first few pushes.
class RequestTracker {
public:
    // Registers a newly launched job and returns the id to stamp on it.
    std::uint64_t Begin() {
        std::uint64_t id = NextRequestId();
        m_ids.push_back(id);
        return id;
    }

    // True if `id` is a job this widget is still waiting for. Also retires it,
    // because a job that has replied is no longer in flight. A widget that
    // wants to keep accepting late replies for the same logical operation
    // (e.g. every slider write) uses Retire() instead.
    bool Accept(std::uint64_t id) {
        for (size_t i = 0; i < m_ids.size(); ++i) {
            if (m_ids[i] == id) {
                m_ids.erase(m_ids.begin() + static_cast<ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    // Retires `id` without answering the question, for flows where a late reply
    // is still meaningful as long as it belongs to this widget instance.
    void Retire(std::uint64_t id) {
        for (size_t i = 0; i < m_ids.size(); ++i) {
            if (m_ids[i] == id) {
                m_ids.erase(m_ids.begin() + static_cast<ptrdiff_t>(i));
                return;
            }
        }
    }

    void Clear() { m_ids.clear(); }

    bool Empty() const { return m_ids.empty(); }

private:
    std::vector<std::uint64_t> m_ids;
};
