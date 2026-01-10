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

    if (!camera.Start("Example Virtual Camera", VCAM_FORMAT_RGB32, width, height, fps)) {
        return -1;
    }

    Debug::Log("camera created");

    constexpr size_t frameSize = width * height * 4;
    std::vector<uint8_t> frame(frameSize);

    constexpr auto frameDuration =
        std::chrono::milliseconds(1000 / fps);

    float phase = 0.0f;
    constexpr float phaseStep = 0.02f;

    for (int i = 0; i < 50000; ++i) {

        const uint8_t r =
            static_cast<uint8_t>(127 + 60 * std::sin(phase));
        const uint8_t g =
            static_cast<uint8_t>(127 + 60 * std::sin(phase + 2.0f));
        const uint8_t b =
            static_cast<uint8_t>(127 + 60 * std::sin(phase + 4.0f));

        uint8_t* p = frame.data();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                p[0] = b;
                p[1] = g;
                p[2] = r;
                p[3] = 255;
                p += 4;
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
