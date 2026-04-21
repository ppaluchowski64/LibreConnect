#pragma once

#include <QMimeData>
#include <QUrl>

#include <functional>

class DeferredFileDragMimeData final : public QMimeData
{
public:
    using Resolver = std::function<QList<QUrl>()>;

    explicit DeferredFileDragMimeData(Resolver resolver);

    QStringList formats() const override;
    bool hasFormat(const QString& mimeType) const override;

protected:
    QVariant retrieveData(const QString& mimeType, QMetaType preferredType) const override;

private:
    void EnsureResolved() const;
    QByteArray BuildUriListData() const;

    Resolver m_resolver;
    mutable bool m_resolved = false;
    mutable QList<QUrl> m_urls;
    mutable QByteArray m_uriListData;
};
