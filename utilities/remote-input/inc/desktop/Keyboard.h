#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "InputTypes.h"
#include "VirtualInputDevice.h"

class Keyboard : public VirtualInputDevice {
    public:
        Keyboard();
        ~Keyboard() override;

        void PressKey(Key key);
        void ReleaseKey(Key key);
        void PressAndReleaseKey(Key key);

        void TypeCharacter(uint32_t unicodePoint);
};

#endif // KEYBOARD_H
