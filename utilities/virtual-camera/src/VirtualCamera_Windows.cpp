#ifdef _WIN32

#include <VirtualCamera.h>
#include <softcam/softcam.h>
#include <DebugLog.h>

void VirtualCamera::Flush() const {

}

void VirtualCamera::SetupStream(const int width, const int height, const int fps) {
    DestroyStream();
    m_camera = scCreateCamera(width, height, fps);
    scWaitForConnection(m_camera);
}

void VirtualCamera::DestroyStream() {
    if (m_camera != nullptr && scIsConnected(m_camera)) {
        scDeleteCamera(m_camera);
    }

    m_camera = nullptr;
}

#endif