#include <NetworkCameraModule.h>

void NetworkCameraModule::OnInitialize() {
    AddThreads(1);
}

asio::awaitable<void> NetworkCameraModule::OnEnable() {
    m_localKey = SRTP::Stream::GenerateKey();
    m_remoteKey = SRTP::Stream::GenerateKey();
    m_videoStream = std::make_unique<SRTP::Stream>(m_context, m_localKey, m_remoteKey);
}

asio::awaitable<void> NetworkCameraModule::OnDisable() {
    m_videoStream.reset();
}

asio::awaitable<void> NetworkCameraModule::OnShutdown() {

}
