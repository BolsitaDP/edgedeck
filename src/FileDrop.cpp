#include "FileDrop.h"

#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>

namespace FileDrop {

namespace {
FORMATETC HdropFormat() {
    FORMATETC format{};
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    return format;
}
} // namespace

class Zone::Target final : public IDropTarget {
public:
    explicit Target(Callback onFiles) : m_onFiles(std::move(onFiles)) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        const long left = --m_refs;
        if (left == 0) delete this;
        return static_cast<ULONG>(left);
    }

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        FORMATETC format = HdropFormat();
        m_accepts = data && data->QueryGetData(&format) == S_OK;
        *effect = Effect(*effect);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = Effect(*effect);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        m_accepts = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        const bool accepted = m_accepts;
        m_accepts = false;
        *effect = DROPEFFECT_NONE;
        if (!accepted || !data) return S_OK;

        FORMATETC format = HdropFormat();
        STGMEDIUM medium{};
        if (FAILED(data->GetData(&format, &medium))) return S_OK;

        std::vector<std::wstring> paths;
        if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
            HDROP drop = reinterpret_cast<HDROP>(medium.hGlobal);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                if (length == 0) continue;
                std::wstring path(static_cast<size_t>(length) + 1, L'\0');
                const UINT copied = DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(copied);
                if (!path.empty()) paths.push_back(std::move(path));
            }
        }
        ReleaseStgMedium(&medium);

        // What was done with them is "made a reference to", not copied: nothing of the file moves.
        *effect = DROPEFFECT_LINK;
        if (m_onFiles && !paths.empty()) m_onFiles(std::move(paths));
        return S_OK;
    }

private:
    // A reference to the file, which is what a row is; where the source does not offer that, a
    // copy - the cursor then says "Copy" and nothing is copied, which is the lesser lie.
    DWORD Effect(DWORD allowed) const {
        if (!m_accepts) return DROPEFFECT_NONE;
        if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
        if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
        return DROPEFFECT_NONE;
    }

    long m_refs = 1;
    bool m_accepts = false;
    Callback m_onFiles;
};

bool Zone::Attach(HWND window, Callback onFiles) {
    Detach();
    if (!window) return false;

    // OleInitialize rather than CoInitializeEx: drag and drop needs the full OLE services, and
    // asking again on a thread that is already a single-threaded apartment is allowed.
    if (FAILED(OleInitialize(nullptr))) return false;
    m_oleStarted = true;

    m_target = new Target(std::move(onFiles));
    if (FAILED(RegisterDragDrop(window, m_target))) {
        Detach();
        return false;
    }
    m_window = window;
    return true;
}

void Zone::Detach() {
    if (m_window) {
        if (IsWindow(m_window)) RevokeDragDrop(m_window);
        m_window = nullptr;
    }
    if (m_target) {
        m_target->Release();
        m_target = nullptr;
    }
    if (m_oleStarted) {
        OleUninitialize();
        m_oleStarted = false;
    }
}
} // namespace FileDrop
