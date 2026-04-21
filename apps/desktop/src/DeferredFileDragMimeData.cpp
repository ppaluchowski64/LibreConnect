#include "DeferredFileDragMimeData.h"

DeferredFileDragMimeData::DeferredFileDragMimeData(Resolver resolver)
    : m_resolver(std::move(resolver))
{
}

QStringList DeferredFileDragMimeData::formats() const
{
    return QStringList{QStringLiteral("text/uri-list")};
}

bool DeferredFileDragMimeData::hasFormat(const QString& mimeType) const
{
    return mimeType == QStringLiteral("text/uri-list");
}

QVariant DeferredFileDragMimeData::retrieveData(const QString& mimeType, const QMetaType preferredType) const
{
    if (mimeType != QStringLiteral("text/uri-list")) {
        return {};
    }

    EnsureResolved();
    if (preferredType.id() == QMetaType::QByteArray || !preferredType.isValid()) {
        return m_uriListData;
    }

    return QVariant::fromValue(m_urls);
}

void DeferredFileDragMimeData::EnsureResolved() const
{
    if (m_resolved) {
        return;
    }

    m_resolved = true;
    if (m_resolver) {
        m_urls = m_resolver();
    }

    m_uriListData = BuildUriListData();
}

QByteArray DeferredFileDragMimeData::BuildUriListData() const
{
    QByteArray data;
    for (const QUrl& url : m_urls) {
        data += url.toString(QUrl::FullyEncoded).toUtf8();
        data += "\r\n";
    }

    return data;
}
