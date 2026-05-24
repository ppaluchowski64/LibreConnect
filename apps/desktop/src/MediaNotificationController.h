#pragma once

#include <QObject>
#include <QEvent>
#include <QByteArray>
#include <QString>
#include <QSettings>
#include <QTimer>
#include <QElapsedTimer>
#include <vector>

class MediaNotificationController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackInfoChanged)
    Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY trackInfoChanged)
    Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY trackInfoChanged)
    Q_PROPERTY(QString elapsedTime READ elapsedTime NOTIFY trackInfoChanged)
    Q_PROPERTY(QString durationTime READ durationTime NOTIFY trackInfoChanged)
    Q_PROPERTY(double duration READ duration NOTIFY trackInfoChanged)
    Q_PROPERTY(double position READ position NOTIFY trackInfoChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY trackInfoChanged)
    Q_PROPERTY(QString coverImageSource READ coverImageSource NOTIFY trackInfoChanged)
    Q_PROPERTY(bool hasTrackInfo READ hasTrackInfo NOTIFY trackInfoChanged)
    Q_PROPERTY(int volume READ volume NOTIFY trackInfoChanged)

public:
    explicit MediaNotificationController(QObject* parent = nullptr);
    ~MediaNotificationController() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    QString trackTitle() const { return m_trackTitle; }
    QString trackArtist() const { return m_trackArtist; }
    QString trackAlbum() const { return m_trackAlbum; }
    QString elapsedTime() const { return m_elapsedTime; }
    QString durationTime() const { return m_durationTime; }
    double duration() const { return m_duration; }
    double position() const { return m_position; }
    bool playing() const { return m_playing; }
    QString coverImageSource() const { return m_coverImageSource; }
    bool hasTrackInfo() const;
    int volume() const { return m_volume; }

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void setTrackInfo(const QString& title, const QString& artist, const QString& album, double duration, double position, bool playing);
    Q_INVOKABLE void sendMediaSignal(int signal);
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void setVolume(int volume);

signals:
    void enabledChanged();
    void trackInfoChanged();

protected:
    bool event(QEvent* event) override;

private:
    void updateNativeNotification();
    void resetTrackInfo();
    void setCoverBytes(const std::vector<uint8_t>& coverBytes);
    void restartProgressTimer();
    void onProgressTick();
    static QString formatTime(double seconds);
    static QString coverMimeType(const QByteArray& coverBytes);

    QSettings m_settings;
    bool m_enabled = false;
    QString m_trackTitle;
    QString m_trackArtist;
    QString m_trackAlbum;
    QString m_elapsedTime;
    QString m_durationTime;
    double m_duration = 0.0;
    double m_position = 0.0;
    bool m_playing = false;
    QByteArray m_coverBytes;
    QString m_coverImageSource;
    int m_volume = 0;
    QTimer m_progressTimer;
    QElapsedTimer m_progressElapsed;
    double m_progressBasePosition = 0.0;
};
