#include <VirtualCamera.h>
#include <DebugLog.h>
#include <boost/uuid/random_generator.hpp>

VirtualCamera::VirtualCamera() {
    static thread_local boost::uuids::random_generator generator = boost::uuids::random_generator();
    m_cameraID = generator();
}

void VirtualCamera::Start(const std::string_view name, const FrameFormat format, const int width, const int height, const int fps) {
    if (m_active) {
        Debug::LogWarning("Stream already started");
        return;
    }

    try {
        SetupCamera(name, format, width, height, fps);
        m_active = true;
    } catch (...) {
        Debug::LogError("Failed to start stream");
    }
}

void VirtualCamera::Stop() {
    if (!m_active) {
        Debug::LogWarning("Stream already stopped");
        return;
    }

    try {
        DestroyCamera();
        m_active = false;
    } catch (...) {
        Debug::LogError("Failed to stop stream");
    }
}