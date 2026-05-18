#include "VirtualInputDevice.h"

#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>

#include <cstring>
#include <stdexcept>

VirtualInputDevice::VirtualInputDevice(const char* deviceName) {
    m_uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (m_uinput_fd < 0)
        throw std::runtime_error("Cannot create virtual device: /dev/uinput missing or no permissions");

    ioctl(m_uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(m_uinput_fd, UI_SET_EVBIT, EV_SYN);

    for (int i = 0; i < 256; ++i)
        ioctl(m_uinput_fd, UI_SET_KEYBIT, i);

    uinput_setup usetup{};
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor  = 0x1D6B;
    usetup.id.product = 0x0100;
    std::strncpy(usetup.name, deviceName, UINPUT_MAX_NAME_SIZE - 1);

    ioctl(m_uinput_fd, UI_DEV_SETUP, &usetup);
    ioctl(m_uinput_fd, UI_DEV_CREATE);

    if (SendKeyEvent(KEY_RESERVED, 0) < 0)
        throw std::runtime_error("Virtual device not ready after creation");
}

VirtualInputDevice::~VirtualInputDevice() {
    if (m_uinput_fd != -1) {
        ioctl(m_uinput_fd, UI_DEV_DESTROY);
        close(m_uinput_fd);
        m_uinput_fd = -1;
    }
}

void VirtualInputDevice::EmitNativeKeyPress(int nativeKeyCode) {
    (void)SendKeyEvent(nativeKeyCode, 1);
}

void VirtualInputDevice::EmitNativeKeyRelease(int nativeKeyCode) {
    (void)SendKeyEvent(nativeKeyCode, 0);
}

ssize_t VirtualInputDevice::SendKeyEvent(int keyCode, int value) const {
    input_event ev{};
    ev.type  = EV_KEY;
    ev.code  = keyCode;
    ev.value = value;
    ssize_t res = write(m_uinput_fd, &ev, sizeof(ev));
    if (res < 0) return res;

    ev.type  = EV_SYN;
    ev.code  = SYN_REPORT;
    ev.value = 0;
    res = write(m_uinput_fd, &ev, sizeof(ev));
    return res;
}
