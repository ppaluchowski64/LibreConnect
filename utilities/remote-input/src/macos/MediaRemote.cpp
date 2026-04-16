#include <ApplicationServices/ApplicationServices.h>

void EmitMacMediaSignal(int code) {
    // 0xa is a press
    CGEventRef eventDown = CGEventCreate(nullptr);
    CGEventSetType(eventDown, kCGEventSystemDefined);
    CGEventSetIntegerValueField(eventDown, kCGEventEventSubtype, 8);
    CGEventSetIntegerValueField(eventDown, kCGEventEventData1, (code << 16) | (0xa << 8));
    CGEventPost(kCGHIDEventTap, eventDown);
    CFRelease(eventDown);

    // 0xb is a release
    CGEventRef eventUp = CGEventCreate(nullptr);
    CGEventSetType(eventUp, kCGEventSystemDefined);
    CGEventSetIntegerValueField(eventUp, kCGEventEventSubtype, 8);
    CGEventSetIntegerValueField(eventUp, kCGEventEventData1, (code << 16) | (0xb << 8));
    CGEventPost(kCGHIDEventTap, eventUp);
    CFRelease(eventUp);
}
