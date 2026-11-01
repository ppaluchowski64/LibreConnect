#include "WindowsVirtualFileDrag.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <shlobj.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>

namespace {
class ExportState final {
  public:
    std::shared_ptr<FileShareModule> module;
    std::vector<FileShareModule::ExportEntry> entries;
    WindowsVirtualFileDrag::ProgressFn progress;
    std::atomic<bool> failed{false};
    std::atomic<bool> cancelled{false};
    uint64_t total = 0;

    static void Pump() {
        QCoreApplication *app = QCoreApplication::instance();
        if (app && QThread::currentThread() == app->thread()) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        }
        Sleep(2);
    }

    void Register(const std::shared_ptr<RemoteFileBuffer> &buffer) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffers.push_back(buffer);
        if (cancelled.load())
            buffer->Cancel();
    }

    void Cancel() {
        cancelled.store(true);
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const std::weak_ptr<RemoteFileBuffer> &weak : m_buffers) {
            if (const std::shared_ptr<RemoteFileBuffer> buffer = weak.lock())
                buffer->Cancel();
        }
    }

    void Report(size_t index, uint64_t position) {
        const uint64_t size = entries[index].entry.GetSize().value_or(0);
        uint64_t completed = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_positions.size() != entries.size())
                m_positions.resize(entries.size());
            m_positions[index] = std::max(m_positions[index], std::min(size, position));
            for (const uint64_t value : m_positions)
                completed += value;
        }
        if (progress)
            progress(entries[index].entry.GetName().value_or(""), position, size, completed, total);
    }

  private:
    std::mutex m_mutex;
    std::vector<std::weak_ptr<RemoteFileBuffer>> m_buffers;
    std::vector<uint64_t> m_positions;
};

