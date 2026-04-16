#include "Keyboard.h"

#include <linux/input-event-codes.h>

int GetNativeKey(Key key) {
    switch (key) {
        case Key::Unknown: return -1;

        case Key::A: return KEY_A;
        case Key::B: return KEY_B;
        case Key::C: return KEY_C;
        case Key::D: return KEY_D;
        case Key::E: return KEY_E;
        case Key::F: return KEY_F;
        case Key::G: return KEY_G;
        case Key::H: return KEY_H;
        case Key::I: return KEY_I;
        case Key::J: return KEY_J;
        case Key::K: return KEY_K;
        case Key::L: return KEY_L;
        case Key::M: return KEY_M;
        case Key::N: return KEY_N;
        case Key::O: return KEY_O;
        case Key::P: return KEY_P;
        case Key::Q: return KEY_Q;
        case Key::R: return KEY_R;
        case Key::S: return KEY_S;
        case Key::T: return KEY_T;
        case Key::U: return KEY_U;
        case Key::V: return KEY_V;
        case Key::W: return KEY_W;
        case Key::X: return KEY_X;
        case Key::Y: return KEY_Y;
        case Key::Z: return KEY_Z;

        case Key::Num0: return KEY_0;
        case Key::Num1: return KEY_1;
        case Key::Num2: return KEY_2;
        case Key::Num3: return KEY_3;
        case Key::Num4: return KEY_4;
        case Key::Num5: return KEY_5;
        case Key::Num6: return KEY_6;
        case Key::Num7: return KEY_7;
        case Key::Num8: return KEY_8;
        case Key::Num9: return KEY_9;

        case Key::Grave: return KEY_GRAVE;
        case Key::Minus: return KEY_MINUS;
        case Key::Equal: return KEY_EQUAL;
        case Key::LeftBracket: return KEY_LEFTBRACE;
        case Key::RightBracket: return KEY_RIGHTBRACE;
        case Key::Backslash: return KEY_BACKSLASH;
        case Key::Semicolon: return KEY_SEMICOLON;
        case Key::Apostrophe: return KEY_APOSTROPHE;
        case Key::Comma: return KEY_COMMA;
        case Key::Period: return KEY_DOT;
        case Key::Slash: return KEY_SLASH;

        case Key::Escape: return KEY_ESC;
        case Key::Tab: return KEY_TAB;
        case Key::CapsLock: return KEY_CAPSLOCK;
        case Key::Space: return KEY_SPACE;
        case Key::Backspace: return KEY_BACKSPACE;
        case Key::Enter: return KEY_ENTER;

        case Key::LeftShift: return KEY_LEFTSHIFT;
        case Key::RightShift: return KEY_RIGHTSHIFT;
        case Key::LeftControl: return KEY_LEFTCTRL;
        case Key::RightControl: return KEY_RIGHTCTRL;
        case Key::LeftAlt: return KEY_LEFTALT;
        case Key::RightAlt: return KEY_RIGHTALT;
        case Key::LeftSuper: return KEY_LEFTMETA;
        case Key::RightSuper: return KEY_RIGHTMETA;
        case Key::Menu: return KEY_COMPOSE;

        case Key::F1: return KEY_F1;
        case Key::F2: return KEY_F2;
        case Key::F3: return KEY_F3;
        case Key::F4: return KEY_F4;
        case Key::F5: return KEY_F5;
        case Key::F6: return KEY_F6;
        case Key::F7: return KEY_F7;
        case Key::F8: return KEY_F8;
        case Key::F9: return KEY_F9;
        case Key::F10: return KEY_F10;
        case Key::F11: return KEY_F11;
        case Key::F12: return KEY_F12;
        case Key::F13: return KEY_F13;
        case Key::F14: return KEY_F14;
        case Key::F15: return KEY_F15;
        case Key::F16: return KEY_F16;
        case Key::F17: return KEY_F17;
        case Key::F18: return KEY_F18;
        case Key::F19: return KEY_F19;
        case Key::F20: return KEY_F20;
        case Key::F21: return KEY_F21;
        case Key::F22: return KEY_F22;
        case Key::F23: return KEY_F23;
        case Key::F24: return KEY_F24;

        case Key::PrintScreen: return KEY_SYSRQ;
        case Key::ScrollLock: return KEY_SCROLLLOCK;
        case Key::Pause: return KEY_PAUSE;

        case Key::Insert: return KEY_INSERT;
        case Key::Delete: return KEY_DELETE;
        case Key::Home: return KEY_HOME;
        case Key::End: return KEY_END;
        case Key::PageUp: return KEY_PAGEUP;
        case Key::PageDown: return KEY_PAGEDOWN;

        case Key::Up: return KEY_UP;
        case Key::Down: return KEY_DOWN;
        case Key::Left: return KEY_LEFT;
        case Key::Right: return KEY_RIGHT;

        case Key::NumLock: return KEY_NUMLOCK;
        case Key::Numpad0: return KEY_KP0;
        case Key::Numpad1: return KEY_KP1;
        case Key::Numpad2: return KEY_KP2;
        case Key::Numpad3: return KEY_KP3;
        case Key::Numpad4: return KEY_KP4;
        case Key::Numpad5: return KEY_KP5;
        case Key::Numpad6: return KEY_KP6;
        case Key::Numpad7: return KEY_KP7;
        case Key::Numpad8: return KEY_KP8;
        case Key::Numpad9: return KEY_KP9;
        case Key::NumpadAdd: return KEY_KPPLUS;
        case Key::NumpadSubtract: return KEY_KPMINUS;
        case Key::NumpadMultiply: return KEY_KPASTERISK;
        case Key::NumpadDivide: return KEY_KPSLASH;
        case Key::NumpadDecimal: return KEY_KPDOT;
        case Key::NumpadEnter: return KEY_KPENTER;

        default: return -1;
    }
}
