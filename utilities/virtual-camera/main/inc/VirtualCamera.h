#ifndef VIRTUAL_CAMERA_H
#define VIRTUAL_CAMERA_H

#include <VCamAPI.h>
#include <string>
#include <atomic>
#include <boost/uuid/uuid.hpp>

class VirtualCamera final {
public:
    VirtualCamera();
    VirtualCamera(const VirtualCamera&) = delete;
    VirtualCamera& operator=(const VirtualCamera&) = delete;

    ~VirtualCamera();

    bool Start(const std::string& name, VCamFormat format, int width, int height, int fps);
    bool Stop();
    bool PushFrame(const void* data) const;

private:
    bool SetupCamera(const std::string& name, int width, int height, int fps);
    bool DestroyCamera();

    boost::uuids::uuid m_cameraID;
    VCamFormat m_format;
    VCamHandle m_handle;
    std::atomic<bool> m_active{false};
};

#endif //VIRTUAL_CAMERA_H
