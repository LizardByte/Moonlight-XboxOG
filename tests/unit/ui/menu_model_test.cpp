#include "src/ui/menu_model.h"

#include <gtest/gtest.h>

namespace {

  TEST(MenuModelTest, SelectsTheFirstEnabledItemWhenConstructed) {
    const ui::MenuModel menu({
      {"disabled", "Disabled", false},
      {"hosts", "Hosts", true},
      {"settings", "Settings", true},
    });

    ASSERT_NE(menu.selected_item(), nullptr);
    EXPECT_EQ(menu.selected_index(), 1U);
    EXPECT_EQ(menu.selected_item()->id, "hosts");
  }

  TEST(MenuModelTest, MovesSelectionAndSkipsDisabledItems) {
    ui::MenuModel menu({
      {"hosts", "Hosts", true},
      {"disabled", "Disabled", false},
      {"settings", "Settings", true},
    });

    const ui::MenuUpdate update = menu.handle_command(input::UiCommand::move_down);

    EXPECT_TRUE(update.selectionChanged);
    ASSERT_NE(menu.selected_item(), nullptr);
    EXPECT_EQ(menu.selected_item()->id, "settings");
  }

  TEST(MenuModelTest, WrapsAroundWhenMovingPastTheLastItem) {
    ui::MenuModel menu({
      {"hosts", "Hosts", true},
      {"settings", "Settings", true},
    });

    EXPECT_TRUE(menu.handle_command(input::UiCommand::move_up).selectionChanged);
    ASSERT_NE(menu.selected_item(), nullptr);
    EXPECT_EQ(menu.selected_item()->id, "settings");
  }

  TEST(MenuModelTest, ActivatesTheSelectedItem) {
    ui::MenuModel menu({
      {"hosts", "Hosts", true},
      {"settings", "Settings", true},
    });

    const ui::MenuUpdate update = menu.handle_command(input::UiCommand::activate);

    EXPECT_TRUE(update.activationRequested);
    EXPECT_EQ(update.activatedItemId, "hosts");
  }

  TEST(MenuModelTest, CanSelectAnEnabledItemById) {
    ui::MenuModel menu({
      {"hosts", "Hosts", true},
      {"disabled", "Disabled", false},
      {"settings", "Settings", true},
    });

    EXPECT_TRUE(menu.select_item_by_id("settings"));
    ASSERT_NE(menu.selected_item(), nullptr);
    EXPECT_EQ(menu.selected_item()->id, "settings");
    EXPECT_FALSE(menu.select_item_by_id("disabled"));
    EXPECT_EQ(menu.selected_item()->id, "settings");
  }

  TEST(MenuModelTest, SurfacesBackAndOverlayActionsWithoutChangingSelection) {
    ui::MenuModel menu({
      {"hosts", "Hosts", true},
    });

    const ui::MenuUpdate backUpdate = menu.handle_command(input::UiCommand::back);
    const ui::MenuUpdate overlayUpdate = menu.handle_command(input::UiCommand::toggle_overlay);

    EXPECT_TRUE(backUpdate.backRequested);
    EXPECT_TRUE(overlayUpdate.overlayToggleRequested);
    ASSERT_NE(menu.selected_item(), nullptr);
    EXPECT_EQ(menu.selected_item()->id, "hosts");
  }

}  // namespace

