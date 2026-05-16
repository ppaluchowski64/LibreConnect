#ifndef NETWORK_MICROPHONE_MODULE_H
#define NETWORK_MICROPHONE_MODULE_H

#include <BaseModule.h>
#include <VMicTypes.h>

class NetworkMicrophoneModule : public BaseModule {
public:
    void CreateAudioDevice(const std::string& name);
    void SelectDevice(const std::string& id);
    void GetAudioDeviceList();

private:
    asio::awaitable<void> InitializeStream();
    asio::awaitable<void> StartStream();

    std::mutex m_mutex;

    std::string m_deviceID;
    std::string m_localKey;
    std::string m_remoteKey;
    VMicHandle m_handle{};

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

#endif //NETWORK_MICROPHONE_MODULE_H