class RemoteFileStream final : public IStream {
  public:
    RemoteFileStream(std::shared_ptr<ExportState> state, size_t index) : m_state(std::move(state)), m_index(index) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IStream && iid != IID_ISequentialStream)
            return E_NOINTERFACE;
        *out = static_cast<IStream *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_refs);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&m_refs);
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Read(void *data, ULONG count, ULONG *read) override {
        if (read)
            *read = 0;
        if (!data && count)
            return STG_E_INVALIDPOINTER;
        if (!count)
            return S_OK;
        try {
            if (m_state->cancelled.load() || m_state->failed.load())
                return STG_E_READFAULT;
            const uint64_t size = m_state->entries[m_index].entry.GetSize().value_or(0);
            if (!m_buffer) {
                if (m_position == size && size != 0)
                    return S_FALSE;
                m_state->Report(m_index, 0);
                m_buffer = m_state->module->StreamExportFile(m_state->entries[m_index].entry);
                m_state->Register(m_buffer);
            }
            ULONG copied = 0;
            std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (copied < count) {
                const RemoteFileBuffer::ReadResult result =
                    m_buffer->Read(std::span<uint8_t>(static_cast<uint8_t *>(data) + copied, count - copied));
                copied += static_cast<ULONG>(result.bytes);
                m_position += result.bytes;
                if (read)
                    *read = copied;
                if (result.failed || m_state->cancelled.load())
                    return Fail();
                if (result.bytes) {
                    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                    if (now - m_lastReport >= std::chrono::milliseconds(50) || m_position == size) {
                        m_state->Report(m_index, m_position);
                        m_lastReport = now;
                    }
                }
                if (result.finished) {
                    if (m_position != size)
                        return Fail();
                    m_state->Report(m_index, m_position);
                    return copied == count ? S_OK : S_FALSE;
                }
                if (std::chrono::steady_clock::now() >= deadline ||
                    ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED)
                    return Fail();
                if (copied < count)
                    ExportState::Pump();
            }
            return S_OK;
        } catch (...) {
            return Fail();
        }
    }
    HRESULT STDMETHODCALLTYPE Write(const void *, ULONG, ULONG *) override {
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position) override {
        if ((origin == STREAM_SEEK_CUR && move.QuadPart == 0) ||
            (origin == STREAM_SEEK_SET && move.QuadPart == static_cast<LONGLONG>(m_position))) {
            if (position)
                position->QuadPart = m_position;
            return S_OK;
        }
        // Shell consumers read sequentially. Do not pretend to support seeking
        // into a remote stream (or retain entire files in memory to implement it).
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream *target, ULARGE_INTEGER requested, ULARGE_INTEGER *read,
                                     ULARGE_INTEGER *written) override {
        if (read)
            read->QuadPart = 0;
        if (written)
            written->QuadPart = 0;
        if (!target)
            return STG_E_INVALIDPOINTER;
        std::array<uint8_t, 64 * 1024> bytes{};
        uint64_t remaining = requested.QuadPart;
        while (remaining) {
            ULONG received = 0;
            const HRESULT hr =
                Read(bytes.data(), static_cast<ULONG>(std::min<uint64_t>(remaining, bytes.size())), &received);
            if (read)
                read->QuadPart += received;
            if (FAILED(hr))
                return hr;
            ULONG saved = 0;
            const HRESULT writeHr = target->Write(bytes.data(), received, &saved);
            if (written)
                written->QuadPart += saved;
            if (FAILED(writeHr) || saved != received)
                return FAILED(writeHr) ? writeHr : STG_E_WRITEFAULT;
            remaining -= received;
            if (hr == S_FALSE || !received)
                return S_FALSE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Revert() override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG *stat, DWORD) override {
        if (!stat)
            return STG_E_INVALIDPOINTER;
        *stat = {};
        stat->type = STGTY_STREAM;
        stat->grfMode = STGM_READ;
        stat->cbSize.QuadPart = m_state->entries[m_index].entry.GetSize().value_or(0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream **out) override {
        if (out)
            *out = nullptr;
        return E_NOTIMPL;
    }

  private:
    ~RemoteFileStream() {
        if (m_buffer && m_position < m_state->entries[m_index].entry.GetSize().value_or(0))
            m_buffer->Cancel();
    }
    HRESULT Fail() {
        m_state->failed.store(true);
        m_state->Cancel();
        return STG_E_READFAULT;
    }
    LONG m_refs = 1;
    std::shared_ptr<ExportState> m_state;
    size_t m_index;
    uint64_t m_position = 0;
    std::shared_ptr<RemoteFileBuffer> m_buffer;
    std::chrono::steady_clock::time_point m_lastReport{};
};

class VirtualFileDataObject final : public IDataObject {
  public:
    VirtualFileDataObject(std::shared_ptr<ExportState> state, std::vector<FileEntry> entries)
        : m_state(std::move(state)), m_selection(std::move(entries)),
          m_descriptor(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW)),
          m_contents(RegisterClipboardFormatW(CFSTR_FILECONTENTS)),
          m_effect(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDataObject)
            return E_NOINTERFACE;
        *out = static_cast<IDataObject *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_refs);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&m_refs);
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override {
        if (!format)
            return E_POINTER;
        if (format->dwAspect != DVASPECT_CONTENT)
            return DV_E_DVASPECT;
        if ((format->cfFormat == m_descriptor || format->cfFormat == m_effect) && (format->tymed & TYMED_HGLOBAL))
            return S_OK;
        if (format->cfFormat == m_contents && (format->tymed & TYMED_ISTREAM))
            return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override {
        if (!format || !medium)
            return E_POINTER;
        *medium = {};
        try {
            const HRESULT supported = QueryGetData(format);
            if (FAILED(supported))
                return supported;
            if (format->cfFormat == m_effect) {
                const DWORD effect = DROPEFFECT_COPY;
                return CopyGlobal(&effect, sizeof(effect), medium);
            }
            if (!EnsureManifest())
                return E_ABORT;
            if (format->cfFormat == m_contents) {
                const size_t index = static_cast<size_t>(format->lindex);
                if (index >= m_state->entries.size() || m_state->entries[index].directory)
                    return DV_E_LINDEX;
                medium->tymed = TYMED_ISTREAM;
                medium->pstm = new RemoteFileStream(m_state, index);
                return S_OK;
            }
            const size_t count = m_state->entries.size();
            const size_t length = FIELD_OFFSET(FILEGROUPDESCRIPTORW, fgd) + count * sizeof(FILEDESCRIPTORW);
            std::vector<uint8_t> storage(length, 0);
            FILEGROUPDESCRIPTORW *group = reinterpret_cast<FILEGROUPDESCRIPTORW *>(storage.data());
            group->cItems = static_cast<UINT>(count);
            for (size_t i = 0; i < count; ++i) {
                const FileShareModule::ExportEntry &item = m_state->entries[i];
                FILEDESCRIPTORW &descriptor = group->fgd[i];
                const std::wstring name = item.relativePath.wstring();
                if (name.empty() || name.size() >= MAX_PATH || name.find_first_of(L"<>:\"|?*") != std::wstring::npos)
                    return DV_E_FORMATETC;
                descriptor.dwFlags = FD_ATTRIBUTES | FD_UNICODE | FD_PROGRESSUI;
                descriptor.dwFileAttributes = item.directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
                if (!item.directory) {
                    const uint64_t size = item.entry.GetSize().value_or(0);
                    descriptor.dwFlags |= FD_FILESIZE;
                    descriptor.nFileSizeHigh = static_cast<DWORD>(size >> 32);
                    descriptor.nFileSizeLow = static_cast<DWORD>(size);
                }
                std::memcpy(descriptor.cFileName, name.c_str(), (name.size() + 1) * sizeof(wchar_t));
            }
            return CopyGlobal(storage.data(), storage.size(), medium);
        } catch (...) {
            m_state->failed.store(true);
            m_state->Cancel();
            return E_FAIL;
        }
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *medium, BOOL release) override {
        if (release && medium)
            ReleaseStgMedium(medium);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *out) override {
        if (out)
            out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (direction != DATADIR_GET)
            return E_NOTIMPL;
        std::array<FORMATETC, 3> formats{
            {{static_cast<CLIPFORMAT>(m_descriptor), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
             {static_cast<CLIPFORMAT>(m_contents), nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM},
             {static_cast<CLIPFORMAT>(m_effect), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}}};
        return SHCreateStdEnumFmtEtc(static_cast<UINT>(formats.size()), formats.data(), out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

  private:
    bool EnsureManifest() {
        if (m_ready)
            return true;
        if (m_resolving || m_state->cancelled.load() || m_state->failed.load())
            return false;
        m_resolving = true;
        try {
            if (m_state->module->GetModuleState() != ModuleState::Enabled)
                m_state->module->Enable(true);
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (m_state->module->GetModuleState() != ModuleState::Enabled) {
                if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED ||
                    std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("File transfer service unavailable");
                ExportState::Pump();
            }
            std::future<std::vector<FileShareModule::ExportEntry>> future =
                m_state->module->DescribeExport(m_selection);
            while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                if (ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED)
                    throw std::runtime_error("Connection lost");
                ExportState::Pump();
            }
            m_state->entries = future.get();
            if (m_state->entries.empty())
                throw std::runtime_error("No files to export");
            for (const FileShareModule::ExportEntry &entry : m_state->entries) {
                if (!entry.directory) {
                    const uint64_t size = entry.entry.GetSize().value_or(0);
                    if (size > std::numeric_limits<uint64_t>::max() - m_state->total)
                        throw std::runtime_error("Export too large");
                    m_state->total += size;
                }
            }
            m_ready = true;
        } catch (...) {
            m_state->failed.store(true);
            m_state->Cancel();
        }
        m_resolving = false;
        return m_ready;
    }
    static HRESULT CopyGlobal(const void *bytes, size_t length, STGMEDIUM *medium) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, length);
        if (!memory)
            return E_OUTOFMEMORY;
        void *target = GlobalLock(memory);
        if (!target) {
            GlobalFree(memory);
            return E_OUTOFMEMORY;
        }
        std::memcpy(target, bytes, length);
        GlobalUnlock(memory);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = memory;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }
    LONG m_refs = 1;
    std::shared_ptr<ExportState> m_state;
    std::vector<FileEntry> m_selection;
    UINT m_descriptor, m_contents, m_effect;
    bool m_ready = false, m_resolving = false;
};

