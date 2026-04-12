#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <PermissionManager.h>

#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <memory>
#include <mutex>
#include <set>

#include <asio.hpp>
#include <asio/co_spawn.hpp>

constexpr uint32_t kMaxQueuedFrameJobs = 8;
constexpr uint32_t kBackpressureLogEvery = 120;

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

    if (m_videoStream) {
        m_videoStream->Close();
        m_videoStream.reset();
    }

    m_h264ParameterSets.clear();
    m_h264LengthSize = 4;
    m_codecConfigSent = false;
    m_streamCodecId = CodecID::H264;
    m_droppedFramesBackpressure.store(0, std::memory_order_relaxed);
    m_waitForKeyframeAfterDrop.store(false, std::memory_order_relaxed);

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
