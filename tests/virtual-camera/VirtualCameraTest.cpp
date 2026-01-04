#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <VirtualCamera.h>
#include <DebugLog.h>

int main() {
    VirtualCamera camera;

    constexpr int width  = 1280;
    constexpr int height = 720;
    constexpr int fps    = 30;

    // IMPORTANT: switch to YUYV
    if (!camera.Start("Example Virtual Camera",
                      VCAM_FORMAT_YUYV,
                      width, height, fps)) {
        return -1;
                      }

    Debug::Log("camera created");

    // YUYV = 2 bytes per pixel
    const size_t frameSize = width * height * 2;
    std::vector<uint8_t> frame(frameSize, 0);

    constexpr auto frameDuration =
        std::chrono::milliseconds(1000 / fps);

    float phase = 0.0f;
    constexpr float phaseStep = 0.02f;

    for (int i = 0; i < 50000; ++i) {
        const uint8_t yVal =
            static_cast<uint8_t>(127 + 60 * std::sin(phase));
        const uint8_t uVal =
            static_cast<uint8_t>(128 + 40 * std::sin(phase + 2.0f));
        const uint8_t vVal =
            static_cast<uint8_t>(128 + 40 * std::sin(phase + 4.0f));

        // Fill frame as YUYV
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; x += 2) {
                const size_t index = (y * width + x) * 2;

                frame[index + 0] = yVal; // Y0
                frame[index + 1] = uVal; // U
                frame[index + 2] = yVal; // Y1
                frame[index + 3] = vVal; // V
            }
        }

        camera.PushFrame(frame.data());

        phase += phaseStep;
        std::this_thread::sleep_for(frameDuration);
    }

    camera.Stop();
    Debug::Log("camera destroyed");
    return 0;
}
