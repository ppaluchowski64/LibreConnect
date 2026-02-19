#ifndef FILE_SHARE_EVENTS_H
#define FILE_SHARE_EVENTS_H

#include <QEvent>
#include <FileEntry.h>

constexpr static int FileShareEventBase = QEvent::User + 200;

enum class TransferOperation : bool {
    Fetch,
    Post
};

class EntryTransferProgressEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(FileShareEventBase);
    explicit EntryTransferProgressEvent(const FileEntry& entry, const size_t totalBytes, const size_t bytesTransferred, TransferOperation operation) : QEvent(Type), m_fileEntry(entry), m_totalBytes(totalBytes), m_bytesTransferred(bytesTransferred), m_operation(operation) {}

    FileEntry GetFileEntry() const { return m_fileEntry; };
    size_t GetTotalBytes() const { return m_totalBytes; }
    size_t GetBytesTransferred() const { return m_bytesTransferred; }
    TransferOperation GetOperation() const { return m_operation; }

    void SetBytesTransferred(const size_t bytesTransferred) { m_bytesTransferred = bytesTransferred; }

    EntryTransferProgressEvent* clone() const override {
        return new EntryTransferProgressEvent(*this);
    }

private:
    FileEntry m_fileEntry;
    size_t m_totalBytes;
    size_t m_bytesTransferred;
    TransferOperation m_operation;

};

class EntryTransferResultEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(FileShareEventBase+1);
    explicit EntryTransferResultEvent(const FileEntry& entry, const bool success) : QEvent(Type), m_fileEntry(entry), m_success(success) {}

    FileEntry GetFileEntry() const { return m_fileEntry; }
    bool Success() const { return m_success; }

private:
    FileEntry m_fileEntry;
    bool m_success;
};

class FetchDirectoryEntriesResultEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(FileShareEventBase+2);
    explicit FetchDirectoryEntriesResultEvent(std::string path, std::vector<FileEntry> entries) : QEvent(Type), m_path(std::move(path)), m_entries(std::move(entries)) {}

    std::vector<FileEntry>&& TakeEntries() { return std::move(m_entries); }
    std::vector<FileEntry> GetEntries() const { return m_entries; }
    std::string GetPath() const { return m_path; }

private:
    std::string m_path;
    std::vector<FileEntry> m_entries;
};

class EntriesCopyResultEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(FileShareEventBase+1);
    explicit EntriesCopyResultEvent(std::vector<FileEntry> entry, const bool success) : QEvent(Type), m_fileEntries(std::move(entry)), m_success(success) {}

    std::vector<FileEntry> GetFileEntries() const { return m_fileEntries; }
    std::vector<FileEntry>&& TakeFileEntries() { return std::move(m_fileEntries); }
    bool Success() const { return m_success; }

private:
    std::vector<FileEntry> m_fileEntries;
    bool m_success;
};


#endif // FILE_SHARE_EVENTS_H