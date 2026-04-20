#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <PermissionManager.h>

#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>

#include <asio.hpp>
#include <asio/co_spawn.hpp>

constexpr uint32_t kMaxQueuedFrameJobs = 8;
constexpr uint32_t kBackpressureLogEvery = 120;
constexpr int64_t kPerfLogIntervalUs = 5'000'000;

int64_t NetworkCameraModule::PerfNowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void NetworkCameraModule::ResetPerformanceStats() {
    m_perfIngressFrames.store(0, std::memory_order_relaxed);
    m_perfIngressBytes.store(0, std::memory_order_relaxed);
    m_perfQueuedFrames.store(0, std::memory_order_relaxed);
    m_perfProcessedFrames.store(0, std::memory_order_relaxed);
    m_perfProcessedBytes.store(0, std::memory_order_relaxed);
    m_perfSentFrames.store(0, std::memory_order_relaxed);
    m_perfSentNals.store(0, std::memory_order_relaxed);
    m_perfDroppedInactive.store(0, std::memory_order_relaxed);
    m_perfDroppedBackpressure.store(0, std::memory_order_relaxed);
    m_perfDroppedAwaitingKeyframe.store(0, std::memory_order_relaxed);
    m_perfQueueWaitUs.store(0, std::memory_order_relaxed);
    m_perfWorkUs.store(0, std::memory_order_relaxed);
    m_perfSendAwaitUs.store(0, std::memory_order_relaxed);
    m_perfMaxQueueDepth.store(0, std::memory_order_relaxed);

    const int64_t nowUs = PerfNowUs();
    m_perfWindowStartUs.store(nowUs, std::memory_order_relaxed);
    m_perfNextLogUs.store(nowUs + kPerfLogIntervalUs, std::memory_order_relaxed);
}

void NetworkCameraModule::NoteCaptureIngress(const size_t bytes) {
    m_perfIngressFrames.fetch_add(1, std::memory_order_relaxed);
    m_perfIngressBytes.fetch_add(static_cast<uint64_t>(bytes), std::memory_order_relaxed);
    MaybeLogPerformanceStats();
}

void NetworkCameraModule::NoteDropInactive() {
    m_perfDroppedInactive.fetch_add(1, std::memory_order_relaxed);
    MaybeLogPerformanceStats();
}

void NetworkCameraModule::NoteDropBackpressure() {
    m_perfDroppedBackpressure.fetch_add(1, std::memory_order_relaxed);
    MaybeLogPerformanceStats();
}

void NetworkCameraModule::NoteDropAwaitingKeyframe() {
    m_perfDroppedAwaitingKeyframe.fetch_add(1, std::memory_order_relaxed);
    MaybeLogPerformanceStats();
}

void NetworkCameraModule::NoteFrameQueued(const uint32_t queueDepth) {
    m_perfQueuedFrames.fetch_add(1, std::memory_order_relaxed);

    uint32_t peak = m_perfMaxQueueDepth.load(std::memory_order_relaxed);
    while (queueDepth > peak && !m_perfMaxQueueDepth.compare_exchange_weak(
        peak,
        queueDepth,
        std::memory_order_relaxed,
        std::memory_order_relaxed
    )) {
    }

    MaybeLogPerformanceStats();
}

void NetworkCameraModule::NoteFrameProcessed(
    const uint64_t queueWaitUs,
    const uint64_t workUs,
    const uint64_t sendAwaitUs,
    const size_t bytes,
    const size_t sentNalCount
) {
    m_perfProcessedFrames.fetch_add(1, std::memory_order_relaxed);
    m_perfQueueWaitUs.fetch_add(queueWaitUs, std::memory_order_relaxed);
    m_perfWorkUs.fetch_add(workUs, std::memory_order_relaxed);
    m_perfSendAwaitUs.fetch_add(sendAwaitUs, std::memory_order_relaxed);
    m_perfProcessedBytes.fetch_add(static_cast<uint64_t>(bytes), std::memory_order_relaxed);

    if (sentNalCount > 0) {
        m_perfSentFrames.fetch_add(1, std::memory_order_relaxed);
        m_perfSentNals.fetch_add(static_cast<uint64_t>(sentNalCount), std::memory_order_relaxed);
    }

    MaybeLogPerformanceStats();
}

