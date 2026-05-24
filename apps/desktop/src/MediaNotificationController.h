#pragma once

#include <QObject>
#include <QEvent>
#include <QString>
#include <QSettings>

class MediaNotificationController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackInfoChanged)
    Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY trackInfoChanged)
    Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY trackInfoChanged)
    Q_PROPERTY(double duration READ duration NOTIFY trackInfoChanged)
    Q_PROPERTY(double position READ position NOTIFY trackInfoChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY trackInfoChanged)

public:
    explicit MediaNotificationController(QObject* parent = nullptr);
    ~MediaNotificationController() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    QString trackTitle() const { return m_trackTitle; }
    QString trackArtist() const { return m_trackArtist; }
    QString trackAlbum() const { return m_trackAlbum; }
    double duration() const { return m_duration; }
    double position() const { return m_position; }
    bool playing() const { return m_playing; }

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void setTrackInfo(const QString& title, const QString& artist, const QString& album, double duration, double position, bool playing);

signals:
    void enabledChanged();
    void trackInfoChanged();

protected:
    bool event(QEvent* event) override;

private:
    void updateNativeNotification();

    QSettings m_settings;
    bool m_enabled = false;
    QString m_trackTitle;
    QString m_trackArtist;
    QString m_trackAlbum;
    double m_duration = 0.0;
    double m_position = 0.0;
    bool m_playing = false;
};
