#ifndef REMOTE_INPUT_EVENTS_H
#define REMOTE_INPUT_EVENTS_H

#include <QEvent>

#include <string>

constexpr static int RemoteInputEventBase = QEvent::User + 560;

class RemoteMediaInfoEvent final : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(RemoteInputEventBase);

    RemoteMediaInfoEvent(std::string title, std::string artist, std::string collection, std::string elapsed, bool playing)
        : QEvent(Type)
        , m_title(std::move(title))
        , m_artist(std::move(artist))
        , m_collection(std::move(collection))
        , m_elapsed(std::move(elapsed))
        , m_playing(playing) {}

    const std::string& GetTitle() const { return m_title; }
    const std::string& GetArtist() const { return m_artist; }
    const std::string& GetCollection() const { return m_collection; }
    const std::string& GetElapsed() const { return m_elapsed; }
    bool IsPlaying() const { return m_playing; }

    RemoteMediaInfoEvent* clone() const override {
        return new RemoteMediaInfoEvent(*this);
    }

private:
    std::string m_title;
    std::string m_artist;
    std::string m_collection;
    std::string m_elapsed;
    bool m_playing = false;
};

#endif // REMOTE_INPUT_EVENTS_H
