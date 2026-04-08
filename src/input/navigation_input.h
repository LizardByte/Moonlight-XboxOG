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
    open_context_menu,
    delete_character,
    previous_page,
    next_page,
    fast_previous_page,
    fast_next_page,
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
   * @brief Controller axis directions mapped onto UI navigation commands.
   */
  enum class GamepadAxisDirection {
    left_stick_up,
    left_stick_down,
    left_stick_left,
    left_stick_right,
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
    m,
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
   * @brief Map a controller axis direction to a UI command.
   *
   * @param direction Controller axis direction that crossed the navigation threshold.
   * @return The abstract UI command to process.
   */
  UiCommand map_gamepad_axis_direction_to_ui_command(GamepadAxisDirection direction);

  /**
   * @brief Map a keyboard key to a UI command.
   *
   * @param key Keyboard key that was pressed.
   * @param shiftPressed Whether Shift was held for keys such as Tab.
   * @return The abstract UI command to process.
   */
  UiCommand map_keyboard_key_to_ui_command(KeyboardKey key, bool shiftPressed = false);

}  // namespace input
