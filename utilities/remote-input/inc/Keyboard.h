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
};

#endif // KEYBOARD_H
