#include "src/ui/shell_view.h"

#include <vector>

#include <gtest/gtest.h>

namespace {

  TEST(ShellViewTest, BuildsHomeScreenContentFromTheInitialState) {
    const app::ClientState state = app::create_initial_state();

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.title, "Xbox");
    ASSERT_FALSE(viewModel.bodyLines.empty());
    EXPECT_EQ(viewModel.bodyLines.front(), "Controller-first Moonlight shell prototype.");
    EXPECT_EQ(viewModel.bodyLines.back(), "Saved hosts available: 0");
    ASSERT_EQ(viewModel.menuRows.size(), 4U);
    EXPECT_TRUE(viewModel.menuRows.front().selected);
    EXPECT_EQ(viewModel.menuRows.front().label, "Hosts");
    EXPECT_FALSE(viewModel.overlayVisible);
  }

  TEST(ShellViewTest, ShowsSavedHostDetailsOnTheHostsScreen) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
      {"Living Room PC", "192.168.0.10", 48000, app::PairingState::not_paired},
    }, "Loaded 1 saved host(s)");
    app::handle_command(state, input::UiCommand::activate);

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.title, "Hosts");
    ASSERT_GE(viewModel.bodyLines.size(), 6U);
    EXPECT_EQ(viewModel.bodyLines[0], "Saved hosts: 1");
    EXPECT_EQ(viewModel.bodyLines[1], "Selected host: Living Room PC");
    EXPECT_EQ(viewModel.bodyLines[2], "Address: 192.168.0.10");
    EXPECT_EQ(viewModel.bodyLines[3], "Port: 48000");
    EXPECT_EQ(viewModel.bodyLines[4], "Pairing: Not paired yet");
    EXPECT_NE(viewModel.bodyLines[5].find("Pair Selected Host"), std::string::npos);
  }

  TEST(ShellViewTest, ShowsKeypadBasedHostEntryInstructionsAndValidation) {
    app::ClientState state = app::create_initial_state();
    state.menu.handle_command(input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.activeField = app::AddHostField::port;
    state.addHostDraft.portInput = "48000";
    state.addHostDraft.validationMessage = "That host is already saved";
    state.addHostDraft.connectionMessage = "Connected to 192.168.0.10:48000";

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.title, "Add Host");
    ASSERT_GE(viewModel.bodyLines.size(), 8U);
    EXPECT_EQ(viewModel.bodyLines[0], "Manual host entry with a popup keypad.");
    EXPECT_EQ(viewModel.bodyLines[1], "Current address: 192.168.0.10");
    EXPECT_EQ(viewModel.bodyLines[2], "Current port: 48000");
    EXPECT_EQ(viewModel.bodyLines[3], "Selected field: Port");
    EXPECT_EQ(viewModel.bodyLines[4], "Select Host Address or Port to open the keypad modal.");
    EXPECT_EQ(viewModel.bodyLines[5], "Use Clear Current Field to erase the selected field.");
    EXPECT_EQ(viewModel.bodyLines[7], "Validation: That host is already saved");
    EXPECT_EQ(viewModel.bodyLines[8], "Connection: Connected to 192.168.0.10:48000");
    ASSERT_GE(viewModel.footerLines.size(), 6U);
    EXPECT_EQ(viewModel.footerLines[1], "Select Address or Port to open the keypad modal");
  }

  TEST(ShellViewTest, BuildsTheAddHostKeypadModalAsANumberPad) {
    app::ClientState state = app::create_initial_state();
    state.menu.handle_command(input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput.clear();
    app::handle_command(state, input::UiCommand::activate);

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.keypadModalVisible);
    EXPECT_EQ(viewModel.keypadModalTitle, "Address Keypad");
    ASSERT_GE(viewModel.keypadModalLines.size(), 3U);
    EXPECT_EQ(viewModel.keypadModalLines[0], "Editing field: Address");
    ASSERT_EQ(viewModel.keypadModalButtons.size(), 11U);
    EXPECT_EQ(viewModel.keypadModalColumnCount, 3U);
    EXPECT_EQ(viewModel.keypadModalButtons[0].label, "1");
    EXPECT_TRUE(viewModel.keypadModalButtons[0].selected);
    EXPECT_EQ(viewModel.keypadModalButtons[9].label, ".");
    EXPECT_EQ(viewModel.keypadModalButtons[10].label, "0");
    EXPECT_EQ(viewModel.footerLines[1], "Keypad open: D-pad moves, A enters, X deletes, Start accepts, B cancels");
  }

  TEST(ShellViewTest, ShowsThatPairingCanBeStartedForAnUnpairedHost) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
      {"Living Room PC", "192.168.0.10", 48000, app::PairingState::not_paired},
    });
    app::handle_command(state, input::UiCommand::activate);

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.title, "Hosts");
    ASSERT_GE(viewModel.bodyLines.size(), 5U);
    EXPECT_EQ(viewModel.bodyLines[0], "Saved hosts: 1");
    EXPECT_EQ(viewModel.bodyLines[1], "Selected host: Living Room PC");
    EXPECT_EQ(viewModel.bodyLines[4], "Pairing: Not paired yet");
    EXPECT_EQ(viewModel.bodyLines[5], "Select Pair Selected Host to start the Sunshine pairing handshake in the background.");

    bool foundPairingAction = false;
    for (const ui::ShellMenuRow &row : viewModel.menuRows) {
      if (row.id == "pair-host") {
        foundPairingAction = true;
        EXPECT_TRUE(row.enabled);
        EXPECT_EQ(row.label, "Pair Selected Host");
      }
    }
    EXPECT_TRUE(foundPairingAction);
  }

  TEST(ShellViewTest, AddsStatsAndRecentLogsToTheOverlayWhenVisible) {
    app::ClientState state = app::create_initial_state();
    state.overlayVisible = true;

    const std::vector<logging::LogEntry> entries = {
      {1, logging::LogLevel::info, "app", "Entered shell"},
      {2, logging::LogLevel::warning, "network", "No active stream"},
    };
    const std::vector<std::string> statsLines = {
      "Stream: 1280x720 @ 60 FPS",
      "Connection: Okay",
    };

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, entries, statsLines);

    EXPECT_TRUE(viewModel.overlayVisible);
    EXPECT_EQ(viewModel.overlayTitle, "Diagnostics");
    ASSERT_GE(viewModel.overlayLines.size(), 4U);
    EXPECT_EQ(viewModel.overlayLines[0], "Stream: 1280x720 @ 60 FPS");
    EXPECT_EQ(viewModel.overlayLines[1], "Connection: Okay");
    EXPECT_EQ(viewModel.overlayLines[2], "[INFO] app: Entered shell");
    EXPECT_EQ(viewModel.overlayLines[3], "[WARN] network: No active stream");
  }

  TEST(ShellViewTest, CanScrollBackToEarlierLogEntriesInTheOverlay) {
    app::ClientState state = app::create_initial_state();
    state.overlayVisible = true;
    state.overlayScrollOffset = 2;

    std::vector<logging::LogEntry> entries;
    entries.reserve(14);
    for (uint64_t index = 0; index < 14; ++index) {
      entries.push_back({index + 1, logging::LogLevel::info, "app", "entry-" + std::to_string(index)});
    }

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, entries);

    ASSERT_FALSE(viewModel.overlayLines.empty());
    EXPECT_EQ(viewModel.overlayLines.front(), "Showing earlier log entries");
  }

  TEST(ShellViewTest, UsesScreenSpecificTextForTheSettingsScreen) {
    app::ClientState state = app::create_initial_state();
    state.menu.handle_command(input::UiCommand::move_down);
    state.menu.handle_command(input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(state.activeScreen, app::ScreenId::settings);
    EXPECT_EQ(viewModel.title, "Settings");
    ASSERT_FALSE(viewModel.bodyLines.empty());
    EXPECT_NE(viewModel.bodyLines.front().find("Display, input, overlay, and logging settings"), std::string::npos);
  }

}  // namespace

