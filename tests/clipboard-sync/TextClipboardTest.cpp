#include "TextClipboard.h"

#include <iostream>

#include <QGuiApplication>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    std::cout << "\n[TEXT CLIPBOARD]\n\n";
    const std::string sampleText = "Hello from Clipboard!";

    if (TextClipboard::Set(sampleText)) {
        std::cout << "Text has been set to clipboard\n";

        if (TextClipboard::Has()) {
            std::cout << "Clipboard contains text\n";

            const std::string clipboardText = TextClipboard::Get();
            std::cout << "Clipboard text: " << clipboardText << '\n';

            if (clipboardText == sampleText)
                std::cout << "TextClipboard test passed\n";
            else
                std::cout << "TextClipboard test failed\n";
        } else {
            std::cout << "Clipboard unexpectedly empty\n";
        }
    } else {
        std::cout << "Failed to set text to clipboard\n";
    }
}
