#include "Keyboard.h"

#ifdef __linux__
    #include <fcntl.h>
    #include <unistd.h>
    #include <linux/uinput.h>
    #include <linux/input-event-codes.h>

    #include <cstring>
    #include <stdexcept>
#elif __APPLE__
    #include <DebugLog.h>
    #include <ThreadPool.h>

    #include <ApplicationServices/ApplicationServices.h>
    #include <Carbon/Carbon.h>

    #include <asio.hpp>
    #include <concurrentqueue.h>

    #include <chrono>
    #include <system_error>
#elif _WIN32
    #include <Windows.h>
#endif

namespace {
    int GetNativeKey(Key key) {
        switch (key) {
        #ifdef __linux__
            case Key::Unknown: return -1;

            case Key::A: return KEY_A;
            case Key::B: return KEY_B;
            case Key::C: return KEY_C;
            case Key::D: return KEY_D;
            case Key::E: return KEY_E;
            case Key::F: return KEY_F;
            case Key::G: return KEY_G;
            case Key::H: return KEY_H;
            case Key::I: return KEY_I;
            case Key::J: return KEY_J;
            case Key::K: return KEY_K;
            case Key::L: return KEY_L;
            case Key::M: return KEY_M;
            case Key::N: return KEY_N;
            case Key::O: return KEY_O;
            case Key::P: return KEY_P;
            case Key::Q: return KEY_Q;
            case Key::R: return KEY_R;
            case Key::S: return KEY_S;
            case Key::T: return KEY_T;
            case Key::U: return KEY_U;
            case Key::V: return KEY_V;
            case Key::W: return KEY_W;
            case Key::X: return KEY_X;
            case Key::Y: return KEY_Y;
            case Key::Z: return KEY_Z;

            case Key::Num0: return KEY_0;
            case Key::Num1: return KEY_1;
            case Key::Num2: return KEY_2;
            case Key::Num3: return KEY_3;
            case Key::Num4: return KEY_4;
            case Key::Num5: return KEY_5;
            case Key::Num6: return KEY_6;
            case Key::Num7: return KEY_7;
            case Key::Num8: return KEY_8;
            case Key::Num9: return KEY_9;

            case Key::Grave: return KEY_GRAVE;
            case Key::Minus: return KEY_MINUS;
            case Key::Equal: return KEY_EQUAL;
            case Key::LeftBracket: return KEY_LEFTBRACE;
            case Key::RightBracket: return KEY_RIGHTBRACE;
            case Key::Backslash: return KEY_BACKSLASH;
            case Key::Semicolon: return KEY_SEMICOLON;
            case Key::Apostrophe: return KEY_APOSTROPHE;
            case Key::Comma: return KEY_COMMA;
            case Key::Period: return KEY_DOT;
            case Key::Slash: return KEY_SLASH;

            case Key::Escape: return KEY_ESC;
            case Key::Tab: return KEY_TAB;
            case Key::CapsLock: return KEY_CAPSLOCK;
            case Key::Space: return KEY_SPACE;
            case Key::Backspace: return KEY_BACKSPACE;
            case Key::Enter: return KEY_ENTER;

            case Key::LeftShift: return KEY_LEFTSHIFT;
            case Key::RightShift: return KEY_RIGHTSHIFT;
            case Key::LeftControl: return KEY_LEFTCTRL;
            case Key::RightControl: return KEY_RIGHTCTRL;
            case Key::LeftAlt: return KEY_LEFTALT;
            case Key::RightAlt: return KEY_RIGHTALT;
            case Key::LeftSuper: return KEY_LEFTMETA;
            case Key::RightSuper: return KEY_RIGHTMETA;
            case Key::Menu: return KEY_COMPOSE;

            case Key::F1: return KEY_F1;
            case Key::F2: return KEY_F2;
            case Key::F3: return KEY_F3;
            case Key::F4: return KEY_F4;
            case Key::F5: return KEY_F5;
            case Key::F6: return KEY_F6;
            case Key::F7: return KEY_F7;
            case Key::F8: return KEY_F8;
            case Key::F9: return KEY_F9;
            case Key::F10: return KEY_F10;
            case Key::F11: return KEY_F11;
            case Key::F12: return KEY_F12;
            case Key::F13: return KEY_F13;
            case Key::F14: return KEY_F14;
            case Key::F15: return KEY_F15;
            case Key::F16: return KEY_F16;
            case Key::F17: return KEY_F17;
            case Key::F18: return KEY_F18;
            case Key::F19: return KEY_F19;
            case Key::F20: return KEY_F20;
            case Key::F21: return KEY_F21;
            case Key::F22: return KEY_F22;
            case Key::F23: return KEY_F23;
            case Key::F24: return KEY_F24;

            case Key::PrintScreen: return KEY_SYSRQ;
            case Key::ScrollLock: return KEY_SCROLLLOCK;
            case Key::Pause: return KEY_PAUSE;

            case Key::Insert: return KEY_INSERT;
            case Key::Delete: return KEY_DELETE;
            case Key::Home: return KEY_HOME;
            case Key::End: return KEY_END;
            case Key::PageUp: return KEY_PAGEUP;
            case Key::PageDown: return KEY_PAGEDOWN;

            case Key::Up: return KEY_UP;
            case Key::Down: return KEY_DOWN;
            case Key::Left: return KEY_LEFT;
            case Key::Right: return KEY_RIGHT;

            case Key::NumLock: return KEY_NUMLOCK;
            case Key::Numpad0: return KEY_KP0;
            case Key::Numpad1: return KEY_KP1;
            case Key::Numpad2: return KEY_KP2;
            case Key::Numpad3: return KEY_KP3;
            case Key::Numpad4: return KEY_KP4;
            case Key::Numpad5: return KEY_KP5;
            case Key::Numpad6: return KEY_KP6;
            case Key::Numpad7: return KEY_KP7;
            case Key::Numpad8: return KEY_KP8;
            case Key::Numpad9: return KEY_KP9;
            case Key::NumpadAdd: return KEY_KPPLUS;
            case Key::NumpadSubtract: return KEY_KPMINUS;
            case Key::NumpadMultiply: return KEY_KPASTERISK;
            case Key::NumpadDivide: return KEY_KPSLASH;
            case Key::NumpadDecimal: return KEY_KPDOT;
            case Key::NumpadEnter: return KEY_KPENTER;

        #elif __APPLE__
            case Key::Unknown: return -1;

            case Key::A: return kVK_ANSI_A;
            case Key::B: return kVK_ANSI_B;
            case Key::C: return kVK_ANSI_C;
            case Key::D: return kVK_ANSI_D;
            case Key::E: return kVK_ANSI_E;
            case Key::F: return kVK_ANSI_F;
            case Key::G: return kVK_ANSI_G;
            case Key::H: return kVK_ANSI_H;
            case Key::I: return kVK_ANSI_I;
            case Key::J: return kVK_ANSI_J;
            case Key::K: return kVK_ANSI_K;
            case Key::L: return kVK_ANSI_L;
            case Key::M: return kVK_ANSI_M;
            case Key::N: return kVK_ANSI_N;
            case Key::O: return kVK_ANSI_O;
            case Key::P: return kVK_ANSI_P;
            case Key::Q: return kVK_ANSI_Q;
            case Key::R: return kVK_ANSI_R;
            case Key::S: return kVK_ANSI_S;
            case Key::T: return kVK_ANSI_T;
            case Key::U: return kVK_ANSI_U;
            case Key::V: return kVK_ANSI_V;
            case Key::W: return kVK_ANSI_W;
            case Key::X: return kVK_ANSI_X;
            case Key::Y: return kVK_ANSI_Y;
            case Key::Z: return kVK_ANSI_Z;

            case Key::Num0: return kVK_ANSI_0;
            case Key::Num1: return kVK_ANSI_1;
            case Key::Num2: return kVK_ANSI_2;
            case Key::Num3: return kVK_ANSI_3;
            case Key::Num4: return kVK_ANSI_4;
            case Key::Num5: return kVK_ANSI_5;
            case Key::Num6: return kVK_ANSI_6;
            case Key::Num7: return kVK_ANSI_7;
            case Key::Num8: return kVK_ANSI_8;
            case Key::Num9: return kVK_ANSI_9;

            case Key::Grave: return kVK_ANSI_Grave;
            case Key::Minus: return kVK_ANSI_Minus;
            case Key::Equal: return kVK_ANSI_Equal;
            case Key::LeftBracket: return kVK_ANSI_LeftBracket;
            case Key::RightBracket: return kVK_ANSI_RightBracket;
            case Key::Backslash: return kVK_ANSI_Backslash;
            case Key::Semicolon: return kVK_ANSI_Semicolon;
            case Key::Apostrophe: return kVK_ANSI_Quote;
            case Key::Comma: return kVK_ANSI_Comma;
            case Key::Period: return kVK_ANSI_Period;
            case Key::Slash: return kVK_ANSI_Slash;

            case Key::Escape: return kVK_Escape;
            case Key::Tab: return kVK_Tab;
            case Key::CapsLock: return kVK_CapsLock;
            case Key::Space: return kVK_Space;
            case Key::Backspace: return kVK_Delete;
            case Key::Enter: return kVK_Return;

            case Key::LeftShift: return kVK_Shift;
            case Key::RightShift: return kVK_RightShift;
            case Key::LeftControl: return kVK_Control;
            case Key::RightControl: return kVK_RightControl;
            case Key::LeftAlt: return kVK_Option;
            case Key::RightAlt: return kVK_RightOption;
            case Key::LeftSuper: return kVK_Command;
            case Key::RightSuper: return kVK_RightCommand;
            case Key::Menu: return -1; // Mac doesn't have a native Menu key

            case Key::F1: return kVK_F1;
            case Key::F2: return kVK_F2;
            case Key::F3: return kVK_F3;
            case Key::F4: return kVK_F4;
            case Key::F5: return kVK_F5;
            case Key::F6: return kVK_F6;
            case Key::F7: return kVK_F7;
            case Key::F8: return kVK_F8;
            case Key::F9: return kVK_F9;
            case Key::F10: return kVK_F10;
            case Key::F11: return kVK_F11;
            case Key::F12: return kVK_F12;
            case Key::F13: return kVK_F13;
            case Key::F14: return kVK_F14;
            case Key::F15: return kVK_F15;
            case Key::F16: return kVK_F16;
            case Key::F17: return kVK_F17;
            case Key::F18: return kVK_F18;
            case Key::F19: return kVK_F19;
            case Key::F20: return kVK_F20;
            case Key::F21: return -1; // Also macOS doesn't support function keys above F20
            case Key::F22: return -1;
            case Key::F23: return -1;
            case Key::F24: return -1;

            case Key::PrintScreen: return -1; // And it doesn't have these keys either
            case Key::ScrollLock: return -1;
            case Key::Pause: return -1;

            case Key::Insert: return kVK_Help; // macOS typically maps PC Insert to Help
            case Key::Delete: return kVK_ForwardDelete;
            case Key::Home: return kVK_Home;
            case Key::End: return kVK_End;
            case Key::PageUp: return kVK_PageUp;
            case Key::PageDown: return kVK_PageDown;

            case Key::Up: return kVK_UpArrow;
            case Key::Down: return kVK_DownArrow;
            case Key::Left: return kVK_LeftArrow;
            case Key::Right: return kVK_RightArrow;

            case Key::NumLock: return kVK_ANSI_KeypadClear;
            case Key::Numpad0: return kVK_ANSI_Keypad0;
            case Key::Numpad1: return kVK_ANSI_Keypad1;
            case Key::Numpad2: return kVK_ANSI_Keypad2;
            case Key::Numpad3: return kVK_ANSI_Keypad3;
            case Key::Numpad4: return kVK_ANSI_Keypad4;
            case Key::Numpad5: return kVK_ANSI_Keypad5;
            case Key::Numpad6: return kVK_ANSI_Keypad6;
            case Key::Numpad7: return kVK_ANSI_Keypad7;
            case Key::Numpad8: return kVK_ANSI_Keypad8;
            case Key::Numpad9: return kVK_ANSI_Keypad9;
            case Key::NumpadAdd: return kVK_ANSI_KeypadPlus;
            case Key::NumpadSubtract: return kVK_ANSI_KeypadMinus;
            case Key::NumpadMultiply: return kVK_ANSI_KeypadMultiply;
            case Key::NumpadDivide: return kVK_ANSI_KeypadDivide;
            case Key::NumpadDecimal: return kVK_ANSI_KeypadDecimal;
            case Key::NumpadEnter: return kVK_ANSI_KeypadEnter;

        #elif _WIN32
            case Key::Unknown: return -1;

            case Key::A: return 'A';
            case Key::B: return 'B';
            case Key::C: return 'C';
            case Key::D: return 'D';
            case Key::E: return 'E';
            case Key::F: return 'F';
            case Key::G: return 'G';
            case Key::H: return 'H';
            case Key::I: return 'I';
            case Key::J: return 'J';
            case Key::K: return 'K';
            case Key::L: return 'L';
            case Key::M: return 'M';
            case Key::N: return 'N';
            case Key::O: return 'O';
            case Key::P: return 'P';
            case Key::Q: return 'Q';
            case Key::R: return 'R';
            case Key::S: return 'S';
            case Key::T: return 'T';
            case Key::U: return 'U';
            case Key::V: return 'V';
            case Key::W: return 'W';
            case Key::X: return 'X';
            case Key::Y: return 'Y';
            case Key::Z: return 'Z';

            case Key::Num0: return '0';
            case Key::Num1: return '1';
            case Key::Num2: return '2';
            case Key::Num3: return '3';
            case Key::Num4: return '4';
            case Key::Num5: return '5';
            case Key::Num6: return '6';
            case Key::Num7: return '7';
            case Key::Num8: return '8';
            case Key::Num9: return '9';

            case Key::Grave: return VK_OEM_3;
            case Key::Minus: return VK_OEM_MINUS;
            case Key::Equal: return VK_OEM_PLUS;
            case Key::LeftBracket: return VK_OEM_4;
            case Key::RightBracket: return VK_OEM_6;
            case Key::Backslash: return VK_OEM_5;
            case Key::Semicolon: return VK_OEM_1;
            case Key::Apostrophe: return VK_OEM_7;
            case Key::Comma: return VK_OEM_COMMA;
            case Key::Period: return VK_OEM_PERIOD;
            case Key::Slash: return VK_OEM_2;

            case Key::Escape: return VK_ESCAPE;
            case Key::Tab: return VK_TAB;
            case Key::CapsLock: return VK_CAPITAL;
            case Key::Space: return VK_SPACE;
            case Key::Backspace: return VK_BACK;
            case Key::Enter: return VK_RETURN;

            case Key::LeftShift: return VK_LSHIFT;
            case Key::RightShift: return VK_RSHIFT;
            case Key::LeftControl: return VK_LCONTROL;
            case Key::RightControl: return VK_RCONTROL;
            case Key::LeftAlt: return VK_LMENU;
            case Key::RightAlt: return VK_RMENU;
            case Key::LeftSuper: return VK_LWIN;
            case Key::RightSuper: return VK_RWIN;
            case Key::Menu: return VK_APPS;

            case Key::F1: return VK_F1;
            case Key::F2: return VK_F2;
            case Key::F3: return VK_F3;
            case Key::F4: return VK_F4;
            case Key::F5: return VK_F5;
            case Key::F6: return VK_F6;
            case Key::F7: return VK_F7;
            case Key::F8: return VK_F8;
            case Key::F9: return VK_F9;
            case Key::F10: return VK_F10;
            case Key::F11: return VK_F11;
            case Key::F12: return VK_F12;
            case Key::F13: return VK_F13;
            case Key::F14: return VK_F14;
            case Key::F15: return VK_F15;
            case Key::F16: return VK_F16;
            case Key::F17: return VK_F17;
            case Key::F18: return VK_F18;
            case Key::F19: return VK_F19;
            case Key::F20: return VK_F20;
            case Key::F21: return VK_F21;
            case Key::F22: return VK_F22;
            case Key::F23: return VK_F23;
            case Key::F24: return VK_F24;

            case Key::PrintScreen: return VK_SNAPSHOT;
            case Key::ScrollLock: return VK_SCROLL;
            case Key::Pause: return VK_PAUSE;

            case Key::Insert: return VK_INSERT;
            case Key::Delete: return VK_DELETE;
            case Key::Home: return VK_HOME;
            case Key::End: return VK_END;
            case Key::PageUp: return VK_PRIOR;
            case Key::PageDown: return VK_NEXT;

            case Key::Up: return VK_UP;
            case Key::Down: return VK_DOWN;
            case Key::Left: return VK_LEFT;
            case Key::Right: return VK_RIGHT;

            case Key::NumLock: return VK_NUMLOCK;
            case Key::Numpad0: return VK_NUMPAD0;
            case Key::Numpad1: return VK_NUMPAD1;
            case Key::Numpad2: return VK_NUMPAD2;
            case Key::Numpad3: return VK_NUMPAD3;
            case Key::Numpad4: return VK_NUMPAD4;
            case Key::Numpad5: return VK_NUMPAD5;
            case Key::Numpad6: return VK_NUMPAD6;
            case Key::Numpad7: return VK_NUMPAD7;
            case Key::Numpad8: return VK_NUMPAD8;
            case Key::Numpad9: return VK_NUMPAD9;
            case Key::NumpadAdd: return VK_ADD;
            case Key::NumpadSubtract: return VK_SUBTRACT;
            case Key::NumpadMultiply: return VK_MULTIPLY;
            case Key::NumpadDivide: return VK_DIVIDE;
            case Key::NumpadDecimal: return VK_DECIMAL;
            case Key::NumpadEnter: return VK_RETURN; // Shares code with normal Enter

        #endif
            default: return -1;
        }
    }
}

