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


#endif // FILE_SHARE_EVENTS_H