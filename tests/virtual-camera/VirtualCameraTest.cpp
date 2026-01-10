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

    if (!camera.Start("Example Virtual Camera", VCAM_FORMAT_NV12, width, height, fps)) {
        return -1;
    }

    Debug::Log("camera created");

    constexpr size_t frameSize = width * height * 3 / 2;
    std::vector<uint8_t> frame(frameSize);

    uint8_t* Y  = frame.data();
    uint8_t* UV = Y + width * height;

    constexpr auto frameDuration = std::chrono::milliseconds(1000 / fps);

    float phase = 0.0f;
    constexpr float phaseStep = 0.02f;

    for (int i = 0; i < 50000; ++i) {
        const uint8_t yVal =
            static_cast<uint8_t>(127 + 60 * std::sin(phase));
        const uint8_t uVal =
            static_cast<uint8_t>(128 + 40 * std::sin(phase + 2.0f));
        const uint8_t vVal =
            static_cast<uint8_t>(128 + 40 * std::sin(phase + 4.0f));

        std::fill(Y, Y + width * height, yVal);

        const int chromaWidth  = width / 2;
        const int chromaHeight = height / 2;

        for (int y = 0; y < chromaHeight; ++y) {
            for (int x = 0; x < chromaWidth; ++x) {
                const size_t idx = (y * chromaWidth + x) * 2;
                UV[idx + 0] = uVal;
                UV[idx + 1] = vVal;
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
