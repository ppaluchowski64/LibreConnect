#ifndef NETWORK_CAMERA_MODULE_H
#define NETWORK_CAMERA_MODULE_H

#include <BaseModule.h>
#include <ConnectionManager.h>
#include <SRTP_Stream.h>
#include <CameraSpecification.h>
#include <CameraUtilities.h>
#include <VirtualCamera.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

extern "C" {
    #include <libswscale/swscale.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/hwcontext.h>
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
    asio::awaitable<void> StartStream();
    void ProcessEncodedFrame(const std::vector<uint8_t>& frameBuffer);
    asio::awaitable<void> ReceiveFrames();

    std::shared_ptr<SRTP::Stream> m_videoStream;
    std::vector<uint8_t> m_localKey;
    std::vector<uint8_t> m_remoteKey;
    mutable std::mutex m_camerasSpecificationMutex;
    std::vector<CameraSpecification> m_camerasSpecification;
    asio::awaitable<void> UpdateCamerasSpecificationList();

    VirtualCamera m_camera;
    CameraSettings m_cameraSettings{};

    AVCodecContext* m_codecContext{nullptr};
    const AVCodec* m_codec{nullptr};
    CodecID m_activeCodecId{CodecID::H264};

    AVFrame* m_frame{nullptr};
    AVFrame* m_hwTransferFrame{nullptr};
    AVFrame* m_frameNv12{nullptr};
    AVPacket* m_packet{nullptr};
    SwsContext* m_swsContext{nullptr};
    AVPixelFormat m_swsSrcFormat{AV_PIX_FMT_NONE};
    int m_swsWidth{0};
    int m_swsHeight{0};
    int m_swsDstWidth{0};
    int m_swsDstHeight{0};
    bool m_seenVps{false};
    bool m_seenSps{false};
    bool m_seenPps{false};
    std::atomic<bool> m_waitForIdrAfterLoss{false};
    std::atomic<uint64_t> m_waitForIdrStartMs{0};
    std::atomic<uint32_t> m_waitForIdrDroppedFrames{0};
    std::atomic<int64_t> m_decodePacketPts{0};
    std::atomic<bool> m_acceptFrames{false};
    std::atomic<bool> m_receiveFramesRunning{false};
    std::vector<uint8_t> m_outputFrameBuffer;

protected:
    void EnableResponseCallbacks() override;
    void DisableResponseCallbacks() override;

    void OnInitialize() override;
    asio::awaitable<void> OnEnable() override;
    asio::awaitable<void> OnDisable() override;
    asio::awaitable<void> OnShutdown() override;

    const char* GetModuleName() const override;
    ModuleType GetModuleType() const override;

};

#endif //NETWORK_CAMERA_MODULE_H

