#include "VirtualInputDevice.h"

#include <Windows.h>

VirtualInputDevice::VirtualInputDevice(const char* /*deviceName*/) {}
VirtualInputDevice::~VirtualInputDevice() = default;

void VirtualInputDevice::EmitNativeKeyPress(int nativeKeyCode) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = nativeKeyCode;
    SendInput(1, &input, sizeof(INPUT));
}

void VirtualInputDevice::EmitNativeKeyRelease(int nativeKeyCode) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = nativeKeyCode;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}
