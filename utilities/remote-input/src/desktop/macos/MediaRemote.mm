#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

void EmitMacMediaSignal(int code) {
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
}
