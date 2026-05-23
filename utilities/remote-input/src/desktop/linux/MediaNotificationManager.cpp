#include "MediaNotificationManager.h"
#include "InputTypes.h"
#include "MediaTrackInfo.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath> // NOLINT
#include <QDir>
#include <QFile>
#include <QStringList> // NOLINT
#include <QVariantMap>
#include <QMetaObject>

#include <chrono>
#include <cmath>
#include <mutex>

namespace {
    std::mutex g_mutex;

    bool g_isPlaying = false;
    double g_position = 0.0;
    int64_t g_lastUpdateMicros = 0;

    TrackMetadata g_metadata;
    QString g_serviceName;

    class MprisPlayer : public QObject {
        Q_OBJECT
        Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

        Q_PROPERTY(QString PlaybackStatus READ PlaybackStatus)
        Q_PROPERTY(QVariantMap Metadata READ Metadata)
        Q_PROPERTY(qlonglong Position READ Position)

        Q_PROPERTY(QString Identity MEMBER m_id CONSTANT)
        Q_PROPERTY(QString DesktopEntry MEMBER m_entry CONSTANT)
        Q_PROPERTY(double Rate MEMBER m_one CONSTANT)

        Q_PROPERTY(double MinimumRate MEMBER m_one CONSTANT)
        Q_PROPERTY(double MaximumRate MEMBER m_one CONSTANT)
        Q_PROPERTY(double Volume MEMBER m_one CONSTANT)
        Q_PROPERTY(bool CanGoNext MEMBER m_true CONSTANT)
        Q_PROPERTY(bool CanGoPrevious MEMBER m_true CONSTANT)
        Q_PROPERTY(bool CanPlay MEMBER m_true CONSTANT)
        Q_PROPERTY(bool CanPause MEMBER m_true CONSTANT)
        Q_PROPERTY(bool CanSeek MEMBER m_true CONSTANT)
        Q_PROPERTY(bool CanControl MEMBER m_true CONSTANT)

        private:
            const QString m_id = "LibreConnect";
            const QString m_entry = "LibreConnect";
            const double m_one = 1.0;
            const bool m_true = true;

        public:
            MprisPlayer() : QObject(nullptr) {}

            [[nodiscard]] QString PlaybackStatus() const {
                (void)this;
                std::lock_guard<std::mutex> lock(g_mutex);
                return g_isPlaying ? "Playing" : "Paused";
            }

            [[nodiscard]] qlonglong Position() const {
                (void)this;
                std::lock_guard<std::mutex> lock(g_mutex);
                double currentPos = MediaTrackInfo::CalculateInterpolatedPosition(g_position, g_lastUpdateMicros, g_isPlaying);

                if (g_metadata.duration > 0.0 && currentPos > g_metadata.duration)
                    currentPos = g_metadata.duration;

                return static_cast<qlonglong>(currentPos * 1000000.0);
            }

            [[nodiscard]] QVariantMap Metadata() const {
                (void)this;
                std::lock_guard<std::mutex> lock(g_mutex);
                QVariantMap map;
                map["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack"));

                if (!g_metadata.title.empty())
                    map["xesam:title"] = QString::fromStdString(g_metadata.title);

                if (!g_metadata.artist.empty())
                    map["xesam:artist"] = QStringList{QString::fromStdString(g_metadata.artist)};

                if (!g_metadata.album.empty())
                    map["xesam:album"] = QString::fromStdString(g_metadata.album);

                if (g_metadata.duration > 0.0)
                    map["mpris:length"] = static_cast<qlonglong>(g_metadata.duration * 1000000.0);

                if (!g_metadata.cover.empty()) {
                    QString coverPath = QDir::temp().filePath("libreconnect_mpris.jpg");
                    QFile file(coverPath);

                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(reinterpret_cast<const char*>(g_metadata.cover.data()), static_cast<qint64>(g_metadata.cover.size()));
                        file.close();
                        map["mpris:artUrl"] = "file://" + coverPath;
                    }
                }

