/**
 * @file src/input/navigation_input.h
 * @brief Declares controller navigation input handling.
 */
#pragma once

namespace input {

  /**
   * @brief Abstract UI command emitted by controller or keyboard input.
   */
  enum class UiCommand {
    none,  ///< No UI action should be performed.
    move_up,  ///< Move selection upward.
    move_down,  ///< Move selection downward.
    move_left,  ///< Move selection left.
    move_right,  ///< Move selection right.
    activate,  ///< Activate the focused item.
    confirm,  ///< Confirm the current action.
    back,  ///< Navigate back or cancel.
    open_context_menu,  ///< Open the focused item's context menu.
    delete_character,  ///< Delete one character from text input.
    previous_page,  ///< Move to the previous page.
    next_page,  ///< Move to the next page.
    fast_previous_page,  ///< Jump backward by a larger page increment.
    fast_next_page,  ///< Jump forward by a larger page increment.
    toggle_overlay,  ///< Toggle the diagnostics overlay.
  };

  /**
   * @brief Controller buttons used by the Moonlight client UI.
   */
  enum class GamepadButton {
    dpad_up,  ///< D-pad up button.
    dpad_down,  ///< D-pad down button.
    dpad_left,  ///< D-pad left button.
    dpad_right,  ///< D-pad right button.
    a,  ///< South face button.
    b,  ///< East face button.
    x,  ///< West face button.
    y,  ///< North face button.
    left_shoulder,  ///< Left shoulder button.
    right_shoulder,  ///< Right shoulder button.
    start,  ///< Start button.
    back,  ///< Back button.
  };

  /**
   * @brief Controller axis directions mapped onto UI navigation commands.
   */
  enum class GamepadAxisDirection {
    left_stick_up,  ///< Left stick moved upward past the navigation threshold.
    left_stick_down,  ///< Left stick moved downward past the navigation threshold.
    left_stick_left,  ///< Left stick moved left past the navigation threshold.
    left_stick_right,  ///< Left stick moved right past the navigation threshold.
  };

  /**
   * @brief Keyboard keys mapped onto the same abstract UI commands.
   */
  enum class KeyboardKey {
    up,  ///< Up arrow key.
    down,  ///< Down arrow key.
    left,  ///< Left arrow key.
    right,  ///< Right arrow key.
    enter,  ///< Enter or return key.
    escape,  ///< Escape key.
    backspace,  ///< Backspace key.
    delete_key,  ///< Delete key.
    space,  ///< Space bar.
    tab,  ///< Tab key.
    page_up,  ///< Page Up key.
    page_down,  ///< Page Down key.
    i,  ///< Letter I key.
    m,  ///< Letter M key.
    f3,  ///< F3 function key.
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
