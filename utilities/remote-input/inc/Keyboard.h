#ifndef KEYBOARD_H
#define KEYBOARD_H

class Keyboard {
    public:
        Keyboard();
        ~Keyboard();

        void PressKey(int keyCode);
        void ReleaseKey(int keyCode);
        void PressAndReleaseKey(int keyCode);
};

#endif // KEYBOARD_H