void NetworkCameraModule::MaybeLogPerformanceStats() {
    const int64_t nowUs = PerfNowUs();
    int64_t nextLogUs = m_perfNextLogUs.load(std::memory_order_relaxed);
    if (nowUs < nextLogUs) {
        return;
    }

    while (!m_perfNextLogUs.compare_exchange_weak(
        nextLogUs,
        nowUs + kPerfLogIntervalUs,
        std::memory_order_relaxed,
        std::memory_order_relaxed
    )) {
        if (nowUs < nextLogUs) {
            return;
        }
    }

    const int64_t windowStartUs = m_perfWindowStartUs.exchange(nowUs, std::memory_order_relaxed);
    if (windowStartUs <= 0 || nowUs <= windowStartUs) {
        return;
    }

    const double windowSeconds = static_cast<double>(nowUs - windowStartUs) / 1'000'000.0;
    if (windowSeconds <= 0.0) {
        return;
    }

    const uint64_t ingressFrames = m_perfIngressFrames.exchange(0, std::memory_order_relaxed);
    const uint64_t ingressBytes = m_perfIngressBytes.exchange(0, std::memory_order_relaxed);
    const uint64_t queuedFrames = m_perfQueuedFrames.exchange(0, std::memory_order_relaxed);
    const uint64_t processedFrames = m_perfProcessedFrames.exchange(0, std::memory_order_relaxed);
    const uint64_t processedBytes = m_perfProcessedBytes.exchange(0, std::memory_order_relaxed);
    const uint64_t sentFrames = m_perfSentFrames.exchange(0, std::memory_order_relaxed);
    const uint64_t sentNals = m_perfSentNals.exchange(0, std::memory_order_relaxed);
    const uint64_t droppedInactive = m_perfDroppedInactive.exchange(0, std::memory_order_relaxed);
    const uint64_t droppedBackpressure = m_perfDroppedBackpressure.exchange(0, std::memory_order_relaxed);
    const uint64_t droppedAwaitingKeyframe = m_perfDroppedAwaitingKeyframe.exchange(0, std::memory_order_relaxed);
    const uint64_t queueWaitUs = m_perfQueueWaitUs.exchange(0, std::memory_order_relaxed);
    const uint64_t workUs = m_perfWorkUs.exchange(0, std::memory_order_relaxed);
    const uint64_t sendAwaitUs = m_perfSendAwaitUs.exchange(0, std::memory_order_relaxed);
    const uint32_t queuePeak = m_perfMaxQueueDepth.exchange(
        m_inFlightSendFrames.load(std::memory_order_relaxed),
        std::memory_order_relaxed
    );

    const double ingressFps = static_cast<double>(ingressFrames) / windowSeconds;
    const double queuedFps = static_cast<double>(queuedFrames) / windowSeconds;
    const double processedFps = static_cast<double>(processedFrames) / windowSeconds;
    const double sentFps = static_cast<double>(sentFrames) / windowSeconds;
    const double avgQueueWaitMs = processedFrames > 0
        ? static_cast<double>(queueWaitUs) / static_cast<double>(processedFrames) / 1000.0
        : 0.0;
    const double avgWorkMs = processedFrames > 0
        ? static_cast<double>(workUs) / static_cast<double>(processedFrames) / 1000.0
        : 0.0;
    const double avgSendAwaitMs = processedFrames > 0
        ? static_cast<double>(sendAwaitUs) / static_cast<double>(processedFrames) / 1000.0
        : 0.0;
    const double avgIngressKb = ingressFrames > 0
        ? static_cast<double>(ingressBytes) / static_cast<double>(ingressFrames) / 1024.0
        : 0.0;
    const double avgProcessedKb = processedFrames > 0
        ? static_cast<double>(processedBytes) / static_cast<double>(processedFrames) / 1024.0
        : 0.0;
    const double avgNalsPerSentFrame = sentFrames > 0
        ? static_cast<double>(sentNals) / static_cast<double>(sentFrames)
        : 0.0;

    Debug::Log(
        "NetworkCameraModule perf ({:.1f}s): in={:.1f}fps/{:.1f}KB, queued={:.1f}fps, processed={:.1f}fps/{:.1f}KB, sent={:.1f}fps avg_nals={:.2f}, drops(backpressure={}, inactive={}, wait_keyframe={}), avg_queue_ms={:.2f}, avg_work_ms={:.2f}, avg_send_wait_ms={:.2f}, queue_peak={}, queue_now={}",
        windowSeconds,
        ingressFps,
        avgIngressKb,
        queuedFps,
        processedFps,
        avgProcessedKb,
        sentFps,
        avgNalsPerSentFrame,
        droppedBackpressure,
        droppedInactive,
        droppedAwaitingKeyframe,
        avgQueueWaitMs,
        avgWorkMs,
        avgSendAwaitMs,
        queuePeak,
        m_inFlightSendFrames.load(std::memory_order_relaxed)
    );
}

