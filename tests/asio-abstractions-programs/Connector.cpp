#include <ConnectionManager.h>

int main(){
    ConnectionManager::Connect(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), 9000), [](const bool result) {
        Debug::Log("Seek result: {}", result ? "Success" : "Failure");
    });

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}