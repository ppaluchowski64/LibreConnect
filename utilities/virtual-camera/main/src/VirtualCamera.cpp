#include <VirtualCamera.h>
#include <DebugLog.h>
#include <boost/uuid/random_generator.hpp>
#include <magic_enum/magic_enum.hpp>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <codecvt>
    #include <locale>
#endif

VirtualCamera::VirtualCamera() : m_format(VCamFormat::VCAM_FORMAT_RGB32), m_handle(nullptr) {
    static thread_local boost::uuids::random_generator generator = boost::uuids::random_generator();
    m_cameraID = generator();
}

bool VirtualCamera::Start(const std::string& name, const VCamFormat format, const int width, const int height, const int fps) {
    if (m_active) {
        Debug::LogWarning("Stream already started");
        return false;
    }

    m_format = format;

    try {
        const bool result = SetupCamera(name, width, height, fps);
        m_active = result;
        return result;
    } catch (...) {
        Debug::LogError("Failed to start stream");
        return false;
    }
}

bool VirtualCamera::Stop() {
    if (!m_active) {
        Debug::LogWarning("Stream already stopped");
        return false;
    }

    try {
        const bool result = DestroyCamera();
        m_active = false;
        return result;
    } catch (...) {
        Debug::LogError("Failed to stop stream");
        return false;
    }
}

bool VirtualCamera::PushFrame(const void* data) const {
    const VCamResult result = PushCamFrame(m_handle, data, m_format);

    if (result != VCAM_SUCCESS) {
        Debug::LogError("Camera push frame result {} - \"{}\"", magic_enum::enum_name(result), VCamGetLastError(m_handle));
        return false;
    }

    return true;
}

bool VirtualCamera::SetupCamera(const std::string& name, const int width, const int height, const int fps) {
    m_handle = nullptr;
    const VCamResult result = CreateCam(name.c_str(), width, height, fps, m_format, &m_handle);

    if (result != VCAM_SUCCESS) {
        Debug::LogError("Camera setup result {} - \"{}\"", magic_enum::enum_name(result), VCamGetLastError(m_handle));
        return false;
    }

    return true;
}

bool VirtualCamera::DestroyCamera() {
    const VCamResult result = DestroyCam(m_handle);

    if (result != VCAM_SUCCESS) {
        Debug::LogError("Camera stop result {} - \"{}\"", magic_enum::enum_name(result), VCamGetLastError(m_handle));
        return false;
    }

    m_handle = nullptr;
    return true;
}