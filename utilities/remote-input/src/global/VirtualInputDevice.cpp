#include "VirtualInputDevice.h"

void VirtualInputDevice::EmitNativeKeyPressAndRelease(int nativeKeyCode) {
    EmitNativeKeyPress(nativeKeyCode);
    EmitNativeKeyRelease(nativeKeyCode);
}
