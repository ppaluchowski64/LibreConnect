#include "MediaTrackInfo.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QVariantMap>
#include <QStringList>
#include <QUrl>
#include <QFile>

#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace {
    QString GetActiveMprisPlayer() {
        if (!QDBusConnection::sessionBus().isConnected())
            return {};

        QDBusInterface dbus("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
        QDBusReply<QStringList> reply = dbus.call("ListNames");

        if (!reply.isValid())
            return {};

        QString firstPlayer;

        for (const QString& name : reply.value()) {
            if (name.startsWith("org.mpris.MediaPlayer2.")) {
                if (name.contains(QLatin1String("LibreConnect"))) {
                    continue;
                }

                if (firstPlayer.isEmpty())
                    firstPlayer = name;

                QDBusInterface props(name, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties");
                QDBusReply<QVariant> status = props.call("Get", "org.mpris.MediaPlayer2.Player", "PlaybackStatus");

                if (status.isValid() && status.value().toString() == "Playing")
                    return name;
            }
        }

        return firstPlayer;
    }
}

std::optional<TrackMetadata> MediaTrackInfo::GetCurrentTrack() {
    QString playerService = GetActiveMprisPlayer();

    if (playerService.isEmpty())
        return std::nullopt;

    QDBusInterface props(playerService, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties");

    QDBusReply<QVariant> metaReply = props.call("Get", "org.mpris.MediaPlayer2.Player", "Metadata");
    QDBusReply<QVariant> statusReply = props.call("Get", "org.mpris.MediaPlayer2.Player", "PlaybackStatus");
    QDBusReply<QVariant> posReply = props.call("Get", "org.mpris.MediaPlayer2.Player", "Position");

    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!metaReply.isValid())
        return std::nullopt;

    TrackMetadata info{};
    info.playing = (statusReply.isValid() && statusReply.value().toString() == "Playing");

    double rawPos = 0.0;

    if (posReply.isValid())
        rawPos = static_cast<double>(posReply.value().toLongLong()) / 1000000.0;

    info.position = CalculateInterpolatedPosition(rawPos, now, info.playing);

    auto arg = metaReply.value().value<QDBusArgument>();
    QVariantMap metadata;
    arg >> metadata;

    info.title = metadata.value("xesam:title").toString().toStdString();
    info.album = metadata.value("xesam:album").toString().toStdString();

    if (metadata.contains("xesam:artist")) {
        QStringList artists = metadata.value("xesam:artist").toStringList();
        info.artist = artists.join(", ").toStdString();
    }

    if (metadata.contains("mpris:length"))
        info.duration = static_cast<double>(metadata.value("mpris:length").toLongLong()) / 1000000.0;

    if (info.duration > 0.0 && info.position > info.duration)
        info.position = info.duration;

    if (metadata.contains("mpris:artUrl")) {
        QString artUrl = metadata.value("mpris:artUrl").toString();
        QUrl url(artUrl);

        if (url.isLocalFile()) {
            QFile file(url.toLocalFile());

            if (file.open(QIODevice::ReadOnly)) {
                QByteArray data = file.readAll();
                info.cover.assign(data.begin(), data.end());
            }
        }
    }

    return info;
}

void MediaTrackInfo::SetPosition(double seconds) {
    QString playerService = GetActiveMprisPlayer();

    if (playerService.isEmpty())
        return;

    QDBusInterface props(playerService, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties");
    QDBusReply<QVariant> metaReply = props.call("Get", "org.mpris.MediaPlayer2.Player", "Metadata");

    if (metaReply.isValid()) {
        auto arg = metaReply.value().value<QDBusArgument>();
        QVariantMap metadata;
        arg >> metadata;

        QVariant trackIdVar = metadata.value("mpris:trackid");
        QDBusObjectPath trackIdPath;

        if (trackIdVar.canConvert<QDBusObjectPath>())
            trackIdPath = trackIdVar.value<QDBusObjectPath>();
        else
            trackIdPath = QDBusObjectPath(trackIdVar.toString());

        if (!trackIdPath.path().isEmpty()) {
            QDBusInterface player(playerService, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player");
            auto microseconds = static_cast<qint64>(seconds * 1000000.0);

            player.call("SetPosition", QVariant::fromValue(trackIdPath), microseconds);
        }
    }
}

bool MediaTrackInfo::ControlPlayback(MediaSignal signal) {
    QString playerService = GetActiveMprisPlayer();

    if (playerService.isEmpty())
        return false;

    QDBusInterface player(playerService, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player");

    switch (signal) {
        case MediaSignal::PlayPause:
            player.call("PlayPause");
            return true;
        case MediaSignal::NextTrack:
            player.call("Next");
            return true;
        case MediaSignal::PreviousTrack:
            player.call("Previous");
            return true;
        default:
            return false;
    }
}
