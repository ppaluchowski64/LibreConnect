#include "TemporaryStorageController.h"

#include <FileSystemManager.h>

namespace
{
QString PathToQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}
}

TemporaryStorageController::TemporaryStorageController(QObject* parent)
    : QObject(parent)
{
    refreshTemporaryStoragePath();
    if (m_temporaryStoragePath.isEmpty()) {
        setStatusMessage(QStringLiteral("Temporary storage path is unavailable."));
    } else {
        setStatusMessage(QStringLiteral("Some temporary files may not be deleted fully. Click Clear Temporary Storage to clear."));
    }
}

bool TemporaryStorageController::clearTemporaryStorage()
{
    refreshTemporaryStoragePath();
    if (m_temporaryStoragePath.isEmpty()) {
        setStatusMessage(QStringLiteral("Temporary storage path is unavailable."));
        return false;
    }

    const bool success = FileSystemManager::ClearTemporaryStorage();
    setStatusMessage(success
        ? QStringLiteral("Temporary storage cleared.")
        : QStringLiteral("Failed to clear temporary storage. Some files may still be in use."));
    return success;
}

void TemporaryStorageController::refreshTemporaryStoragePath()
{
    const std::filesystem::path path = FileSystemManager::GetTemporaryStoragePath();
    setTemporaryStoragePath(path.empty() ? QString() : PathToQString(path));
}

void TemporaryStorageController::setTemporaryStoragePath(const QString& temporaryStoragePath)
{
    if (m_temporaryStoragePath == temporaryStoragePath) {
        return;
    }

    m_temporaryStoragePath = temporaryStoragePath;
    emit temporaryStoragePathChanged();
}

void TemporaryStorageController::setStatusMessage(const QString& statusMessage)
{
    if (m_statusMessage == statusMessage) {
        return;
    }

    m_statusMessage = statusMessage;
    emit statusMessageChanged();
}