class DragDropSource final : public IDropSource {
  public:
    explicit DragDropSource(std::shared_ptr<ExportState> state) : m_state(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropSource)
            return E_NOINTERFACE;
        *out = static_cast<IDropSource *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_refs);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG count = InterlockedDecrement(&m_refs);
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape || m_state->failed.load() || m_state->cancelled.load() ||
            ConnectionManager::GetConnectionState() != ConnectionState::CONNECTED)
            return DRAGDROP_S_CANCEL;
        return (keys & MK_LBUTTON) ? S_OK : DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

  private:
    LONG m_refs = 1;
    std::shared_ptr<ExportState> m_state;
};
} // namespace

WindowsVirtualFileDrag::Result WindowsVirtualFileDrag::Start(std::shared_ptr<FileShareModule> module,
                                                             std::vector<FileEntry> entries, ProgressFn progress) {
    if (!module || entries.empty())
        return Result::Failed;
    if (FAILED(OleInitialize(nullptr)))
        return Result::Failed;
    Result result = Result::Failed;
    VirtualFileDataObject *data = nullptr;
    DragDropSource *source = nullptr;
    std::shared_ptr<ExportState> state;
    try {
        state = std::make_shared<ExportState>();
        state->module = std::move(module);
        state->progress = std::move(progress);
        data = new VirtualFileDataObject(state, std::move(entries));
        source = new DragDropSource(state);
        DWORD effect = DROPEFFECT_NONE;
        const HRESULT hr = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
        result = state->failed.load()                                 ? Result::Failed
                 : hr == DRAGDROP_S_DROP && effect == DROPEFFECT_COPY ? Result::Completed
                                                                      : Result::Cancelled;
    } catch (...) {
        result = Result::Failed;
    }
    if (state && result != Result::Completed)
        state->Cancel();
    if (data)
        data->Release();
    if (source)
        source->Release();
    OleUninitialize();
    return result;
}
#endif
