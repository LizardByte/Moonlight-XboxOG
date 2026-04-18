/**
 * @file tests/unit/input/navigation_input_test.cpp
 * @brief Verifies controller navigation input handling.
 */
// class header include
#include "src/input/navigation_input.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(NavigationInputTest, MapsControllerButtonsToNavigationCommands) {
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_up), input::UiCommand::move_up);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_down), input::UiCommand::move_down);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_left), input::UiCommand::move_left);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_right), input::UiCommand::move_right);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::a), input::UiCommand::activate);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::b), input::UiCommand::back);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::back), input::UiCommand::back);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::x), input::UiCommand::delete_character);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::start), input::UiCommand::confirm);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::left_shoulder), input::UiCommand::previous_page);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::right_shoulder), input::UiCommand::next_page);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(input::GamepadButton::y), input::UiCommand::open_context_menu);
    EXPECT_EQ(input::map_gamepad_button_to_ui_command(static_cast<input::GamepadButton>(999)), input::UiCommand::none);
  }

  TEST(NavigationInputTest, MapsControllerAxisDirectionsToNavigationCommands) {
    EXPECT_EQ(input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_up), input::UiCommand::move_up);
    EXPECT_EQ(input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_down), input::UiCommand::move_down);
    EXPECT_EQ(input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_left), input::UiCommand::move_left);
    EXPECT_EQ(input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_right), input::UiCommand::move_right);
    EXPECT_EQ(input::map_gamepad_axis_direction_to_ui_command(static_cast<input::GamepadAxisDirection>(999)), input::UiCommand::none);
  }

  TEST(NavigationInputTest, MapsKeyboardKeysToNavigationCommands) {
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::up), input::UiCommand::move_up);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::down), input::UiCommand::move_down);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::left), input::UiCommand::move_left);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::right), input::UiCommand::move_right);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::enter), input::UiCommand::confirm);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::escape), input::UiCommand::back);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::backspace), input::UiCommand::delete_character);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::delete_key), input::UiCommand::delete_character);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::space), input::UiCommand::activate);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::tab, false), input::UiCommand::next_page);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::tab, true), input::UiCommand::previous_page);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::page_up), input::UiCommand::previous_page);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::page_down), input::UiCommand::next_page);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::i), input::UiCommand::open_context_menu);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::m), input::UiCommand::open_context_menu);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(input::KeyboardKey::f3), input::UiCommand::toggle_overlay);
    EXPECT_EQ(input::map_keyboard_key_to_ui_command(static_cast<input::KeyboardKey>(999)), input::UiCommand::none);
  }

}  // namespace