asio::awaitable<void> NetworkCameraModule::StartStream(const size_t requestID, const std::string cameraID, const CameraFormat requestedFormat) {
    Debug::Log(
        "NetworkCameraModule: StartStream requestId={}, cameraId={}, {}x{}@{}",
        requestID,
        cameraID,
        requestedFormat.width,
        requestedFormat.height,
        requestedFormat.framerate
    );

    {
        asio::steady_timer timer(m_context);
        int attempts = 0;
        while (m_portNumber.load() == 0) {
            if (++attempts > 3000) { // ~30 seconds at 10ms
                Debug::LogError("SRTP port info not received in time");
                ConnectionManager::SendRequestResponse(requestID, PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM_RESPONSE, StreamStartFailReason::IncorrectConfig);
                ProcessError(ModuleFailReason::Timeout);
                co_return;
            }
            timer.expires_after(asio::chrono::milliseconds(10));
            co_await timer.async_wait();
        }
    }

    ResetPerformanceStats();
    Debug::Log("NetworkCameraModule: perf telemetry enabled (5s window)");

#ifdef ANDROID_DEVICE
    StartStream_Android(requestID, cameraID, requestedFormat);
#else
    StartStream_IOS(requestID, cameraID, requestedFormat);
#endif

    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, true);
}

bool NetworkCameraModule::TryReserveFrameSlot(const char* sourceTag) {
    uint32_t current = m_inFlightSendFrames.load(std::memory_order_relaxed);
    while (true) {
        if (current >= kMaxQueuedFrameJobs) {
            m_waitForKeyframeAfterDrop.store(true, std::memory_order_relaxed);
            const uint32_t dropped = m_droppedFramesBackpressure.fetch_add(1, std::memory_order_relaxed) + 1;
            NoteDropBackpressure();
            if ((dropped % kBackpressureLogEvery) == 1) {
                Debug::LogWarning(
                    "NetworkCameraModule: Dropping frame due to backpressure (source={}, queued={}, dropped={})",
                    sourceTag ? sourceTag : "unknown",
                    current,
                    dropped
                );
            }
            return false;
        }

        if (m_inFlightSendFrames.compare_exchange_weak(
            current,
            current + 1,
            std::memory_order_acq_rel,
            std::memory_order_relaxed
        )) {
            NoteFrameQueued(current + 1);
            return true;
        }
    }
}

void NetworkCameraModule::ReleaseFrameSlot() {
    const uint32_t previous = m_inFlightSendFrames.fetch_sub(1, std::memory_order_acq_rel);
    if (previous == 0) {
        m_inFlightSendFrames.store(0, std::memory_order_relaxed);
        Debug::LogWarning("NetworkCameraModule: ReleaseFrameSlot underflow");
    }
}

std::shared_ptr<SRTP::Stream> NetworkCameraModule::GetVideoStream() const {
    std::lock_guard<std::mutex> lock(m_videoStreamMutex);
    return m_videoStream;
}

void NetworkCameraModule::SetVideoStream(std::shared_ptr<SRTP::Stream> stream) {
    std::lock_guard<std::mutex> lock(m_videoStreamMutex);
    m_videoStream = std::move(stream);
}

std::shared_ptr<SRTP::Stream> NetworkCameraModule::ClearVideoStream() {
    std::lock_guard<std::mutex> lock(m_videoStreamMutex);
    std::shared_ptr<SRTP::Stream> stream = std::move(m_videoStream);
    m_videoStream.reset();
    return stream;
}

