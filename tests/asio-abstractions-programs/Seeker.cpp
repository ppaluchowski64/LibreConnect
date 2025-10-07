#include <ConnectionManager.h>

int main(){
    ConnectionManager::AddResponseHandler(PC_PackageType::MESSAGE, [](std::unique_ptr<Package<PC_PackageType>>&& package) {
        std::string message;
        package->GetValue<std::string>(message);
        Debug::Log("[Seeker] Message: {}", message);
    });

    ConnectionManager::Seek(TCPEndpoint(asio::ip::make_address_v4("127.0.0.1"), 9000), [](const bool result) {
        Debug::Log("Seek result: {}", result ? "Success" : "Failure");
        ConnectionManager::Send(PC_PackageType::MESSAGE, std::string("hello from seeker"));
    });

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}