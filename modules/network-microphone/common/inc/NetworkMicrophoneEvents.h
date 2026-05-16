#ifndef NETWORK_MICROPHONE_EVENTS_H
#define NETWORK_MICROPHONE_EVENTS_H

#include <QEvent>
#include <MicrophoneTypes.h>
#include <VMicTypes.h>

constexpr static int EventBase = QEvent::User + 600;

class AudioDeviceListEvent : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(EventBase);
    explicit AudioDeviceListEvent(const std::vector<MicrophoneDevice>& devices) : QEvent(Type), m_devices(devices) {}

    std::vector<MicrophoneDevice> GetAudioDeviceList() const {
        return m_devices;
    }

    AudioDeviceListEvent* clone() const override {
        return new AudioDeviceListEvent(*this);
    }

private:
    std::vector<MicrophoneDevice> m_devices;

};

class VirtualMicrophoneErrorEvent : public QEvent {
public:
    static constexpr QEvent::Type Type = static_cast<QEvent::Type>(EventBase + 1);
    explicit VirtualMicrophoneErrorEvent(const VMicResult error) : QEvent(Type), m_error(error) {}

    VMicResult GetError() const {return m_error;}

    VirtualMicrophoneErrorEvent* clone() const override {
        return new VirtualMicrophoneErrorEvent(*this);
    }

private:
    VMicResult m_error;

};

#endif //NETWORK_MICROPHONE_EVENTS_H