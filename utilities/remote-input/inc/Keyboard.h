#ifndef KEYBOARD_H
#define KEYBOARD_H

#ifdef __APPLE__
    #include <asio.hpp>
    #include <asio/awaitable.hpp>
    #include <concurrentqueue.h>
    #include <AwaitableFlag.h>
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

        void PressKey(int keyCode);
        void ReleaseKey(int keyCode);
        void PressAndReleaseKey(int keyCode);

    #ifdef __APPLE__
    private:
        moodycamel::ConcurrentQueue<KeyEvent> m_eventQueue;
        AwaitableFlag m_eventFlag;
        std::atomic<bool> m_isRunning;

        asio::awaitable<void> CoProcessEvents();
    #endif
};

#endif // KEYBOARD_H
