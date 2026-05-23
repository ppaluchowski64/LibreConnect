#pragma once

#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QString>
#include <QChar>
#include <QByteArray>
#include <vector>

class MobileRemoteInputController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString trackCollection READ trackCollection NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString elapsedTime READ elapsedTime NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString durationTime READ durationTime NOTIFY nowPlayingChanged)
    Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY nowPlayingChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY nowPlayingChanged)
    Q_PROPERTY(QString coverImageSource READ coverImageSource NOTIFY nowPlayingChanged)
    Q_PROPERTY(bool hasTrackInfo READ hasTrackInfo NOTIFY nowPlayingChanged)
    Q_PROPERTY(int volume READ volume NOTIFY nowPlayingChanged)

public:
    explicit MobileRemoteInputController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    bool ready() const { return m_ready; }
    bool playing() const { return m_playing; }
    QString statusMessage() const { return m_statusMessage; }
    QString trackTitle() const { return m_trackTitle; }
    QString trackArtist() const { return m_trackArtist; }
    QString trackCollection() const { return m_trackCollection; }
    QString elapsedTime() const { return m_elapsedTime; }
    QString durationTime() const { return m_durationTime; }
    double positionSeconds() const { return m_positionSeconds; }
    double durationSeconds() const { return m_durationSeconds; }
    QString coverImageSource() const { return m_coverImageSource; }
    bool hasTrackInfo() const;
    int volume() const { return m_volume; }

    Q_INVOKABLE void setSessionActive(bool active);
    Q_INVOKABLE void sendMediaSignal(int signal);
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void sendQtKeyEvent(int qtKey, const QString& text, int modifiers);
    Q_INVOKABLE void presenterPreviousSlide();
    Q_INVOKABLE void presenterNextSlide();
    Q_INVOKABLE void presenterStartSlideshow();
    Q_INVOKABLE void presenterEndSlideshow();
    void setNowPlayingInfo(
        const QString& title,
        const QString& artist,
        const QString& collection,
        const QString& elapsed,
        bool playing,
        double positionSeconds = 0.0,
        double durationSeconds = 0.0,
        const std::vector<uint8_t>& coverBytes = {},
        int volume = 0
    );

signals:
    void connectedChanged();
    void readyChanged();
    void playbackChanged();
    void statusMessageChanged();
    void nowPlayingChanged();
    void accessibilityPermissionRequired(QString message);

protected:
    bool event(QEvent* event) override;

private:
    struct KeyMapping {
        int key;
        bool requiresShift;
    };

    void refreshState();
    void requestNowPlayingUpdate();
    bool sendMappedKey(int key, bool requiresShift, int modifiers = 0);
    bool sendCharacter(QChar c, int modifiers = 0);
    void setStatusMessage(const QString& message);
    void setReadyState(bool ready);
    static KeyMapping mapQtSpecialKey(int qtKey);
    static KeyMapping mapCharacter(QChar c);
    static QString formatTime(double seconds);
    void updateAndroidMediaNotification() const;
    void hideAndroidMediaNotification() const;

    QTimer m_pollTimer;
    QTimer m_mediaInfoTimer;
    QTimer m_optimisticPlaybackTimer;
    bool m_connected = false;
    bool m_ready = false;
    bool m_playing = false;
    bool m_sessionActive = false;
    bool m_accessibilityGranted = true;
    QString m_statusMessage;
    QString m_trackTitle;
    QString m_trackArtist;
    QString m_trackCollection;
    QString m_elapsedTime;
    QString m_durationTime;
    double m_positionSeconds = 0.0;
    double m_durationSeconds = 0.0;
    int m_volume = 0;
    QByteArray m_coverBytes;
    QString m_coverImageSource;
};
