#ifndef NETWORK_CAMERA_MODULE_H
#define NETWORK_CAMERA_MODULE_H

#include <BaseModule.h>
#include <ConnectionManager.h>
#include <SRTP_Stream.h>
#include <CameraSpecification.h>

#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>
#include <QMediaFormat>
#include <QVideoFrame>

class NetworkCameraModule final : public BaseModule {
public:

#if defined(DESKTOP_DEVICE)
    std::vector<CameraSpecification> GetCamerasSpecification() const;
#endif

private:
    std::unique_ptr<SRTP::Stream> m_videoStream;
    std::vector<uint8_t> m_localKey;
    std::vector<uint8_t> m_remoteKey;
    std::atomic<bool> m_listen{true};

    QMediaCaptureSession m_captureSession;
    QMediaRecorder m_mediaRecorder;
    QVideoFrameInput m_frameInput;

#if defined(DESKTOP_DEVICE)
    std::vector<CameraSpecification> m_camerasSpecification;
    asio::awaitable<void> UpdateCamerasSpecificationList();
#endif

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

};

#endif //NETWORK_CAMERA_MODULE_H