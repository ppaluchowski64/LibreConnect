#ifdef _WIN32

#include <VirtualCamera.h>
#include <DebugLog.h>
#include <windows.h>
#include <fmt/ostream.h>

static std::string to_string(const std::wstring& utf16) {
    if (utf16.empty())
        return {};

    const int sizeNeeded = WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16.data(),
        static_cast<int>(utf16.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    std::string utf8(sizeNeeded, 0);

    WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16.data(),
        static_cast<int>(utf16.size()),
        utf8.data(),
        sizeNeeded,
        nullptr,
        nullptr
    );

    return utf8;
}

void VirtualCamera::PushFrame(const void* data) const {
    const VCamResult result = PushCamFrame(m_handle, data, m_format);

    if (result != VCAM_SUCCESS) {
        Debug::LogError("Camera push frame result {} - \"{}\"", magic_enum::enum_name(result), to_string(VCamGetLastError(m_handle)));
    }
}

void VirtualCamera::SetupCamera(const std::wstring& name, const int width, const int height, const int fps) {
    m_handle = nullptr;
    const VCamResult result = CreateCam(name.c_str(), width, height, fps, &m_handle);

    if (result != VCAM_SUCCESS) {
        Debug::LogError("Camera setup result {} - \"{}\"", magic_enum::enum_name(result), to_string(VCamGetLastError(m_handle)));
    }
}

void VirtualCamera::DestroyCamera() {
    const VCamResult result = DestroyCam(m_handle);

    if (result != VCAM_SUCCESS) {
        Debug::LogError("Camera stop result {} - \"{}\"", magic_enum::enum_name(result), to_string(VCamGetLastError(m_handle)));
    }

    m_handle = nullptr;
}

#endif