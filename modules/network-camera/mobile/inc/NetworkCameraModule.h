#ifndef NETWORK_CAMERA_MODULE_H
#define NETWORK_CAMERA_MODULE_H

#include <BaseModule.h>
#include <ConnectionManager.h>
#include <SRTP_Stream.h>
#include <CameraSpecification.h>
#include <CameraUtilities.h>
#include <atomic>
#include <cstdint>
#include <vector>

#include <QVideoFrame>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QMetaObject>

extern "C" {
    #include <libswscale/swscale.h>
}

enum class StreamStartFailReason : uint8_t {
    None = 0,
    IncorrectConfig = 1,
    InternalError = 2

};

//#define IOS_DEVICE

class NetworkCameraModule final : public BaseModule {
private:

#ifdef ANDROID_DEVICE
    static int32_t ComputeTargetBitrate(int width, int height, int fps);
    static void UpdateMainServiceCameraRequest(bool enabled);
    static bool StartMainServiceCameraFrameReceiver(const std::string& cameraID, int32_t width, int32_t height, int32_t fps, int32_t bitrate);
    static void StopMainServiceCameraFrameReceiver();
    static bool IsCameraFormatSupportedByCodec(const AVCodec* codec, int width, int height, int requestedFps);
    static QString QueryMainServiceCameraConfigurationsJson();

    asio::awaitable<void> SendEncodedFrame(std::vector<uint8_t> accessUnit, int32_t flags, int64_t ptsUs, uint64_t generation);

    static void StopStream_Android();
    void StartStream_Android(size_t requestID, const std::string& cameraID, CameraFormat requestedFormat);

#endif

#ifdef IOS_DEVICE
    asio::awaitable<void> EncodeAndSendFrame(const AVFrame* avFrame, uint64_t generation);

    asio::awaitable<void> StopStream_IOS();
    void StartStream_IOS(size_t requestID, const std::string& cameraID, CameraFormat requestedFormat);

    asio::awaitable<void> SendFrame_IOS(QVideoFrame frame);

    std::unique_ptr<QMediaCaptureSession> m_captureSession;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QVideoSink> m_videoSink;

    const AVCodec* m_codec{nullptr};
    AVCodecContext* m_codecContext{nullptr};
    SwsContext* m_swsContext{nullptr};
    AVFrame* m_reusableSendFrame{nullptr};
    AVFrame* m_reusableAndroidConvertFrame{nullptr};
    AVPacket* m_reusableEncodePacket{nullptr};

#endif

    bool TryReserveFrameSlot(const char* sourceTag);
    void ReleaseFrameSlot();

    asio::awaitable<void> StartStream(size_t requestID, std::string cameraID, CameraFormat requestedFormat);
    static std::vector<CameraSpecification> FetchCamerasSpecificationForCodec(const AVCodec* codec);

    std::shared_ptr<SRTP::Stream> m_videoStream;
    std::vector<uint8_t> m_localKey;
    std::vector<uint8_t> m_remoteKey;


    int64_t m_ptsCounter{0};
    std::atomic<uint16_t> m_portNumber{0};
    std::atomic<bool> m_streamActive{false};
    std::atomic<uint64_t> m_streamGeneration{0};
    std::atomic<uint32_t> m_inFlightSendFrames{0};
    std::atomic<uint32_t> m_droppedFramesBackpressure{0};
    std::atomic<bool> m_qtPipelineStopped{true};
    QMetaObject::Connection m_videoFrameConnection;

    std::vector<std::vector<uint8_t>> m_h264ParameterSets;
    uint8_t m_h264LengthSize{4};
    bool m_codecConfigSent{false};
    CodecID m_streamCodecId{CodecID::H264};

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;
    void OnInitialize() override;

    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;

#ifdef ANDROID_DEVICE
public:
    void OnAndroidEncodedFrame(std::vector<uint8_t> accessUnit, int32_t flags, int64_t ptsUs);
#endif
};

#endif //NETWORK_CAMERA_MODULE_H