void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_REMOTE_KEY");
        m_peerModuleEnabled.store(false);
        m_portNumber.store(0);
        m_localKey = SRTP::Stream::GenerateKey();

        const size_t requestID = package->GetValue<size_t>();
        package->GetValue(m_remoteKey);

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY_RESPONSE,
            std::vector(m_localKey)
        );

        Enable();
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST , [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_CAMERAS_SPECIFICATION_LIST");
        const size_t requestID = package->GetValue<size_t>();
        const AVCodec* codec = GetEncoderCodec(CodecID::H264);
        std::vector<CameraSpecification> specifications = FetchCamerasSpecificationForCodec(codec);
        if (specifications.empty()) {
            Debug::LogWarning("Encoder-filtered camera format list is empty, falling back to raw camera formats");
#ifdef ANDROID_DEVICE
            specifications = FetchCamerasSpecificationForCodec(nullptr);
#else
            specifications = FetchCamerasSpecification();
#endif
        }

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST_RESPONSE,
            std::move(specifications)
        );
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_PORT_INFO, [instance, this](PC_Package&& package) mutable {
        const uint16_t port = package->GetValue<uint16_t>();
        Debug::Log("Received SRTP port info: {}", port);
        m_portNumber.store(port);
        if (m_portNumber.load() == 0) {
            Disable();
        }
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_START_STREAM");
        const size_t requestID = package->GetValue<size_t>();
        const std::string deviceID = package->GetValue<std::string>();
        const CameraFormat format = package->GetValue<CameraFormat>();

        asio::co_spawn(m_context, StartStream(requestID,deviceID, format), asio::detached);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: REQUEST_STOP_STREAM");
        m_peerModuleEnabled.store(false);
        ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, false);
        Disable(true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE, [instance, this](PC_Package&& package) mutable {
        Debug::Log("NetworkCameraModule: ENABLE");
        if (GetModuleState() == ModuleState::Enabled) {
            ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, true);
            return;
        }
        Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, [instance, this](PC_Package&& package) mutable {
       m_peerModuleEnabled.store(package->GetValue<bool>());
    });
}

void NetworkCameraModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_PORT_INFO);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED);
}

void NetworkCameraModule::OnInitialize() {
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_peerModuleEnabled.store(false);
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);

    m_droppedFramesBackpressure.store(0, std::memory_order_relaxed);
    m_waitForKeyframeAfterDrop.store(false, std::memory_order_relaxed);
    ResetPerformanceStats();

#ifdef ANDROID_DEVICE
    if (!co_await PermissionManager::RequestDisablingBatteryOptimizations()) {
        Debug::LogWarning("NetworkCameraModule: Battery optimization is still enabled; background/screen-off streaming reliability may be reduced");
    }
#endif

    if (!co_await PermissionManager::RequestCameraAccessPermission()) {
        Disable();
        co_return;
    }

    if (ShouldAbortEnable()) {
        co_return;
    }

#ifdef ANDROID_DEVICE
    UpdateMainServiceCameraRequest(true);
#endif

    Debug::Log("NetworkCameraModule: Enabled, waiting for SRTP port, camera permission granted");

    asio::steady_timer timer(m_context);
    while (!m_peerModuleEnabled.load()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(10));
        co_await timer.async_wait();
    }
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_peerModuleEnabled.store(false);
    m_portNumber.store(0);
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, false);

    m_streamActive.store(false);
    m_streamGeneration.fetch_add(1);

    if (const std::shared_ptr<SRTP::Stream> stream = ClearVideoStream()) {
        stream->Close();
    }

    m_h264ParameterSets.clear();
    m_h264LengthSize = 4;
    m_codecConfigSent = false;
    m_streamCodecId = CodecID::H264;
    m_droppedFramesBackpressure.store(0, std::memory_order_relaxed);
    m_waitForKeyframeAfterDrop.store(false, std::memory_order_relaxed);
    ResetPerformanceStats();

#ifdef ANDROID_DEVICE
    StopStream_Android();
#else
    co_await StopStream_IOS();
#endif

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    co_await OnDisable();
    co_return;
}

const char* NetworkCameraModule::GetModuleName() const {
    return "NetworkCameraModule";
}

ModuleType NetworkCameraModule::GetModuleType() const {
    return ModuleType::NetworkCamera;
}
