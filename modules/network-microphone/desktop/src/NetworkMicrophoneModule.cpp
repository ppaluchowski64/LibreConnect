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

void NetworkMicrophoneModule::EnableResponseCallbacks() {

}

void NetworkMicrophoneModule::DisableResponseCallbacks() {

}

void NetworkMicrophoneModule::OnInitialize() {

}

asio::awaitable<void> NetworkMicrophoneModule::OnEnable() {
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
