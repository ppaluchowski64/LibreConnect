#include <NetworkCameraModule.h>
#include <CameraUtilities.h>
#include <magic_enum/magic_enum.hpp>
#include <asio.hpp>
#include <asio/co_spawn.hpp>

std::vector<CameraSpecification> NetworkCameraModule::GetCamerasSpecification() const {
    return m_camerasSpecification;
}

asio::awaitable<void> NetworkCameraModule::UpdateCamerasSpecificationList() {
    constexpr size_t UPDATE_DELAY = 5;

    while (GetModuleState() != ModuleState::Disabled && GetModuleState() != ModuleState::Uninitialized) {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_CAMERAS_SPECIFICATION_LIST);
        if (!response.has_value()) {
            Debug::LogWarning("NetworkCameraModule::UpdateCamerasSpecificationList: No response");
            continue;
        }

        response.value()->GetValue(m_camerasSpecification);

        asio::steady_timer timer(m_context);
        timer.expires_after(std::chrono::seconds(UPDATE_DELAY));
        co_await timer.async_wait(asio::use_awaitable);
    }
}


void NetworkCameraModule::EnableResponseCallbacks() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();

}

void NetworkCameraModule::DisableResponseCallbacks() {

}

void NetworkCameraModule::OnInitialize() {
    AddThreads(1);
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    asio::co_spawn(m_context, UpdateCamerasSpecificationList(), asio::detached);

    m_localKey = SRTP::Stream::GenerateKey();

    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_REMOTE_KEY, std::vector(m_localKey));
        if (!response.has_value()) {
            throw std::runtime_error("No response");
        }

        response.value()->GetValue(m_remoteKey);
    }

    m_videoStream = std::make_unique<SRTP::Stream>(m_context, m_localKey, m_remoteKey);

    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_START_STREAM);
        if (!response.has_value()) {
            throw std::runtime_error("No response");
        }

        const StreamStartFailReason reason = response.value()->GetValue<StreamStartFailReason>();
        if (reason != StreamStartFailReason::None) {
            throw std::runtime_error(fmt::format("Failed to start stream: {}", magic_enum::enum_name(reason)));
        }
    }

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    m_videoStream.reset();

    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_REQUEST_STOP_STREAM);

    co_return;
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {
    const std::shared_ptr<BaseModule> instance = shared_from_this();
    co_return;
}
