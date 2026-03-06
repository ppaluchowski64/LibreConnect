#include <NotificationEmitter.h>
#include <iostream>

int main() {
    const std::vector<std::wstring> buttons = {L"action"};
    const auto id = NotificationEmitter::Emit(L"hello", L"test content", std::nullopt, std::nullopt, buttons);

    std::wcout << L"Press Enter to remove notification" << std::endl;
    std::cin.get();

    NotificationEmitter::Remove(id);

    std::wcout << L"Press Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}