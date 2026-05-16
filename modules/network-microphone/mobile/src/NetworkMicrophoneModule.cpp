#include <NetworkMicrophoneModule.h>
#include <DebugLog.h>
#include <ConnectionManager.h>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace {
    void SetMicrophoneCaptureEnabled(const bool enabled) {
#ifdef Q_OS_ANDROID
        const QJniObject receiver = QJniObject::getStaticObjectField(
            "com/LibreConnect/mobile/MicrophoneReceiver",
            "INSTANCE",
            "Lcom/LibreConnect/mobile/MicrophoneReceiver;"
        );

        if (!receiver.isValid()) {
            Debug::LogError("NetworkMicrophoneModule: Failed to get MicrophoneReceiver instance");
            return;
        }

        if (enabled) {
            receiver.callMethod<jboolean>("start", "(Lkotlin/jvm/functions/Function1;)Z", nullptr);
        } else {
            receiver.callMethod<void>("stop");
        }
#endif
    }
}

void NetworkMicrophoneModule::ProcessAndSendAudio(const std::vector<uint8_t>& pcm) const {
    if (m_audioStream && GetModuleState() == ModuleState::Enabled) {
        // TODO: Implement Opus encoding
        m_audioStream->Send(pcm.data(), pcm.size());
    }
}

asio::awaitable<void> NetworkMicrophoneModule::InitializeStream(uint16_t port) {
    m_audioStream = std::make_shared<SRTP::Stream>(
        m_context,
        m_localKey,
        m_remoteKey,
        48000
    );

    const IPAddress peerAddress = ConnectionManager::GetPeerAddress();
    m_audioStream->Bind({peerAddress, port});
    
    Debug::Log("NetworkMicrophoneModule: SRTP stream initialized to {}:{}", peerAddress.to_string(), port);
    co_return;
}

void NetworkMicrophoneModule::EnableResponseCallbacks() {
    std::shared_ptr<NetworkMicrophoneModule> instance = std::dynamic_pointer_cast<NetworkMicrophoneModule>(shared_from_this());
    
    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        instance->Enable(true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_DISABLE, [instance](PC_Package&& package) mutable {
        instance->Disable(true);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_START_STREAM, [instance](PC_Package&& package) mutable {
        const uint16_t port = package->GetValue<uint16_t>();
        asio::co_spawn(instance->m_context, instance->InitializeStream(port), asio::detached);
    });

    ConnectionManager::AddResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_REMOTE_KEY_REQUEST, [instance](PC_Package&& package) mutable {
        instance->m_remoteKey = package->GetValue<std::vector<uint8_t>>();
        instance->m_localKey = SRTP::Stream::GenerateKey();
        ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_REMOTE_KEY_REQUEST, instance->m_localKey);
    });
}

void NetworkMicrophoneModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_START_STREAM);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::NETWORK_MICROPHONE_MODULE_REMOTE_KEY_REQUEST);
}

void NetworkMicrophoneModule::OnInitialize() {}

asio::awaitable<void> NetworkMicrophoneModule::OnEnable() {
    SetMicrophoneCaptureEnabled(true);
    ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_STATE_CHANGED, true);
    co_return;
}

asio::awaitable<void> NetworkMicrophoneModule::OnDisable() {
    SetMicrophoneCaptureEnabled(false);
    if (m_audioStream) {
        m_audioStream->Close();
        m_audioStream.reset();
    }
    ConnectionManager::Send(PC_PackageType::NETWORK_MICROPHONE_MODULE_STATE_CHANGED, false);
    co_return;
}

asio::awaitable<void> NetworkMicrophoneModule::OnShutdown() {
    co_await OnDisable();
    co_return;
}

const char* NetworkMicrophoneModule::GetModuleName() const {
    return "NetworkMicrophoneModule";
}

ModuleType NetworkMicrophoneModule::GetModuleType() const {
    return ModuleType::NetworkMicrophone;
}
