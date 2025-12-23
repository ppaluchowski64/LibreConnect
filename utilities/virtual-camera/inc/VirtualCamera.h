#ifndef VIRTUAL_CAMERA_H
#define VIRTUAL_CAMERA_H

#ifdef WIN32
#include <VCamAPI.h>
#endif

#include <boost/uuid/uuid.hpp>
#include <magic_enum/magic_enum.hpp>

enum class FrameFormat : uint8_t {
    RGBA32,
    NV12
};

class VirtualCamera final {
public:
    VirtualCamera();
    VirtualCamera(const VirtualCamera&) = delete;
    VirtualCamera& operator=(const VirtualCamera&) = delete;

    void Start(const std::wstring& name, FrameFormat format, int width, int height, int fps);
    void Stop();
    void PushFrame(const void* data) const;

private:
    void SetupCamera(const std::wstring& name, int width, int height, int fps);
    void DestroyCamera();

    boost::uuids::uuid m_cameraID;
    FrameFormat m_format;
    bool m_active{false};

#ifdef WIN32
    VCamHandle m_handle;
#endif


};

#endif //VIRTUAL_CAMERA_H
