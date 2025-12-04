#include <VirtualCamera.h>
#include <chrono>
#include <thread>

#include "VirtualCamera.h"

int main() {
    VirtualCamera camera1;
    const auto frameBuffer = std::make_shared<FrameBuffer>();

    camera1.SetFrameBuffer(frameBuffer);
    camera1.StartStream(640, 360, 30);

    VirtualCamera camera2;
    camera2.SetFrameBuffer(frameBuffer);
    camera2.StartStream(640, 360, 30);

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
