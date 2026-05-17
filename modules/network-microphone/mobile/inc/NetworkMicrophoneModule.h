#ifndef NETWORK_MICROPHONE_MODULE_H
#define NETWORK_MICROPHONE_MODULE_H

#include <BaseModule.h>
#include <SRTP_Stream.h>
#include <memory>
#include <vector>
#include <atomic>

class NetworkMicrophoneModule : public BaseModule {
public:
    void ProcessAndSendAudio(const std::vector<uint8_t>& pcm) const;

private:
    asio::awaitable<void> InitializeStream(uint16_t port);

    std::vector<uint8_t> m_localKey;
    std::vector<uint8_t> m_remoteKey;
    
    std::shared_ptr<SRTP::Stream> m_audioStream;

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
