#include <Scanner.h>
#include <thread>
#include <ConnectionManager.h>

int main() {
    LanDeviceScanner::BeginScan();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}