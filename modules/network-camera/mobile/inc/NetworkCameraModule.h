#ifndef NETWORK_CAMERA_MODULE_H
#define NETWORK_CAMERA_MODULE_H

#include <BaseModule.h>
#include <ConnectionManager.h>
#include <SRTP_Stream.h>
#include <CameraSpecification.h>
#include <CameraUtilities.h>

#include <QVideoFrame>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>

extern "C" {
    #include <libswscale/swscale.h>
}

enum class StreamStartFailReason : uint8_t {
    None = 0,
    IncorrectConfig = 1,
    InternalError = 2

};

class NetworkCameraModule final : public BaseModule {
private:
    asio::awaitable<void> StartStream(size_t requestID, std::string cameraID, CameraFormat requestedFormat);
    asio::awaitable<void> SendFrame(QVideoFrame frame);

    std::unique_ptr<SRTP::Stream> m_videoStream;
    std::vector<uint8_t> m_localKey;
    std::vector<uint8_t> m_remoteKey;

    std::unique_ptr<QMediaCaptureSession> m_captureSession;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QVideoSink> m_videoSink;
    const AVCodec* m_codec{nullptr};
    AVCodecContext* m_codecContext{nullptr};
    SwsContext* m_swsContext{nullptr};

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;
    void OnInitialize() override;

    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

};

#endif //NETWORK_CAMERA_MODULE_H