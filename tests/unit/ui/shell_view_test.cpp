// test header include
#include "src/ui/shell_view.h"

// standard includes
#include <string>
#include <vector>

// lib inclues
#include <gtest/gtest.h>

// test includes
#include "tests/support/network_test_constants.h"

namespace {

  TEST(ShellViewTest, BuildsHostsScreenContentFromTheInitialState) {
    const app::ClientState state = app::create_initial_state();

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.title, "Moonlight");
    EXPECT_TRUE(viewModel.pageTitle.empty());
    ASSERT_EQ(viewModel.toolbarButtons.size(), 3U);
    EXPECT_TRUE(viewModel.toolbarButtons[2].selected);
    EXPECT_EQ(viewModel.toolbarButtons[2].label, "Add Host");
    EXPECT_EQ(viewModel.toolbarButtons[2].iconAssetPath, "icons\\add-host.svg");
    ASSERT_FALSE(viewModel.bodyLines.empty());
    EXPECT_EQ(viewModel.bodyLines.front(), "No PCs have been added yet.");
    ASSERT_EQ(viewModel.footerActions.size(), 2U);
    EXPECT_EQ(viewModel.footerActions[0].label, "Select");
    EXPECT_EQ(viewModel.footerActions[0].iconAssetPath, "icons\\button-a.svg");
    EXPECT_EQ(viewModel.footerActions[1].label, "Exit");
    EXPECT_EQ(viewModel.footerActions[1].iconAssetPath, "icons\\button-select.svg");
    EXPECT_EQ(viewModel.footerActions[1].secondaryIconAssetPath, "icons\\button-start.svg");
    EXPECT_FALSE(viewModel.overlayVisible);
  }

  TEST(ShellViewTest, ShowsSavedHostsAsTiles) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpHostGridA], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::not_paired, app::HostReachability::offline},
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              },
                       "Loaded 2 saved host(s)");
    app::handle_command(state, input::UiCommand::move_right);

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.pageTitle.empty());
    EXPECT_EQ(viewModel.hostColumnCount, 3U);
    ASSERT_EQ(viewModel.hostTiles.size(), 2U);
    EXPECT_EQ(viewModel.hostTiles[0].displayName, "Living Room PC");
    EXPECT_EQ(viewModel.hostTiles[0].statusLabel, "Offline");
    EXPECT_EQ(viewModel.hostTiles[0].iconAssetPath, "icons\\host-monitor-offline.svg");
    EXPECT_EQ(viewModel.hostTiles[1].displayName, "Office PC");
    EXPECT_EQ(viewModel.hostTiles[1].statusLabel, "Online");
    EXPECT_EQ(viewModel.hostTiles[1].iconAssetPath, "icons\\host-monitor-online.svg");
    EXPECT_TRUE(viewModel.hostTiles[1].selected);
    ASSERT_GE(viewModel.bodyLines.size(), 2U);
    EXPECT_EQ(viewModel.bodyLines[1], "Press Y on a controller, or I on a keyboard, for host actions.");
  }

  TEST(ShellViewTest, HidesHostMenuFooterActionWhenToolbarIsSelected) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpHostGridA], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });
    state.hostsFocusArea = app::HostsFocusArea::toolbar;

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    ASSERT_EQ(viewModel.footerActions.size(), 2U);
    EXPECT_EQ(viewModel.footerActions[0].label, "Select");
    EXPECT_EQ(viewModel.footerActions[1].label, "Exit");
  }

  TEST(ShellViewTest, ShowsKeypadBasedHostEntryInstructionsAndValidation) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.activeField = app::AddHostField::port;
    state.addHostDraft.addressInput = test_support::kTestIpv4Addresses[test_support::kIpHostGridA];
    state.addHostDraft.portInput = "48000";
    state.addHostDraft.validationMessage = "That host is already saved";
    state.addHostDraft.connectionMessage =
      "Connected to " + std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]) + ":48000";

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.pageTitle, "Add Host");
    ASSERT_GE(viewModel.bodyLines.size(), 4U);
    EXPECT_EQ(viewModel.bodyLines[0], "Manual host entry with a keypad modal.");
    EXPECT_EQ(viewModel.bodyLines[1], "Address: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]));
    EXPECT_EQ(viewModel.bodyLines[2], "Port: 48000");
    EXPECT_EQ(viewModel.bodyLines[3], "Press A to edit either field with the keypad modal.");
    ASSERT_EQ(viewModel.footerActions.size(), 2U);
    EXPECT_EQ(viewModel.footerActions[0].label, "Select");
    EXPECT_EQ(viewModel.footerActions[1].label, "Back");
  }

  TEST(ShellViewTest, ShowsLoggingDetailsOnTheSettingsScreen) {
    app::ClientState state = app::create_initial_state();
    state.loggingLevel = logging::LogLevel::none;
    state.xemuConsoleLoggingLevel = logging::LogLevel::warning;
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::settings);
    app::set_log_file_path(state, "E:\\UDATA\\12345678\\moonlight.log");

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    ASSERT_GE(viewModel.bodyLines.size(), 7U);
    EXPECT_EQ(viewModel.bodyLines[0], "Category: Logging");
    EXPECT_EQ(viewModel.bodyLines[1], "Runtime log file: reset on every startup");
    EXPECT_EQ(viewModel.bodyLines[2], "Log file path: E:\\UDATA\\12345678\\moonlight.log");
    EXPECT_EQ(viewModel.bodyLines[3], "File logging level: NONE");
    EXPECT_EQ(viewModel.bodyLines[4], "xemu console logging level: WARN");
    ASSERT_EQ(viewModel.detailMenuRows.size(), 3U);
    EXPECT_EQ(viewModel.detailMenuRows[0].label, "View Log File");
    EXPECT_EQ(viewModel.detailMenuRows[1].label, "File Logging Level: NONE");
    EXPECT_EQ(viewModel.detailMenuRows[2].label, "xemu Console Level: WARN");
    EXPECT_EQ(viewModel.selectedMenuRowDescription, "Control the runtime log file, the in-app log viewer, and xemu debugger output verbosity.");
    EXPECT_TRUE(viewModel.leftPanelActive);
    EXPECT_FALSE(viewModel.rightPanelActive);
  }

  TEST(ShellViewTest, ExposesTheFullSelectedSettingsLabelBesideTheMenu) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::settings);

    state.selectedSettingsCategory = app::SettingsCategory::reset;
    app::replace_saved_files(state, {
                                      {"E:\\UDATA\\12345678\\pairing\\a-very-long-file-name-that-needs-the-detail-pane.bin", "pairing\\a-very-long-file-name-that-needs-the-detail-pane.bin", 1536U},
                                    });
    state.settingsFocusArea = app::SettingsFocusArea::options;
    ASSERT_TRUE(state.detailMenu.select_item_by_id("delete-saved-file:E:\\UDATA\\12345678\\pairing\\a-very-long-file-name-that-needs-the-detail-pane.bin"));

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.selectedMenuRowLabel, "Delete pairing\\a-very-long-file-name-that-needs-the-detail-pane.bin");
    EXPECT_EQ(viewModel.selectedMenuRowDescription, "Delete only this saved file from disk while leaving the rest of the Moonlight data intact.");
  }

  TEST(ShellViewTest, ShowsSettingDescriptionsForFocusedOptions) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::settings);

    state.settingsFocusArea = app::SettingsFocusArea::options;
    ASSERT_TRUE(state.detailMenu.select_item_by_id("cycle-xemu-console-log-level"));

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.selectedMenuRowLabel, "xemu Console Level: NONE");
    EXPECT_EQ(viewModel.selectedMenuRowDescription, "Choose the minimum severity mirrored to xemu through DbgPrint() when you launch xemu with a serial console.");
  }

  TEST(ShellViewTest, MarksTheFocusedSettingsPanelAsActive) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::settings);

    state.settingsFocusArea = app::SettingsFocusArea::options;

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_FALSE(viewModel.leftPanelActive);
    EXPECT_TRUE(viewModel.rightPanelActive);
  }

  TEST(ShellViewTest, KeepsTheLeftPanelActiveOnTheAddHostScreen) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::add_host);

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.leftPanelActive);
    EXPECT_FALSE(viewModel.rightPanelActive);
  }

  TEST(ShellViewTest, BuildsTheAddHostKeypadModalAsANumberPad) {
    app::ClientState state = app::create_initial_state();
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
  }

  TEST(ShellViewTest, BuildsTheAppsPageForASelectedPairedHost) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });
    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    state.activeHost.runningGameId = 101;
    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], {
                                                                                                                                                             {"Steam", 101, true, false, false, "steam-cover", true, false},
                                                                                                                                                             {"Desktop", 102, false, false, false, "desktop-cover", false, false},
                                                                                                                                                           },
                               0x4242U,
                               true,
                               "Loaded 2 app(s)");

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_EQ(viewModel.screen, app::ScreenId::apps);
    EXPECT_EQ(viewModel.pageTitle, "Office PC");
    ASSERT_FALSE(viewModel.appTiles.empty());
    EXPECT_EQ(viewModel.appTiles[0].name, "Steam");
    EXPECT_TRUE(viewModel.appTiles[0].selected);
    EXPECT_TRUE(viewModel.bodyLines.empty());
    EXPECT_EQ(viewModel.appTiles[0].detail, "Running now");
    EXPECT_EQ(viewModel.appTiles[0].badgeLabel, "HDR");
    EXPECT_TRUE(viewModel.appTiles[0].boxArtCached);
    EXPECT_TRUE(viewModel.appTiles[1].detail.empty());
    ASSERT_EQ(viewModel.footerActions.size(), 3U);
    EXPECT_EQ(viewModel.footerActions[0].label, "Launch");
    EXPECT_EQ(viewModel.footerActions[1].label, "App Menu");
    EXPECT_EQ(viewModel.footerActions[2].label, "Back");
  }

  TEST(ShellViewTest, HidesCachedAppTilesWhenTheSelectedHostIsNoLongerPaired) {
    app::ClientState state = app::create_initial_state();
    state.activeScreen = app::ScreenId::apps;
    state.activeHostLoaded = true;
    state.activeHost = {
      "Office PC",
      test_support::kTestIpv4Addresses[test_support::kIpOffice],
      test_support::kTestPorts[test_support::kPortDefaultHost],
      app::PairingState::not_paired,
      app::HostReachability::online,
    };
    state.activeHost.apps = {
      {"Steam", 101, false, false, false, "steam-cover", true, false},
    };
    state.activeHost.appListState = app::HostAppListState::failed;
    state.activeHost.appListStatusMessage = "The host reports that this client is no longer paired. Pair the host again.";

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.appTiles.empty());
    ASSERT_FALSE(viewModel.bodyLines.empty());
    EXPECT_EQ(viewModel.bodyLines.front(), "This host is not paired yet. Return and select it to begin pairing.");
  }

  TEST(ShellViewTest, SuppressesTransientAppsLoadingTextAndNotifications) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });
    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    state.statusMessage = "Loading apps for Office PC...";

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.bodyLines.empty());
    EXPECT_FALSE(viewModel.notificationVisible);
  }

  TEST(ShellViewTest, ShowsOnlyBackOnAppsScreenWhenNoVisibleAppIsSelected) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });
    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    ASSERT_EQ(viewModel.footerActions.size(), 1U);
    EXPECT_EQ(viewModel.footerActions[0].label, "Back");
  }

  TEST(ShellViewTest, BuildsHostDetailsModalContent) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpHostGridA], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online, test_support::kTestIpv4Addresses[test_support::kIpHostGridA], "uuid-123", test_support::kTestIpv4Addresses[test_support::kIpHostGridA], test_support::kTestIpv4Addresses[test_support::kIpServerExternal], {}, test_support::kTestIpv4Addresses[test_support::kIpHostGridA], "00:11:22:33:44:55", test_support::kTestPorts[test_support::kPortResolvedHttps], 0},
                              });
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::open_context_menu);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.modalVisible);
    EXPECT_EQ(viewModel.modalTitle, "Host Details");
    ASSERT_GE(viewModel.modalLines.size(), 5U);
    EXPECT_EQ(viewModel.modalLines[0], "Name: Living Room PC");
    EXPECT_EQ(viewModel.modalLines[1], "State: ONLINE");
    EXPECT_EQ(viewModel.modalLines[2], "Active Address: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]));
  }

  TEST(ShellViewTest, BuildsDedicatedLogViewerModalState) {
    app::ClientState state = app::create_initial_state();
    app::set_log_file_path(state, "E:\\UDATA\\12345678\\moonlight.log");
    state.logViewerPlacement = app::LogViewerPlacement::right;
    app::apply_log_viewer_contents(state, {
                                            "[000001] [INFO] app: Entered shell",
                                            "[000002] [WARN] network: No active stream",
                                          },
                                   "Loaded log file preview");
    state.logViewerScrollOffset = 1;

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.modalVisible);
    EXPECT_TRUE(viewModel.logViewerVisible);
    EXPECT_EQ(viewModel.modalTitle, "Log File");
    EXPECT_EQ(viewModel.logViewerPath, "E:\\UDATA\\12345678\\moonlight.log");
    EXPECT_EQ(viewModel.logViewerPlacement, app::LogViewerPlacement::right);
    EXPECT_EQ(viewModel.logViewerScrollOffset, 1U);
    ASSERT_EQ(viewModel.logViewerLines.size(), 2U);
    EXPECT_EQ(viewModel.logViewerLines[0], "[000001] [INFO] app: Entered shell");
    ASSERT_EQ(viewModel.modalFooterActions.size(), 6U);
    EXPECT_EQ(viewModel.modalFooterActions[0].iconAssetPath, "icons\\button-lb.svg");
    EXPECT_EQ(viewModel.modalFooterActions[5].iconAssetPath, "icons\\button-b.svg");
  }

  TEST(ShellViewTest, BuildsSupportModalFooterActionsWithControllerIcons) {
    app::ClientState state = app::create_initial_state();
    state.modal.id = app::ModalId::support;

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.modalVisible);
    EXPECT_EQ(viewModel.modalTitle, "Support");
    ASSERT_EQ(viewModel.modalFooterActions.size(), 2U);
    EXPECT_EQ(viewModel.modalFooterActions[0].iconAssetPath, "icons\\button-a.svg");
    EXPECT_EQ(viewModel.modalFooterActions[0].secondaryIconAssetPath, "icons\\button-start.svg");
    EXPECT_EQ(viewModel.modalFooterActions[1].iconAssetPath, "icons\\button-b.svg");
  }

  TEST(ShellViewTest, BuildsConfirmationModalFooterActionsForResetDialogs) {
    app::ClientState state = app::create_initial_state();
    state.activeScreen = app::ScreenId::settings;
    state.modal.id = app::ModalId::confirmation;
    state.confirmation.action = app::ConfirmationAction::factory_reset;
    state.confirmation.title = "Factory Reset";
    state.confirmation.lines = {
      "Delete all Moonlight saved data?",
      "This removes hosts, logs, pairing identity, and cached cover art.",
    };

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.modalVisible);
    EXPECT_EQ(viewModel.modalTitle, "Factory Reset");
    ASSERT_EQ(viewModel.modalFooterActions.size(), 2U);
    EXPECT_EQ(viewModel.modalFooterActions[0].label, "OK");
    EXPECT_EQ(viewModel.modalFooterActions[0].iconAssetPath, "icons\\button-a.svg");
    EXPECT_EQ(viewModel.modalFooterActions[1].label, "Cancel");
    EXPECT_EQ(viewModel.modalFooterActions[1].iconAssetPath, "icons\\button-b.svg");
  }

  TEST(ShellViewTest, ShowsNotificationsOnTheSettingsScreen) {
    app::ClientState state = app::create_initial_state();
    state.activeScreen = app::ScreenId::settings;
    state.statusMessage = "Deleted saved file moonlight.log";

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    EXPECT_TRUE(viewModel.notificationVisible);
    EXPECT_EQ(viewModel.notification.message, "Deleted saved file moonlight.log");
  }

  TEST(ShellViewTest, HidesThePairingPinUntilReachabilityHasBeenConfirmed) {
    app::ClientState state = app::create_initial_state();
    state.activeScreen = app::ScreenId::pair_host;
    state.pairingDraft = {
      test_support::kTestIpv4Addresses[test_support::kIpHostGridA],
      test_support::kTestPorts[test_support::kPortPairing],
      "1234",
      app::PairingStage::idle,
      "Checking whether the host is reachable before pairing begins."
    };

    const ui::ShellViewModel viewModel = ui::build_shell_view_model(state, {});

    ASSERT_GE(viewModel.bodyLines.size(), 2U);
    EXPECT_EQ(viewModel.bodyLines[0], "Target host: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]));
    EXPECT_EQ(viewModel.bodyLines[1], "Checking whether the host is reachable before showing a PIN.");
    for (const std::string &line : viewModel.bodyLines) {
      EXPECT_EQ(line.find("PIN:"), std::string::npos);
    }
    EXPECT_FALSE(viewModel.notificationVisible);
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

}  // namespace
