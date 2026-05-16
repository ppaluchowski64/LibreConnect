#pragma once

#include <QUrl>
#include <QString>

#include <filesystem>

namespace ExternalFileOpener {
bool OpenLocalFile(const std::filesystem::path& path);
bool OpenLocalFile(const QString& path);
bool OpenUrl(const QUrl& url);
}
