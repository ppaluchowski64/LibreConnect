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
    id[m_deviceID.size()] = '\0';

    constexpr VMicFormat format{
        .sampleRate = 48000,
        .bitDepth = 16,
        .channels = 2
    };

    const VMicResult result = VMic_OpenDevice(&m_handle, id, &format);
    if (result != VMIC_SUCCESS) {
        Debug::LogError("NetworkMicrophoneModule: Failed to open virtual microphone device: {}", static_cast<int>(result));
        throw std::runtime_error("Failed to open virtual microphone device");
    }

    m_audioStream = std::make_shared<SRTP::Stream>(
        m_context,
        m_localKey,
        m_remoteKey,
        48000
    );

    const UDPEndpoint endpoint = m_audioStream->Bind();
    Debug::Log("NetworkMicrophoneModule: SRTP stream bound to port {}", endpoint.port());

    ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_START_STREAM, endpoint.port());
    co_return;
}

asio::awaitable<void> NetworkMicrophoneModule::StartStream() {
    const VMicResult result = VMic_StartStream(m_handle);
    if (result != VMIC_SUCCESS) {
        Debug::LogError("NetworkMicrophoneModule: Failed to start virtual microphone stream: {}", static_cast<int>(result));
        co_return;
    }

    std::shared_ptr<NetworkMicrophoneModule> instance = std::dynamic_pointer_cast<NetworkMicrophoneModule>(shared_from_this());
    asio::co_spawn(m_context, [instance]() -> asio::awaitable<void> {
        std::vector<uint8_t> payload;
        try {
            asio::steady_timer timer(instance->m_context.get_executor());
            while (instance->GetModuleState() == ModuleState::Enabling) {
                timer.expires_after(std::chrono::milliseconds(10));
                co_await timer.async_wait();
            }

            while (instance->GetModuleState() == ModuleState::Enabled) {
                co_await instance->m_audioStream->AsyncReceive(payload);

                if (payload.empty()) continue;

                // TODO: Integrate Opus decoder here.

                const uint32_t framesCount = static_cast<uint32_t>(payload.size() / 4);
                VMic_PushSamples(instance->m_handle, payload.data(), framesCount);
            }
        } catch (const std::exception& e) {
            Debug::LogError("NetworkMicrophoneModule: Stream loop error: {}", e.what());
        }

        Debug::Log("NetworkMicrophoneModule: Stream loop stopped");
    }, asio::detached);
}

void NetworkMicrophoneModule::EnableResponseCallbacks() {
    std::shared_ptr<NetworkMicrophoneModule> instance = std::dynamic_pointer_cast<NetworkMicrophoneModule>(shared_from_this());
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("NetworkMicrophoneModule: Received enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            Debug::Log("NetworkMicrophoneModule: Already enabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_STATE_CHANGED, true);
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
    const VMicResult result = VMic_Initialize();
    if (result != VMIC_SUCCESS && result != VMIC_ERROR_DEVICE_ALREADY_INITIALIZED) {
        const std::unique_ptr<QEvent> event = std::make_unique<VirtualMicrophoneErrorEvent>(result);
        ConnectionManager::SendEvent(event);
    }
}

asio::awaitable<void> NetworkMicrophoneModule::OnEnable() {
    m_localKey = SRTP::Stream::GenerateKey();
    
    {
        const std::optional<PC_Package> response = co_await ConnectionManager::SendRequest(
            PC_PackageType::NETWORK_MICROPHONE_MODULE_REMOTE_KEY_REQUEST, 
            m_localKey
        );
        
        if (!response.has_value()) {
            Debug::LogError("NetworkMicrophoneModule: Failed to exchange keys with peer");
            co_return;
        }

        response.value()->GetValue(m_remoteKey);
    }
    ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_ENABLE);

    try {
        co_await InitializeStream();
        co_await StartStream();
    } catch (const std::exception& e) {
        Debug::LogError("NetworkMicrophoneModule: Failed to enable module: {}", e.what());
    }

    co_return;
}

asio::awaitable<void> NetworkMicrophoneModule::OnDisable() {
    ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_DISABLE);

    if (m_handle) {
        VMic_StopStream(m_handle);
        VMic_Close(m_handle);
        m_handle = nullptr;
    }

    if (m_audioStream) {
        m_audioStream->Close();
        m_audioStream.reset();
    }

    ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_STATE_CHANGED, false);
    co_return;
}

asio::awaitable<void> NetworkMicrophoneModule::OnShutdown() {
    co_await OnDisable();
    VMic_Shutdown();
    co_return;
}

const char* NetworkMicrophoneModule::GetModuleName() const {
    return "NetworkMicrophoneModule";
}

ModuleType NetworkMicrophoneModule::GetModuleType() const {
    return ModuleType::NetworkMicrophone;
}
