// class header include
#include "src/input/navigation_input.h"

namespace input {

  UiCommand map_gamepad_button_to_ui_command(GamepadButton button) {
    switch (button) {
      case GamepadButton::dpad_up:
        return UiCommand::move_up;
      case GamepadButton::dpad_down:
        return UiCommand::move_down;
      case GamepadButton::dpad_left:
        return UiCommand::move_left;
      case GamepadButton::dpad_right:
        return UiCommand::move_right;
      case GamepadButton::a:
        return UiCommand::activate;
      case GamepadButton::start:
        return UiCommand::confirm;
      case GamepadButton::b:
      case GamepadButton::back:
        return UiCommand::back;
      case GamepadButton::x:
        return UiCommand::delete_character;
      case GamepadButton::left_shoulder:
        return UiCommand::previous_page;
      case GamepadButton::right_shoulder:
        return UiCommand::next_page;
      case GamepadButton::y:
        return UiCommand::toggle_overlay;
    }

    return UiCommand::none;
  }

  UiCommand map_keyboard_key_to_ui_command(KeyboardKey key, bool shiftPressed) {
    switch (key) {
      case KeyboardKey::up:
        return UiCommand::move_up;
      case KeyboardKey::down:
        return UiCommand::move_down;
      case KeyboardKey::left:
        return UiCommand::move_left;
      case KeyboardKey::right:
        return UiCommand::move_right;
      case KeyboardKey::enter:
        return UiCommand::confirm;
      case KeyboardKey::backspace:
      case KeyboardKey::delete_key:
        return UiCommand::delete_character;
      case KeyboardKey::space:
        return UiCommand::activate;
      case KeyboardKey::escape:
        return UiCommand::back;
      case KeyboardKey::tab:
        return shiftPressed ? UiCommand::previous_page : UiCommand::next_page;
      case KeyboardKey::page_up:
        return UiCommand::previous_page;
      case KeyboardKey::page_down:
        return UiCommand::next_page;
      case KeyboardKey::i:
      case KeyboardKey::f3:
        return UiCommand::toggle_overlay;
    }

    return UiCommand::none;
  }

}  // namespace input
