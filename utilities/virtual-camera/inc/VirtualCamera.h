#ifndef VIRTUAL_CAMERA_H
#define VIRTUAL_CAMERA_H


#ifdef _WIN32
#include <softcam/softcam.h>
#endif

#include <memory>
#include <boost/uuid/uuid.hpp>

class FrameBuffer {

};

class VirtualCamera final {
public:
    VirtualCamera();
    VirtualCamera(const VirtualCamera&) = delete;
    VirtualCamera& operator=(const VirtualCamera&) = delete;

    void StartStream(int width, int height, int fps);
    void StopStream();
    void SetFrameBuffer(const std::shared_ptr<FrameBuffer>& frameBuffer);
    void Flush() const;

private:
    void SetupStream(int width, int height, int fps);
    void DestroyStream();

    boost::uuids::uuid m_cameraID;
    std::shared_ptr<FrameBuffer> m_frameBuffer{nullptr};
    bool m_active{false};

#ifdef _WIN32
    scCamera m_camera{nullptr};
#endif

};

#endif //VIRTUAL_CAMERA_H
