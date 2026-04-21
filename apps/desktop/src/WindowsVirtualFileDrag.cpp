#include "WindowsVirtualFileDrag.h"

#ifdef _WIN32

#include <windows.h>
#include <objidl.h>
#include <shlobj.h>

#include <QString>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <utility>

namespace {

QString PathToQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

class VirtualPathDataObject final : public IDataObject
{
public:
    explicit VirtualPathDataObject(WindowsVirtualFileDrag::ResolvePathsFn resolver)
        : m_resolver(std::move(resolver))
        , m_cfHDrop(CF_HDROP)
        , m_cfPreferredDropEffect(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT))
        , m_cfPerformedDropEffect(RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT))
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppvObject = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_refCount)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override
    {
        if (!format || !medium) {
            return E_POINTER;
        }

        if (format->cfFormat == m_cfPreferredDropEffect && (format->tymed & TYMED_HGLOBAL)) {
            return BuildDropEffect(medium, DROPEFFECT_COPY);
        }

        if (format->cfFormat == m_cfPerformedDropEffect && (format->tymed & TYMED_HGLOBAL)) {
            return BuildDropEffect(medium, DROPEFFECT_COPY);
        }

        if (format->cfFormat == m_cfHDrop && (format->tymed & TYMED_HGLOBAL)) {
            return BuildDropFiles(medium);
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium, BOOL takeOwnership) override
    {
        if (!format || !medium) {
            return E_POINTER;
        }

        if (format->cfFormat == m_cfPerformedDropEffect && medium->tymed == TYMED_HGLOBAL && medium->hGlobal) {
            auto* effectPtr = static_cast<DWORD*>(GlobalLock(medium->hGlobal));
            if (effectPtr) {
                GlobalUnlock(medium->hGlobal);
            }

            if (takeOwnership) {
                ReleaseStgMedium(medium);
            }
            return S_OK;
        }

        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
    {
        if (!format) {
            return E_POINTER;
        }

        if (format->cfFormat == m_cfPreferredDropEffect && (format->tymed & TYMED_HGLOBAL)) {
            return S_OK;
        }

        if (format->cfFormat == m_cfPerformedDropEffect && (format->tymed & TYMED_HGLOBAL)) {
            return S_OK;
        }

        if (format->cfFormat == m_cfHDrop && (format->tymed & TYMED_HGLOBAL)) {
            return S_OK;
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override
    {
        if (out) out->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** outEnum) override
    {
        if (!outEnum) {
            return E_POINTER;
        }

        if (direction != DATADIR_GET) {
            *outEnum = nullptr;
            return E_NOTIMPL;
        }

        std::array<FORMATETC, 3> formats{};
        formats[0] = { static_cast<CLIPFORMAT>(m_cfPreferredDropEffect), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        formats[1] = { static_cast<CLIPFORMAT>(m_cfPerformedDropEffect), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        formats[2] = { static_cast<CLIPFORMAT>(m_cfHDrop), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return SHCreateStdEnumFmtEtc(static_cast<UINT>(formats.size()), formats.data(), outEnum);
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    HRESULT BuildDropEffect(STGMEDIUM* medium, const DWORD effect) const
    {
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
        if (!hGlobal) {
            return E_OUTOFMEMORY;
        }

        auto* effectPtr = static_cast<DWORD*>(GlobalLock(hGlobal));
        if (!effectPtr) {
            GlobalFree(hGlobal);
            return E_OUTOFMEMORY;
        }

        *effectPtr = effect;
        GlobalUnlock(hGlobal);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = hGlobal;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    static void PumpMessagesWhileWaiting(const std::future_status status)
    {
        if (status == std::future_status::ready) {
            return;
        }

        QCoreApplication* app = QCoreApplication::instance();
        if (app && QThread::currentThread() == app->thread()) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        }
    }

    void EnsureResolved()
    {
        bool shouldResolve = false;
        {
            std::lock_guard<std::mutex> lock(m_resolveMutex);
            if (m_resolved) {
                return;
            }

            if (!m_resolving) {
                m_resolving = true;
                shouldResolve = true;
            } else {
                shouldResolve = false;
            }
        }

        if (!shouldResolve) {
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(m_resolveMutex);
                    if (m_resolved || !m_resolving) {
                        return;
                    }
                }

                PumpMessagesWhileWaiting(std::future_status::timeout);
                Sleep(10);
            }
        }

        if (!m_prepareFuture.valid()) {
            if (!m_resolver) {
                std::lock_guard<std::mutex> lock(m_resolveMutex);
                m_resolvedPaths.clear();
                m_resolving = false;
                m_resolved = true;
                return;
            }

            m_prepareFuture = std::async(std::launch::async, [resolver = std::move(m_resolver)]() mutable -> std::vector<std::filesystem::path> {
                if (!resolver) {
                    return {};
                }

                return resolver();
            });
        }

        while (true) {
            const std::future_status status = m_prepareFuture.wait_for(std::chrono::milliseconds(10));
            PumpMessagesWhileWaiting(status);
            if (status == std::future_status::ready) {
                break;
            }
        }

        std::vector<std::filesystem::path> resolvedPaths;
        try {
            resolvedPaths = m_prepareFuture.get();
        } catch (...) {
            resolvedPaths.clear();
        }

        std::lock_guard<std::mutex> lock(m_resolveMutex);
        m_resolvedPaths.clear();
        m_resolvedPaths.reserve(resolvedPaths.size());
        for (const std::filesystem::path& path : resolvedPaths) {
            if (!std::filesystem::exists(path)) {
                continue;
            }
            m_resolvedPaths.push_back(PathToQString(path));
        }
        m_resolving = false;
        m_resolved = true;
    }

    HRESULT BuildDropFiles(STGMEDIUM* medium)
    {
        // Explorer can probe CF_HDROP during hover. Defer expensive resolution
        // until the user has actually released the drag button (drop intent).
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) {
            return DV_E_FORMATETC;
        }

        EnsureResolved();
        if (m_resolvedPaths.empty()) {
            return DV_E_FORMATETC;
        }

        size_t charsRequired = 0;
        for (const QString& path : m_resolvedPaths) {
            charsRequired += static_cast<size_t>(path.size()) + 1;
        }
        charsRequired += 1;

        const SIZE_T bytesRequired = sizeof(DROPFILES) + (charsRequired * sizeof(wchar_t));
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytesRequired);
        if (!hGlobal) {
            return E_OUTOFMEMORY;
        }

        auto* dropFiles = static_cast<DROPFILES*>(GlobalLock(hGlobal));
        if (!dropFiles) {
            GlobalFree(hGlobal);
            return E_OUTOFMEMORY;
        }

        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->fWide = TRUE;

        auto* writePtr = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(dropFiles) + sizeof(DROPFILES));
        for (const QString& path : m_resolvedPaths) {
            const std::wstring widePath = path.toStdWString();
            const size_t length = widePath.size();
            std::memcpy(writePtr, widePath.c_str(), length * sizeof(wchar_t));
            writePtr += length;
            *writePtr++ = L'\0';
        }
        *writePtr = L'\0';

        GlobalUnlock(hGlobal);

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = hGlobal;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }

    ~VirtualPathDataObject() = default;

    LONG m_refCount{1};
    WindowsVirtualFileDrag::ResolvePathsFn m_resolver;
    UINT m_cfHDrop{0};
    UINT m_cfPreferredDropEffect{0};
    UINT m_cfPerformedDropEffect{0};
    std::mutex m_resolveMutex;
    bool m_resolved = false;
    bool m_resolving = false;
    std::vector<QString> m_resolvedPaths;
    std::future<std::vector<std::filesystem::path>> m_prepareFuture;
};

class DragDropSource final : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppvObject = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&m_refCount)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return static_cast<ULONG>(count);
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keyState) override
    {
        QCoreApplication* app = QCoreApplication::instance();
        if (app && QThread::currentThread() == app->thread()) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        }

        if (escape) return DRAGDROP_S_CANCEL;
        const bool keyStateLeftDown = (keyState & MK_LBUTTON) != 0;
        const bool asyncLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!keyStateLeftDown || !asyncLeftDown) return DRAGDROP_S_DROP;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    ~DragDropSource() = default;
    LONG m_refCount{1};
};

}

bool WindowsVirtualFileDrag::Start(ResolvePathsFn resolver)
{
    const HRESULT initHr = OleInitialize(nullptr);
    if (FAILED(initHr)) {
        return false;
    }

    auto* dataObject = new VirtualPathDataObject(std::move(resolver));
    auto* dropSource = new DragDropSource();

    DWORD effect = DROPEFFECT_NONE;
    const HRESULT dragHr = DoDragDrop(dataObject, dropSource, DROPEFFECT_COPY, &effect);

    dataObject->Release();
    dropSource->Release();
    OleUninitialize();

    return dragHr == DRAGDROP_S_DROP;
}

#else

bool WindowsVirtualFileDrag::Start(ResolvePathsFn)
{
    return false;
}

#endif
