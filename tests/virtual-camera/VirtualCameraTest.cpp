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

    camera.Start(
        L"Example Virtual Camera",
        FrameFormat::RGBA32,
        width,
        height,
        fps
    );

    Debug::Log("camera created");

    std::vector<uint8_t> frame(width * height * 4, 255);
    constexpr auto frameDuration = std::chrono::milliseconds(1000 / fps);

    float phase = 0.0f;
    constexpr float phaseStep = 0.02f;

    for (int i = 0; i < 3000; ++i) {
        const uint8_t r = static_cast<uint8_t>(127 + 127 * std::sin(phase));
        const uint8_t g = static_cast<uint8_t>(127 + 127 * std::sin(phase + 2.0f));
        const uint8_t b = static_cast<uint8_t>(127 + 127 * std::sin(phase + 4.0f));

        for (size_t p = 0; p < frame.size(); p += 4) {
            frame[p + 0] = r;
            frame[p + 1] = g;
            frame[p + 2] = b;
            frame[p + 3] = 255;
        }

        camera.PushFrame(frame.data());

        phase += phaseStep;
        std::this_thread::sleep_for(frameDuration);
    }

    camera.Stop();
    return 0;
}
