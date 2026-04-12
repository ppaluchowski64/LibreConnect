#include "Keyboard.h"

#ifdef __linux__

    #include <fcntl.h>
    #include <unistd.h>
    #include <linux/uinput.h>
    #include <cstring>
    #include <stdexcept>

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

    void Keyboard::PressKey(int keyCode) {
        sendKeyEvent(uinput_fd, keyCode, 1);
    }

    void Keyboard::ReleaseKey(int keyCode) {
        sendKeyEvent(uinput_fd, keyCode, 0);
    }

#elif __APPLE__

    #include <ApplicationServices/ApplicationServices.h>
    #include <ThreadPool.h>
    #include <iostream>

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

    void Keyboard::PressKey(int keyCode) {
        static thread_local moodycamel::ProducerToken producerToken(m_eventQueue);
        m_eventQueue.enqueue(producerToken, {keyCode, true});
        m_eventFlag.Signal();
    }

    void Keyboard::ReleaseKey(int keyCode) {
        static thread_local moodycamel::ProducerToken producerToken(m_eventQueue);
        m_eventQueue.enqueue(producerToken, {keyCode, false});
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

    #include <Windows.h>

    Keyboard::Keyboard() = default;
    Keyboard::~Keyboard() = default;

    void Keyboard::PressKey(int keyCode) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = keyCode;
        SendInput(1, &input, sizeof(INPUT));
    }

    void Keyboard::ReleaseKey(int keyCode) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = keyCode;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }

#endif

void Keyboard::PressAndReleaseKey(int keyCode) {
    PressKey(keyCode);
    ReleaseKey(keyCode);
}
