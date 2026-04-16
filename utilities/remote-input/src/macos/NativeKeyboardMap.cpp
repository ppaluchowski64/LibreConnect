#include "NativeKeyboardMap.h"

#include "InputTypes.h"

#include <Carbon/Carbon.h>

int GetNativeKeyCode(Key key) {
    switch (key) {
        case Key::A: return kVK_ANSI_A;
        case Key::B: return kVK_ANSI_B;
        case Key::C: return kVK_ANSI_C;
        case Key::D: return kVK_ANSI_D;
        case Key::E: return kVK_ANSI_E;
        case Key::F: return kVK_ANSI_F;
        case Key::G: return kVK_ANSI_G;
        case Key::H: return kVK_ANSI_H;
        case Key::I: return kVK_ANSI_I;
        case Key::J: return kVK_ANSI_J;
        case Key::K: return kVK_ANSI_K;
        case Key::L: return kVK_ANSI_L;
        case Key::M: return kVK_ANSI_M;
        case Key::N: return kVK_ANSI_N;
        case Key::O: return kVK_ANSI_O;
        case Key::P: return kVK_ANSI_P;
        case Key::Q: return kVK_ANSI_Q;
        case Key::R: return kVK_ANSI_R;
        case Key::S: return kVK_ANSI_S;
        case Key::T: return kVK_ANSI_T;
        case Key::U: return kVK_ANSI_U;
        case Key::V: return kVK_ANSI_V;
        case Key::W: return kVK_ANSI_W;
        case Key::X: return kVK_ANSI_X;
        case Key::Y: return kVK_ANSI_Y;
        case Key::Z: return kVK_ANSI_Z;

        case Key::Num0: return kVK_ANSI_0;
        case Key::Num1: return kVK_ANSI_1;
        case Key::Num2: return kVK_ANSI_2;
        case Key::Num3: return kVK_ANSI_3;
        case Key::Num4: return kVK_ANSI_4;
        case Key::Num5: return kVK_ANSI_5;
        case Key::Num6: return kVK_ANSI_6;
        case Key::Num7: return kVK_ANSI_7;
        case Key::Num8: return kVK_ANSI_8;
        case Key::Num9: return kVK_ANSI_9;

        case Key::Grave: return kVK_ANSI_Grave;
        case Key::Minus: return kVK_ANSI_Minus;
        case Key::Equal: return kVK_ANSI_Equal;
        case Key::LeftBracket: return kVK_ANSI_LeftBracket;
        case Key::RightBracket: return kVK_ANSI_RightBracket;
        case Key::Backslash: return kVK_ANSI_Backslash;
        case Key::Semicolon: return kVK_ANSI_Semicolon;
        case Key::Apostrophe: return kVK_ANSI_Quote;
        case Key::Comma: return kVK_ANSI_Comma;
        case Key::Period: return kVK_ANSI_Period;
        case Key::Slash: return kVK_ANSI_Slash;

        case Key::Escape: return kVK_Escape;
        case Key::Tab: return kVK_Tab;
        case Key::CapsLock: return kVK_CapsLock;
        case Key::Space: return kVK_Space;
        case Key::Backspace: return kVK_Delete;
        case Key::Enter: return kVK_Return;

        case Key::LeftShift: return kVK_Shift;
        case Key::RightShift: return kVK_RightShift;
        case Key::LeftControl: return kVK_Control;
        case Key::RightControl: return kVK_RightControl;
        case Key::LeftAlt: return kVK_Option;
        case Key::RightAlt: return kVK_RightOption;
        case Key::LeftSuper: return kVK_Command;
        case Key::RightSuper: return kVK_RightCommand;
        case Key::Menu: return -1; // Mac doesn't have a native Menu key

        case Key::F1: return kVK_F1;
        case Key::F2: return kVK_F2;
        case Key::F3: return kVK_F3;
        case Key::F4: return kVK_F4;
        case Key::F5: return kVK_F5;
        case Key::F6: return kVK_F6;
        case Key::F7: return kVK_F7;
        case Key::F8: return kVK_F8;
        case Key::F9: return kVK_F9;
        case Key::F10: return kVK_F10;
        case Key::F11: return kVK_F11;
        case Key::F12: return kVK_F12;
        case Key::F13: return kVK_F13;
        case Key::F14: return kVK_F14;
        case Key::F15: return kVK_F15;
        case Key::F16: return kVK_F16;
        case Key::F17: return kVK_F17;
        case Key::F18: return kVK_F18;
        case Key::F19: return kVK_F19;
        case Key::F20: return kVK_F20;
        case Key::F21: return -1; // Also macOS doesn't support function keys above F20
        case Key::F22: return -1;
        case Key::F23: return -1;
        case Key::F24: return -1;

        case Key::PrintScreen: return -1; // And it doesn't have these keys either
        case Key::ScrollLock: return -1;
        case Key::Pause: return -1;

        case Key::Insert: return kVK_Help; // macOS typically maps PC Insert to Help
        case Key::Delete: return kVK_ForwardDelete;
        case Key::Home: return kVK_Home;
        case Key::End: return kVK_End;
        case Key::PageUp: return kVK_PageUp;
        case Key::PageDown: return kVK_PageDown;

        case Key::Up: return kVK_UpArrow;
        case Key::Down: return kVK_DownArrow;
        case Key::Left: return kVK_LeftArrow;
        case Key::Right: return kVK_RightArrow;

        case Key::NumLock: return kVK_ANSI_KeypadClear;
        case Key::Numpad0: return kVK_ANSI_Keypad0;
        case Key::Numpad1: return kVK_ANSI_Keypad1;
        case Key::Numpad2: return kVK_ANSI_Keypad2;
        case Key::Numpad3: return kVK_ANSI_Keypad3;
        case Key::Numpad4: return kVK_ANSI_Keypad4;
        case Key::Numpad5: return kVK_ANSI_Keypad5;
        case Key::Numpad6: return kVK_ANSI_Keypad6;
        case Key::Numpad7: return kVK_ANSI_Keypad7;
        case Key::Numpad8: return kVK_ANSI_Keypad8;
        case Key::Numpad9: return kVK_ANSI_Keypad9;
        case Key::NumpadAdd: return kVK_ANSI_KeypadPlus;
        case Key::NumpadSubtract: return kVK_ANSI_KeypadMinus;
        case Key::NumpadMultiply: return kVK_ANSI_KeypadMultiply;
        case Key::NumpadDivide: return kVK_ANSI_KeypadDivide;
        case Key::NumpadDecimal: return kVK_ANSI_KeypadDecimal;
        case Key::NumpadEnter: return kVK_ANSI_KeypadEnter;

        default: return -1;
    }
}
