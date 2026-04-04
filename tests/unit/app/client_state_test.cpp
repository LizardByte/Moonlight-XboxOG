// class header include
#include "src/app/client_state.h"

// standard includes
#include <algorithm>

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(ClientStateTest, StartsOnTheHomeScreenWithTheHostsEntrySelected) {
    const app::ClientState state = app::create_initial_state();

    EXPECT_EQ(state.activeScreen, app::ScreenId::home);
    EXPECT_FALSE(state.overlayVisible);
    EXPECT_FALSE(state.shouldExit);
    EXPECT_FALSE(state.hostsDirty);
    ASSERT_NE(state.menu.selected_item(), nullptr);
    EXPECT_EQ(state.menu.selected_item()->id, "hosts");
  }

  TEST(ClientStateTest, ReplacesHostsFromPersistenceWithoutMarkingThemDirty) {
    app::ClientState state = app::create_initial_state();

    app::replace_hosts(state, {
      {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired},
      {"Office PC", "10.0.0.25", 48000, app::PairingState::paired},
    }, "Loaded 2 saved host(s)");

    ASSERT_EQ(state.hosts.size(), 2U);
    EXPECT_FALSE(state.hostsDirty);
    EXPECT_EQ(state.statusMessage, "Loaded 2 saved host(s)");
  }

  TEST(ClientStateTest, ActivatingHomeEntriesTransitionsBetweenTopLevelScreens) {
    app::ClientState state = app::create_initial_state();

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(update.activatedItemId, "hosts");
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);

    update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::home);

    state.menu.handle_command(input::UiCommand::move_down);
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(update.activatedItemId, "add-host");
    EXPECT_EQ(state.activeScreen, app::ScreenId::add_host);

    update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::home);

    state.menu.handle_command(input::UiCommand::move_down);
    state.menu.handle_command(input::UiCommand::move_down);
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(update.activatedItemId, "settings");
    EXPECT_EQ(state.activeScreen, app::ScreenId::settings);
  }

  TEST(ClientStateTest, TogglingAndScrollingTheOverlayUpdatesTheVisibleState) {
    app::ClientState state = app::create_initial_state();

    app::AppUpdate update = app::handle_command(state, input::UiCommand::toggle_overlay);
    EXPECT_TRUE(update.overlayChanged);
    EXPECT_TRUE(update.overlayVisibilityChanged);
    EXPECT_TRUE(state.overlayVisible);
    EXPECT_EQ(state.overlayScrollOffset, 0U);

    update = app::handle_command(state, input::UiCommand::previous_page);
    EXPECT_TRUE(update.overlayChanged);
    EXPECT_FALSE(update.overlayVisibilityChanged);
    EXPECT_GT(state.overlayScrollOffset, 0U);

    update = app::handle_command(state, input::UiCommand::next_page);
    EXPECT_TRUE(update.overlayChanged);
    EXPECT_FALSE(update.overlayVisibilityChanged);
    EXPECT_EQ(state.overlayScrollOffset, 0U);
  }

  TEST(ClientStateTest, ActivatingExitMarksTheClientForShutdown) {
    app::ClientState state = app::create_initial_state();

    state.menu.handle_command(input::UiCommand::move_down);
    state.menu.handle_command(input::UiCommand::move_down);
    state.menu.handle_command(input::UiCommand::move_down);

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.exitRequested);
    EXPECT_EQ(update.activatedItemId, "exit");
    EXPECT_TRUE(state.shouldExit);
  }

  TEST(ClientStateTest, CanSaveAManualHostEntryWithACustomPort) {
    app::ClientState state = app::create_initial_state();

    state.menu.handle_command(input::UiCommand::move_down);
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    ASSERT_EQ(state.activeScreen, app::ScreenId::add_host);
    state.addHostDraft.addressInput = "193.168.1.10";
    state.addHostDraft.portInput = "48000";
    state.menu.select_item_by_id("save-host");
    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    ASSERT_EQ(state.hosts.size(), 1U);
    EXPECT_EQ(state.hosts.front().address, "193.168.1.10");
    EXPECT_EQ(state.hosts.front().port, 48000);
    EXPECT_EQ(state.hosts.front().displayName, "Host 193.168.1.10");
    ASSERT_NE(state.menu.selected_item(), nullptr);
    EXPECT_EQ(state.menu.selected_item()->id, "host:193.168.1.10:48000");
  }

  TEST(ClientStateTest, RejectsDuplicateHostEntriesAndAllowsCancellationBackToHosts) {
    app::ClientState state = app::create_initial_state();

    state.menu.handle_command(input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = "192.168.0.10";
    state.menu.select_item_by_id("save-host");
    app::handle_command(state, input::UiCommand::activate);

    ASSERT_EQ(state.hosts.size(), 1U);
    ASSERT_EQ(state.activeScreen, app::ScreenId::hosts);

    state.menu.handle_command(input::UiCommand::move_down);
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    ASSERT_EQ(state.activeScreen, app::ScreenId::add_host);

    state.addHostDraft.addressInput = "192.168.0.10";
    state.menu.select_item_by_id("save-host");
    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::add_host);
    EXPECT_EQ(state.addHostDraft.validationMessage, "That host is already saved");

    state.menu.select_item_by_id("cancel-add-host");
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    ASSERT_NE(state.menu.selected_item(), nullptr);
    EXPECT_EQ(state.menu.selected_item()->id, "add-host");
  }

  TEST(ClientStateTest, StartsPairingForTheSelectedUnpairedHost) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
      {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired},
    });

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);

    const auto pairItem = std::find_if(state.menu.items().begin(), state.menu.items().end(), [](const ui::MenuItem &item) {
      return item.id == "pair-host";
    });
    ASSERT_NE(pairItem, state.menu.items().end());
    EXPECT_TRUE(pairItem->enabled);
    EXPECT_TRUE(state.menu.select_item_by_id("pair-host"));

    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.screenChanged);
    EXPECT_FALSE(update.hostsChanged);
    EXPECT_TRUE(update.pairingRequested);
    EXPECT_EQ(update.pairingAddress, "192.168.1.20");
    EXPECT_EQ(update.pairingPort, app::DEFAULT_HOST_PORT);
    EXPECT_TRUE(app::is_valid_pairing_pin(update.pairingPin));
    EXPECT_EQ(state.activeScreen, app::ScreenId::pair_host);
    EXPECT_EQ(state.pairingDraft.targetAddress, "192.168.1.20");
    EXPECT_EQ(state.pairingDraft.targetPort, app::DEFAULT_HOST_PORT);
    EXPECT_EQ(state.pairingDraft.generatedPin, update.pairingPin);
    EXPECT_EQ(state.pairingDraft.stage, app::PairingStage::in_progress);
    ASSERT_EQ(state.hosts.size(), 1U);
    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::not_paired);
    EXPECT_FALSE(state.hostsDirty);
  }

  TEST(ClientStateTest, CanDeleteTheSelectedHostAndKeepFocusOnTheRemainingList) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
      {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired},
      {"Office PC", "10.0.0.25", 48000, app::PairingState::paired},
    });

    app::handle_command(state, input::UiCommand::activate);
    state.menu.select_item_by_id("delete-host");
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.screenChanged);
    EXPECT_TRUE(update.hostsChanged);
    ASSERT_EQ(state.hosts.size(), 1U);
    EXPECT_EQ(state.hosts.front().address, "10.0.0.25");
    ASSERT_NE(state.menu.selected_item(), nullptr);
    EXPECT_EQ(state.menu.selected_item()->id, "host:10.0.0.25:48000");
    EXPECT_TRUE(state.hostsDirty);
  }

  TEST(ClientStateTest, RequestsAConnectionTestFromTheAddHostScreen) {
    app::ClientState state = app::create_initial_state();

    state.menu.handle_command(input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = "192.168.0.10";
    state.addHostDraft.portInput = "48000";
    state.menu.select_item_by_id("test-connection");

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.connectionTestRequested);
    EXPECT_EQ(update.connectionTestAddress, "192.168.0.10");
    EXPECT_EQ(update.connectionTestPort, 48000);
    EXPECT_EQ(state.statusMessage, "Testing connection to 192.168.0.10:48000...");
  }

  TEST(ClientStateTest, StagesCancelsDeletesAndAcceptsAddHostKeypadEdits) {
    app::ClientState state = app::create_initial_state();

    state.menu.handle_command(input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = "192.168.0.10";

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_FALSE(update.screenChanged);
    EXPECT_TRUE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 0U);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.10");

    app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.addHostDraft.addressInput, "192.168.0.10");
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.101");

    app::handle_command(state, input::UiCommand::delete_character);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.10");

    app::handle_command(state, input::UiCommand::back);
    EXPECT_FALSE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.addressInput, "192.168.0.10");
    ASSERT_NE(state.menu.selected_item(), nullptr);
    EXPECT_EQ(state.menu.selected_item()->id, "edit-address");

    app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(state.addHostDraft.keypad.visible);
    app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.101");
    app::handle_command(state, input::UiCommand::confirm);
    EXPECT_FALSE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.addressInput, "192.168.0.101");
  }

}  // namespace
