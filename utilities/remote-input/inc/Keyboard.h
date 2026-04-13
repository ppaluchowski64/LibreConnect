#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <cstdint>

#ifdef __APPLE__
    #include <AwaitableFlag.h>

    #include <asio.hpp>
    #include <asio/awaitable.hpp>
    #include <concurrentqueue.h>

    #include <atomic>

    struct KeyEvent {
        int keyCode;
        bool isPress;
    };
#endif

enum class Key : uint8_t {
    Unknown = 0,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    Grave,        // ` ~
    Minus,        // - _
    Equal,        // = +
    LeftBracket,  // [ {
    RightBracket, // ] }
    Backslash,    // \ |
    Semicolon,    // ; :
    Apostrophe,   // ' "
    Comma,        // , <
    Period,       // . >
    Slash,        // / ?

    Escape,
    Tab,
    CapsLock,
    Space,
    Backspace,
    Enter,

    LeftShift, RightShift,
    LeftControl, RightControl,
    LeftAlt, RightAlt,
    LeftSuper, RightSuper, // Win / Cmd / Meta
    Menu,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

    PrintScreen,
    ScrollLock,
    Pause,

    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,

    Up,
    Down,
    Left,
    Right,

    NumLock,
    Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadAdd,
    NumpadSubtract,
    NumpadMultiply,
    NumpadDivide,
    NumpadDecimal,
    NumpadEnter
};

class Keyboard {
    public:
        Keyboard();
        ~Keyboard();

        void PressKey(Key key);
        void ReleaseKey(Key key);
        void PressAndReleaseKey(Key key);

    #ifdef __APPLE__
    private:
        moodycamel::ConcurrentQueue<KeyEvent> m_eventQueue;
        AwaitableFlag m_eventFlag;
        std::atomic<bool> m_isRunning;

        asio::awaitable<void> CoProcessEvents();
    #endif
};

#endif // KEYBOARD_H
