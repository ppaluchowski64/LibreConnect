#include <NotificationEmitter.h>
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    const std::vector<NotificationEmitter::ButtonAction> buttons = {
        {L"Action", []() {
            std::cout << "\n\nButton action triggered\n\n" << std::endl;
        }}
    };

    const auto id = NotificationEmitter::Emit(L"hello", L"test content", std::nullopt, std::nullopt, buttons);

#ifdef __APPLE__
    // Finder-launched .app has no stdin; keep process alive for notification interaction.
    std::this_thread::sleep_for(std::chrono::seconds(20));
    NotificationEmitter::Remove(id);
    std::this_thread::sleep_for(std::chrono::seconds(2));
#else
    std::wcout << L"Press Enter to remove notification" << std::endl;
    std::cin.get();

    NotificationEmitter::Remove(id);

    std::wcout << L"Press Enter to exit..." << std::endl;
    std::cin.get();
#endif

    return 0;
}
