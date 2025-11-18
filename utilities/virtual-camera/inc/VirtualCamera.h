#ifndef VIRTUAL_CAMERA_H
#define VIRTUAL_CAMERA_H

#include <memory>
#include <boost/uuid/uuid.hpp>

class FrameBuffer;

class VirtualCamera final {
public:
    VirtualCamera();
    VirtualCamera(const VirtualCamera&) = delete;
    VirtualCamera& operator=(const VirtualCamera&) = delete;

    void StartStream();
    void StopStream();
    void SetFrameBuffer(const std::shared_ptr<FrameBuffer>& frameBuffer);
    void Flush() const;

private:
    void SetupStream() const;
    void DestroyStream() const;

    boost::uuids::uuid m_cameraID;
    std::shared_ptr<FrameBuffer> m_frameBuffer{nullptr};
    bool m_active{false};
};

#endif //VIRTUAL_CAMERA_H
