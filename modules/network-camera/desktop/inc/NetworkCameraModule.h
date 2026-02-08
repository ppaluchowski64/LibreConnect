#ifndef NETWORK_CAMERA_MODULE_H
#define NETWORK_CAMERA_MODULE_H

#include <BaseModule.h>
#include <ConnectionManager.h>
#include <SRTP_Stream.h>
#include <CameraSpecification.h>
#include <VirtualCamera.h>

extern "C" {
    #include <libswscale/swscale.h>
}

enum class StreamStartFailReason : uint8_t {
    None = 0,
    IncorrectConfig = 1,
    InternalError = 2
};

class NetworkCameraModule final : public BaseModule {
public:
    std::vector<CameraSpecification> GetCamerasSpecification() const;
    void SetCameraSettings(CameraSettings settings);

private:
    void StartStream();
    asio::awaitable<void> ReceiveFrames() const;

    std::unique_ptr<SRTP::Stream> m_videoStream;
    std::vector<uint8_t> m_localKey;
    std::vector<uint8_t> m_remoteKey;
    std::vector<CameraSpecification> m_camerasSpecification;
    asio::awaitable<void> UpdateCamerasSpecificationList();

    VirtualCamera m_camera;
    CameraSettings m_cameraSettings{};

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

};

#endif //NETWORK_CAMERA_MODULE_H