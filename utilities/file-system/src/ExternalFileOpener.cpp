#include <ExternalFileOpener.h>

#include <QDesktopServices>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {
QString PathToQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

#if defined(Q_OS_LINUX)
bool StartDetachedWithStableLocale(const QString& program, const QStringList& arguments)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LC_NUMERIC"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);

    return process.startDetached();
}

bool OpenWithGio(const QUrl& url)
{
    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (gio.isEmpty()) {
        return false;
    }

    return StartDetachedWithStableLocale(gio, { QStringLiteral("open"), url.toString() });
}
#endif
}

namespace ExternalFileOpener {
bool OpenLocalFile(const std::filesystem::path& path)
{
    if (path.empty()) {
        return false;
    }

    return OpenLocalFile(PathToQString(path));
}

bool OpenLocalFile(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return false;
    }

    return OpenUrl(QUrl::fromLocalFile(trimmedPath));
}

bool OpenUrl(const QUrl& url)
{
    if (!url.isValid() || url.isEmpty()) {
        return false;
    }

#if defined(Q_OS_LINUX)
    if (url.isLocalFile() && OpenWithGio(url)) {
        return true;
    }
#endif

    return QDesktopServices::openUrl(url);
}
}
