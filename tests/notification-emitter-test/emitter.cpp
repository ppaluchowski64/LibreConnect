#include <NotificationEmitter.h>
#include <iostream>

int main() {
    const std::vector<NotificationEmitter::ButtonAction> buttons = {
        {L"Action", []() {
            std::cout << "\n\nButton action triggered\n\n" << std::endl;
        }}
    };

    const auto id = NotificationEmitter::Emit(L"hello", L"test content", std::nullopt, std::nullopt, buttons);

    std::wcout << L"Press Enter to remove notification" << std::endl;
    std::cin.get();

    NotificationEmitter::Remove(id);

    std::wcout << L"Press Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}