#ifdef __linux__

    static int uinput_fd = -1;

    static ssize_t sendKeyEvent(int fd, int keyCode, int value) {
        input_event ev{};
        ev.type  = EV_KEY;
        ev.code  = keyCode;
        ev.value = value;
        ssize_t res = write(fd, &ev, sizeof(ev));
        if (res < 0) return res;

        ev.type  = EV_SYN;
        ev.code  = SYN_REPORT;
        ev.value = 0;
        res = write(fd, &ev, sizeof(ev));
        return res;
    }

    Keyboard::Keyboard() {
        uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (uinput_fd < 0)
            throw std::runtime_error("Cannot create virtual keyboard: /dev/uinput missing or no permissions");

        ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
        ioctl(uinput_fd, UI_SET_EVBIT, EV_SYN);

        for (int i = 0; i < 256; ++i)
            ioctl(uinput_fd, UI_SET_KEYBIT, i);

        uinput_setup usetup{};
        usetup.id.bustype = BUS_VIRTUAL;
        usetup.id.vendor  = 0x1D6B;
        usetup.id.product = 0x0100;
        std::strcpy(usetup.name, "libreconnect-keyboard");

        ioctl(uinput_fd, UI_DEV_SETUP, &usetup);
        ioctl(uinput_fd, UI_DEV_CREATE);

        if (sendKeyEvent(uinput_fd, KEY_RESERVED, 0) < 0)
            throw std::runtime_error("Virtual keyboard not ready after creation");
    }

    Keyboard::~Keyboard() {
        if (uinput_fd != -1) {
            ioctl(uinput_fd, UI_DEV_DESTROY);
            close(uinput_fd);
            uinput_fd = -1;
        }
    }

    void Keyboard::PressKey(Key key) {
        int nativeCode = GetNativeKey(key);
        if (nativeCode == -1) return;
        sendKeyEvent(uinput_fd, nativeCode, 1);
    }

    void Keyboard::ReleaseKey(Key key) {
        int nativeCode = GetNativeKey(key);
        if (nativeCode == -1) return;
        sendKeyEvent(uinput_fd, nativeCode, 0);
    }

