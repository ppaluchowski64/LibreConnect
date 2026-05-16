#include <NetworkMicrophoneModule.h>
#include <VMicAPI.h>
#include <VMicTypes.h>
#include <NetworkMicrophoneEvents.h>
#include <MicrophoneTypes.h>

void NetworkMicrophoneModule::CreateAudioDevice(const std::string& name) {
    std::shared_ptr<NetworkMicrophoneModule> instance = std::dynamic_pointer_cast<NetworkMicrophoneModule>(shared_from_this());
    asio::post(m_moduleStrand, [instance, name]() {
        constexpr int bufferSize = 48000;
        char id[256];

        const VMicResult result = VMic_CreateDevice(name.c_str(), id, bufferSize);
        if (result != VMIC_SUCCESS) {
            const std::unique_ptr<QEvent> event = std::make_unique<VirtualMicrophoneErrorEvent>(result);
            ConnectionManager::SendEvent(event);
            return;
        }

        instance->GetAudioDeviceList();
    });
}

void NetworkMicrophoneModule::SelectDevice(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_deviceID = id;
}

void NetworkMicrophoneModule::GetAudioDeviceList() {
    std::shared_ptr<NetworkMicrophoneModule> instance = std::dynamic_pointer_cast<NetworkMicrophoneModule>(shared_from_this());
    asio::post(m_moduleStrand, [instance]() {
        std::vector<VMicDeviceInfo> devices;
        uint32_t count;

        const VMicResult result1 = VMic_GetAvailableDevices(nullptr, &count);
        if (result1 != VMIC_SUCCESS) {
            const std::unique_ptr<QEvent> event = std::make_unique<VirtualMicrophoneErrorEvent>(result1);
            ConnectionManager::SendEvent(event);
        }

        devices.resize(count);
        const VMicResult result2 = VMic_GetAvailableDevices(devices.data(), &count);
        if (result2 != VMIC_SUCCESS) {
            const std::unique_ptr<QEvent> event = std::make_unique<VirtualMicrophoneErrorEvent>(result2);
            ConnectionManager::SendEvent(event);
        }

        std::vector<MicrophoneDevice> resultDevices;
        resultDevices.reserve(count);
        for (const auto& device : devices) {
            resultDevices.push_back(MicrophoneDevice{device.name, device.id});
        }

        const std::unique_ptr<QEvent> event = std::make_unique<AudioDeviceListEvent>(resultDevices);
        ConnectionManager::SendEvent(event);
    });
}

asio::awaitable<void> NetworkMicrophoneModule::InitializeStream() {
    char id[255];
    std::memcpy(id, m_deviceID.c_str(), m_deviceID.size());

    constexpr VMicFormat format{
        .sampleRate = 48000,
        .bitDepth = 16,
        .channels = 2
    };

    const VMicResult result = VMic_OpenDevice(&m_handle, id, &format);
    if (result != VMIC_SUCCESS) {
        // error
    }
}

asio::awaitable<void> NetworkMicrophoneModule::StartStream() {
    const VMicResult result = VMic_StartStream(m_handle);
    if (result != VMIC_SUCCESS) {
        // error
    }

    std::shared_ptr<NetworkMicrophoneModule> instance = std::dynamic_pointer_cast<NetworkMicrophoneModule>(shared_from_this());
    asio::co_spawn(m_context, [instance]() -> asio::awaitable<void> {
        while (instance->GetModuleState() == ModuleState::Enabled || instance->GetModuleState() == ModuleState::Enabling) {

        }
    }, asio::detached);
}

void NetworkMicrophoneModule::EnableResponseCallbacks() {
    std::shared_ptr<NetworkMicrophoneModule> instance = std::dynamic_pointer_cast<NetworkMicrophoneModule>(shared_from_this());
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("NetworkMicrophoneModule: Received enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            Debug::Log("NetworkMicrophoneModule: Already enabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_STATE_CHANGED, true);
            return;
        }
        instance->Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_DISABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("NetworkMicrophoneModule: Received disable request");
        if (instance->GetModuleState() == ModuleState::Disabled) {
            Debug::Log("NetworkMicrophoneModule: Already disabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_STATE_CHANGED, false);
            return;
        }
        instance->Disable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_STATE_CHANGED, [instance](PC_Package&& package) mutable {
        const bool peerEnabled = package->GetValue<bool>();
        Debug::Log("NetworkMicrophoneModule: Peer module state changed: {}", peerEnabled);
        instance->m_peerModuleEnabled.store(peerEnabled);
    });
}

void NetworkMicrophoneModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_STATE_CHANGED);
}

void NetworkMicrophoneModule::OnInitialize() {

}

asio::awaitable<void> NetworkMicrophoneModule::OnEnable() {
    ConnectionManager::Send(PC_PackageType::NETWORK_CAMERA_MODULE_ENABLE);

    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(PC_PackageType::NETWORK_MICROPHONE_MODULE_REMOTE_KEY_REQUEST, std::string(m_localKey));
        if (!response.has_value()) {
            co_return;
        }

        response.value()->GetValue(m_remoteKey);
    }

    co_await InitializeStream();
    ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_START_STREAM);
    co_await StartStream();

    co_return;
}

asio::awaitable<void> NetworkMicrophoneModule::OnDisable() {
    co_return;
}

asio::awaitable<void> NetworkMicrophoneModule::OnShutdown() {
    co_return;
}

const char* NetworkMicrophoneModule::GetModuleName() const {
    return "NetworkMicrophoneModule";
}

ModuleType NetworkMicrophoneModule::GetModuleType() const {
    return ModuleType::NetworkMicrophone;
}