                return map;
            }

        public slots:
            void Next() const {
                (void)this;
                MediaNotificationManager::InvokeAction(MediaSignal::NextTrack);
            }

            void Previous() const {
                (void)this;
                MediaNotificationManager::InvokeAction(MediaSignal::PreviousTrack);
            }

            void Pause() const {
                (void)this;
                MediaNotificationManager::InvokeAction(MediaSignal::PlayPause);
            }

            void PlayPause() const {
                (void)this;
                MediaNotificationManager::InvokeAction(MediaSignal::PlayPause);
            }

            void Stop() const {
                (void)this;
                MediaNotificationManager::InvokeAction(MediaSignal::PlayPause);
            }

            void Play() const {
                (void)this;
                MediaNotificationManager::InvokeAction(MediaSignal::PlayPause);
            }

            void Seek(qlonglong offset) const {
                (void)this;
                MediaNotificationManager::InvokeSeek(static_cast<double>(Position()) / 1000000.0 + static_cast<double>(offset) / 1000000.0);
            }

            void SetPosition(const QDBusObjectPath&, qlonglong pos) const {
                (void)this;
                MediaNotificationManager::InvokeSeek(static_cast<double>(pos) / 1000000.0);
            }

        signals:
            void Seeked(qlonglong /*position*/);
    };

    MprisPlayer* g_player = nullptr;

    void NotifyPropertiesChanged() {
        MprisPlayer* player = nullptr;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            player = g_player;
        }

        if (!player)
            return;

        QMetaObject::invokeMethod(player, [player]() {
            QDBusMessage msg = QDBusMessage::createSignal("/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged");
            QVariantMap changed;

            changed["PlaybackStatus"] = player->PlaybackStatus();
            changed["Metadata"] = player->Metadata();
            msg << "org.mpris.MediaPlayer2.Player" << changed << QStringList();

            (void)QDBusConnection::sessionBus().send(msg);
        });
    }
}

void MediaNotificationManager::Show() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_player)
        return;

    g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    g_player = new MprisPlayer();
    g_player->moveToThread(QCoreApplication::instance()->thread());

    MprisPlayer* newPlayer = g_player;

    QMetaObject::invokeMethod(QCoreApplication::instance(), [newPlayer]() {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.registerObject("/org/mpris/MediaPlayer2", newPlayer, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        g_serviceName = QString("org.mpris.MediaPlayer2.LibreConnect.instance") + QString::number(QCoreApplication::applicationPid());
        bus.registerService(g_serviceName);
    });
}

void MediaNotificationManager::Hide() {
    MprisPlayer* playerToDelete = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (!g_player)
            return;

        playerToDelete = g_player;
        g_player = nullptr;
    }

    QMetaObject::invokeMethod(QCoreApplication::instance(), [playerToDelete]() {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject("/org/mpris/MediaPlayer2");
        bus.unregisterService(g_serviceName);

        playerToDelete->deleteLater();
    });
}

void MediaNotificationManager::UpdateMetadata(const TrackMetadata& metadata) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_metadata = metadata;
    }

    NotifyPropertiesChanged();
}

void MediaNotificationManager::UpdatePlaybackState(bool isPlaying, double position) {
    bool stateChanged = false;
    bool positionJumped = false;
    MprisPlayer* player = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        stateChanged = (g_isPlaying != isPlaying);
        positionJumped = std::abs(g_position - position) > 1.5;

        g_isPlaying = isPlaying;
        g_position = position;

        g_lastUpdateMicros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        player = g_player;
    }

    if (stateChanged)
        NotifyPropertiesChanged();

    if (positionJumped && player) {
        auto dbusPos = static_cast<qlonglong>(position * 1000000.0);

        QMetaObject::invokeMethod(player, [player, dbusPos]() {
            emit player->Seeked(dbusPos);
        });
    }
}

bool MediaNotificationManager::IsVisible() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_player != nullptr;
}

#include "MediaNotificationManager.moc"