#elif __APPLE__

    static CGEventSourceRef source = nullptr;

    Keyboard::Keyboard() :
        m_eventFlag(ThreadPool::GetContext().get_executor()),
        m_isRunning(true) {

        source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

        if (source)
            CGEventSourceSetLocalEventsSuppressionInterval(source, 0.0);

        asio::co_spawn(ThreadPool::GetContext(), CoProcessEvents(), asio::detached);
    }

    Keyboard::~Keyboard() {
        m_isRunning.store(false);
        m_eventFlag.Signal();

        if (source) {
            CFRelease(source);
            source = nullptr;
        }
    }

    void Keyboard::PressKey(Key key) {
        int nativeCode = GetNativeKey(key);
        if (nativeCode == -1) return;

        static thread_local moodycamel::ProducerToken producerToken(m_eventQueue);
        m_eventQueue.enqueue(producerToken, {nativeCode, true});
        m_eventFlag.Signal();
    }

    void Keyboard::ReleaseKey(Key key) {
        int nativeCode = GetNativeKey(key);
        if (nativeCode == -1) return;

        static thread_local moodycamel::ProducerToken producerToken(m_eventQueue);
        m_eventQueue.enqueue(producerToken, {nativeCode, false});
        m_eventFlag.Signal();
    }

    asio::awaitable<void> Keyboard::CoProcessEvents() {
        try {
            moodycamel::ConsumerToken consumerToken(m_eventQueue);
            asio::steady_timer timer(ThreadPool::GetContext().get_executor());

            while (m_isRunning.load()) {
                co_await m_eventFlag.Wait();

                KeyEvent ev{};
                while (m_isRunning.load() && m_eventQueue.try_dequeue(consumerToken, ev)) {
                    if (!source) continue;

                    CGEventRef event = CGEventCreateKeyboardEvent(
                        source,
                        (CGKeyCode)ev.keyCode,
                        ev.isPress
                    );

                    if (event) {
                        CGEventPost(kCGHIDEventTap, event);
                        CFRelease(event);
                    }

                    timer.expires_after(std::chrono::milliseconds(5));
                    co_await timer.async_wait(asio::use_awaitable);
                }
            }
        } catch (const std::system_error& error) {
            if (error.code() != asio::error::operation_aborted) {
                Debug::LogError("Keyboard::CoProcessEvents Asio error: {} (code: {})", error.what(), error.code().value());
            }
        } catch (const std::exception& e) {
            Debug::LogError("Keyboard::CoProcessEvents exception: {}", e.what());
        }
    }

#elif _WIN32

    Keyboard::Keyboard() = default;
    Keyboard::~Keyboard() = default;

    void Keyboard::PressKey(Key key) {
        int nativeCode = GetNativeKey(key);
        if (nativeCode == -1) return;

        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = nativeCode;
        SendInput(1, &input, sizeof(INPUT));
    }

    void Keyboard::ReleaseKey(Key key) {
        int nativeCode = GetNativeKey(key);
        if (nativeCode == -1) return;

        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = nativeCode;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }

#endif

void Keyboard::PressAndReleaseKey(Key key) {
    PressKey(key);
    ReleaseKey(key);
}
