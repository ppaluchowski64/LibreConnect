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

    void Keyboard::PressAndReleaseKey(int keyCode) {
        PressKey(keyCode);
        ReleaseKey(keyCode);
    }

#endif
