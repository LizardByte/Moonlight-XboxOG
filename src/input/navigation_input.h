#pragma once

namespace input {

  /**
   * @brief Abstract UI command emitted by controller or keyboard input.
   */
  enum class UiCommand {
    none,
    move_up,
    move_down,
    move_left,
    move_right,
    activate,
    confirm,
    back,
    delete_character,
    previous_page,
    next_page,
    toggle_overlay,
  };

  /**
   * @brief Controller buttons used by the Moonlight client UI.
   */
  enum class GamepadButton {
    dpad_up,
    dpad_down,
    dpad_left,
    dpad_right,
    a,
    b,
    x,
    y,
    left_shoulder,
    right_shoulder,
    start,
    back,
  };

  /**
   * @brief Keyboard keys mapped onto the same abstract UI commands.
   */
  enum class KeyboardKey {
    up,
    down,
    left,
    right,
    enter,
    escape,
    backspace,
    delete_key,
    space,
    tab,
    page_up,
    page_down,
    i,
    f3,
  };

  /**
   * @brief Map a controller button to a UI command.
   *
   * @param button Controller button that was pressed.
   * @return The abstract UI command to process.
   */
  UiCommand map_gamepad_button_to_ui_command(GamepadButton button);

  /**
   * @brief Map a keyboard key to a UI command.
   *
   * @param key Keyboard key that was pressed.
   * @param shiftPressed Whether Shift was held for keys such as Tab.
   * @return The abstract UI command to process.
   */
  UiCommand map_keyboard_key_to_ui_command(KeyboardKey key, bool shiftPressed = false);

}  // namespace input
