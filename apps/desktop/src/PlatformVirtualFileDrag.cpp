#include "PlatformVirtualFileDrag.h"

#ifdef _WIN32
#include "WindowsVirtualFileDrag.h"
#else
#ifndef __APPLE__
#include "DeferredFileDragMimeData.h"
#endif

#include <QDrag>
#include <QGuiApplication>
#include <QMimeData>
#include <QString>
#include <QUrl>
#endif

#include <utility>

#ifndef __APPLE__
bool PlatformVirtualFileDrag::Start(QObject* dragSource, ResolvePathsFn resolver)
{
#ifdef _WIN32
    Q_UNUSED(dragSource);
    return WindowsVirtualFileDrag::Start(std::move(resolver));
#else
    QObject* source = dragSource ? dragSource : QGuiApplication::focusObject();
    if (!source) {
        source = QGuiApplication::instance();
    }

    if (!source) {
        return false;
    }

    auto* mimeData = new DeferredFileDragMimeData([resolver = std::move(resolver)]() mutable -> QList<QUrl> {
        QList<QUrl> urls;
        if (!resolver) {
            return urls;
        }

        const std::vector<std::filesystem::path> preparedPaths = resolver();
        for (const std::filesystem::path& path : preparedPaths) {
            if (!std::filesystem::exists(path)) {
                continue;
            }
            urls.push_back(QUrl::fromLocalFile(QString::fromStdString(path.string())));
        }

        return urls;
    });

    QDrag drag(source);
    drag.setMimeData(mimeData);
    const Qt::DropAction action = drag.exec(Qt::CopyAction);
    return action != Qt::IgnoreAction;
#endif
}

bool PlatformVirtualFileDrag::StartPromisedFiles(QObject* dragSource, std::vector<PromisedFile> files, PromiseCompletionFn completion)
{
    Q_UNUSED(dragSource);
    Q_UNUSED(files);
    Q_UNUSED(completion);
    return false;
}
#endif
