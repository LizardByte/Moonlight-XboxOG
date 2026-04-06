// class header include
#include "src/input/navigation_input.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(NavigationInputTest, MapsControllerButtonsToNavigationCommands) {
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_up), input::UiCommand::move_up);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::a), input::UiCommand::activate);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::b), input::UiCommand::back);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::x), input::UiCommand::delete_character);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::start), input::UiCommand::confirm);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::left_shoulder), input::UiCommand::previous_page);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::y), input::UiCommand::open_context_menu);
  }

  TEST(NavigationInputTest, MapsKeyboardKeysToNavigationCommands) {
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::down), input::UiCommand::move_down);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::enter), input::UiCommand::confirm);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::escape), input::UiCommand::back);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::backspace), input::UiCommand::delete_character);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::delete_key), input::UiCommand::delete_character);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::tab, false), input::UiCommand::next_page);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::tab, true), input::UiCommand::previous_page);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::i), input::UiCommand::open_context_menu);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::m), input::UiCommand::open_context_menu);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::f3), input::UiCommand::toggle_overlay);
  }

}  // namespace
