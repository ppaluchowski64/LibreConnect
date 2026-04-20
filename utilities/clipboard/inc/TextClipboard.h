#ifndef TEXT_CLIPBOARD_H
#define TEXT_CLIPBOARD_H

#include <functional>
#include <string>

class TextClipboard {
    public:
        static bool Set(const std::string& text);
        static std::string Get();
        static bool Has();
        static void AddClipboardUpdateListener(std::function<void()>&& callback);
        static void RemoveClipboardUpdateListener();

    private:
    #ifdef __linux__
        static bool IsWayland();
        static bool HasWlClipboard();
    #endif
};

#endif // TEXT_CLIPBOARD_H
