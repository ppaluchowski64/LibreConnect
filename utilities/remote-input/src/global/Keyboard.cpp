#include "Keyboard.h"

void Keyboard::PressAndReleaseKey(Key key) {
    PressKey(key);
    ReleaseKey(key);
}
