#include <VirtualCamera.h>
#include <DebugLog.h>
#include <boost/uuid/random_generator.hpp>

VirtualCamera::VirtualCamera() {
    static thread_local boost::uuids::random_generator generator = boost::uuids::random_generator();
    m_cameraID = generator();
}

void VirtualCamera::StartStream() {
    if (!m_frameBuffer) {
        Debug::LogError("Framebuffer is null");
        return;
    }

    if (m_active) {
        Debug::LogWarning("Stream already started");
        return;
    }

    try {
        SetupStream();
        m_active = true;
    } catch (...) {
        Debug::LogError("Failed to start stream");
    }
}

void VirtualCamera::StopStream() {
    if (!m_active) {
        Debug::LogWarning("Stream already stopped");
        return;
    }

    try {
        DestroyStream();
        m_active = false;
    } catch (...) {
        Debug::LogError("Failed to stop stream");
    }
}

void VirtualCamera::SetFrameBuffer(const std::shared_ptr<FrameBuffer>& frameBuffer) {
    m_frameBuffer = frameBuffer;
}

#ifdef _WIN32

void VirtualCamera::Flush() const {

}

void VirtualCamera::SetupStream() const {

}

void VirtualCamera::DestroyStream() const {

}

#endif


