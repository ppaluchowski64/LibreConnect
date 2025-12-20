#ifndef VIRTUAL_CAMERA_H
#define VIRTUAL_CAMERA_H

#include <memory>
#include <boost/uuid/uuid.hpp>

enum class FrameFormat : uint8_t {
    RGBA32,
    NV12
};

class VirtualCamera final {
public:
    VirtualCamera();
    VirtualCamera(const VirtualCamera&) = delete;
    VirtualCamera& operator=(const VirtualCamera&) = delete;

    void Start(std::string_view name, FrameFormat format, int width, int height, int fps);
    void Stop();
    void PushFrame(const void* data) const;

private:
    void SetupCamera(std::string_view name, FrameFormat format, int width, int height, int fps);
    void DestroyCamera();

    boost::uuids::uuid m_cameraID;
    bool m_active{false};

};

#endif //VIRTUAL_CAMERA_H
