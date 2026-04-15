#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "InputTypes.h"

#ifdef __linux__

    #include <sys/types.h>

#elif __APPLE__

    #include <AwaitableFlag.h>

    #include <CoreGraphics/CoreGraphics.h>

    #include <asio.hpp>
    #include <asio/awaitable.hpp>
    #include <concurrentqueue.h>

    #include <atomic>

    struct KeyEvent {
        int keyCode;
        bool isPress;
    };

#endif

class Keyboard {
    public:
        Keyboard();
        ~Keyboard();

        void PressKey(Key key);
        void ReleaseKey(Key key);
        void PressAndReleaseKey(Key key);

    private:
    #ifdef __linux__

        int m_uinput_fd = -1;

        [[nodiscard]] ssize_t SendKeyEvent(int keyCode, int value) const;

    #elif __APPLE__

        CGEventSourceRef m_source = nullptr;

        moodycamel::ConcurrentQueue<KeyEvent> m_eventQueue;
        AwaitableFlag m_eventFlag;
        std::atomic<bool> m_isRunning;

        asio::awaitable<void> CoProcessEvents();

    #endif
};

#endif // KEYBOARD_H
