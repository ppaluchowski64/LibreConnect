#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>
#include "MediaNotificationManager.h"
#include <thread>
#include <chrono>

void EmitMacMediaSignal(int code) {
    bool wasVisible = MediaNotificationManager::IsVisible();
    if (wasVisible) {
        MediaNotificationManager::Hide();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 0xa is a press
    NSEvent *eventDown = [NSEvent otherEventWithType:NSEventTypeSystemDefined
                                            location:NSZeroPoint
                                       modifierFlags:0xa00 // key down
                                           timestamp:0
                                        windowNumber:0
                                             context:nil
                                             subtype:8
                                               data1:((code << 16) | (0xa << 8))
                                               data2:-1];

    CGEventPost(kCGHIDEventTap, [eventDown CGEvent]);

    // 0xb is a release
    NSEvent *eventUp = [NSEvent otherEventWithType:NSEventTypeSystemDefined
                                          location:NSZeroPoint
                                     modifierFlags:0xb00 // key up
                                         timestamp:0
                                      windowNumber:0
                                           context:nil
                                           subtype:8
                                             data1:((code << 16) | (0xb << 8))
                                             data2:-1];

    CGEventPost(kCGHIDEventTap, [eventUp CGEvent]);

    if (wasVisible) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        MediaNotificationManager::Show();
    }
}
