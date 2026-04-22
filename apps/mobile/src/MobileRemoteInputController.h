#pragma once

#include <QObject>
#include <QEvent>
#include <QTimer>
#include <QString>
#include <QChar>

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
    Q_PROPERTY(bool hasTrackInfo READ hasTrackInfo NOTIFY nowPlayingChanged)

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
    bool hasTrackInfo() const;

    Q_INVOKABLE void setSessionActive(bool active);
    Q_INVOKABLE void sendMediaSignal(int signal);
    Q_INVOKABLE void sendQtKeyEvent(int qtKey, const QString& text, int modifiers);
    Q_INVOKABLE void setNowPlayingInfo(
        const QString& title,
        const QString& artist,
        const QString& collection,
        const QString& elapsed,
        bool playing
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
    bool sendMappedKey(int key, bool requiresShift);
    bool sendCharacter(QChar c);
    void setStatusMessage(const QString& message);
    void setReadyState(bool ready);
    static KeyMapping mapQtSpecialKey(int qtKey);
    static KeyMapping mapCharacter(QChar c);
    void updateAndroidMediaNotification() const;
    void hideAndroidMediaNotification() const;

    QTimer m_pollTimer;
    QTimer m_mediaInfoTimer;
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
};
