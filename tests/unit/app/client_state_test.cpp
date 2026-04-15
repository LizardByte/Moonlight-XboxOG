/**
 * @file tests/unit/app/client_state_test.cpp
 * @brief Verifies client state models and transitions.
 */
// class header include
#include "src/app/client_state.h"

// lib includes
#include <gtest/gtest.h>
#include <string>

// test includes
#include "tests/support/network_test_constants.h"

namespace {

  TEST(ClientStateTest, StartsOnTheHostsScreenWithTheToolbarSelected) {
    const app::ClientState state = app::create_initial_state();

    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    EXPECT_EQ(state.hosts.focusArea, app::HostsFocusArea::toolbar);
    EXPECT_EQ(state.hosts.selectedToolbarButtonIndex, 2U);
    EXPECT_FALSE(state.shell.overlayVisible);
    EXPECT_FALSE(state.shell.shouldExit);
    EXPECT_FALSE(state.hosts.dirty);
    EXPECT_EQ(state.settings.loggingLevel, logging::LogLevel::none);
  }

  TEST(ClientStateTest, ReplacesHostsFromPersistenceWithoutMarkingThemDirty) {
    app::ClientState state = app::create_initial_state();

    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::not_paired},
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired},
                              },
                       "Loaded 2 saved host(s)");

    ASSERT_EQ(state.hosts.size(), 2U);
    EXPECT_FALSE(state.hosts.dirty);
    EXPECT_EQ(state.shell.statusMessage, "Loaded 2 saved host(s)");
    EXPECT_EQ(state.hosts.focusArea, app::HostsFocusArea::grid);
    EXPECT_EQ(state.hosts.selectedHostIndex, 0U);
  }

  TEST(ClientStateTest, HostsToolbarCanOpenSettingsAndAddHost) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_EQ(update.navigation.activatedItemId, "settings-button");
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::settings);

    update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);

    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_EQ(update.navigation.activatedItemId, "add-host-button");
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::add_host);
  }

  TEST(ClientStateTest, SettingsCanRequestLogViewingAndCycleBothLoggingLevelsFromNone) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::settings);

    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.settings.selectedCategory, app::SettingsCategory::logging);
    EXPECT_EQ(state.settings.focusArea, app::SettingsFocusArea::options);

    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.requests.logViewRequested);
    EXPECT_EQ(update.navigation.activatedItemId, "view-log-file");

    app::handle_command(state, input::UiCommand::move_down);
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_FALSE(update.requests.logViewRequested);
    EXPECT_TRUE(update.persistence.settingsChanged);
    EXPECT_EQ(state.settings.loggingLevel, logging::LogLevel::error);
    EXPECT_EQ(state.shell.statusMessage, "Logging level set to ERROR");

    app::handle_command(state, input::UiCommand::move_down);
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.persistence.settingsChanged);
    EXPECT_EQ(state.settings.xemuConsoleLoggingLevel, logging::LogLevel::error);
    EXPECT_EQ(state.shell.statusMessage, "xemu console logging level set to ERROR");
  }

  TEST(ClientStateTest, TogglingAndScrollingTheOverlayUpdatesTheVisibleState) {
    app::ClientState state = app::create_initial_state();

    app::AppUpdate update = app::handle_command(state, input::UiCommand::toggle_overlay);
    EXPECT_TRUE(update.navigation.overlayChanged);
    EXPECT_TRUE(update.navigation.overlayVisibilityChanged);
    EXPECT_TRUE(state.shell.overlayVisible);
    EXPECT_EQ(state.shell.overlayScrollOffset, 0U);

    update = app::handle_command(state, input::UiCommand::previous_page);
    EXPECT_TRUE(update.navigation.overlayChanged);
    EXPECT_GT(state.shell.overlayScrollOffset, 0U);

    update = app::handle_command(state, input::UiCommand::next_page);
    EXPECT_TRUE(update.navigation.overlayChanged);
    EXPECT_EQ(state.shell.overlayScrollOffset, 0U);
  }

  TEST(ClientStateTest, BackFromHostsDoesNotRequestShutdown) {
    app::ClientState state = app::create_initial_state();

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::back);

    EXPECT_FALSE(update.navigation.exitRequested);
    EXPECT_FALSE(state.shell.shouldExit);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, CanSaveAManualHostEntryWithACustomPort) {
    app::ClientState state = app::create_initial_state();

    state.hosts.focusArea = app::HostsFocusArea::toolbar;
    state.hosts.selectedToolbarButtonIndex = 2U;
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::add_host);
    state.addHostDraft.addressInput = test_support::kTestIpv4Addresses[test_support::kIpManualCustomPort];
    state.addHostDraft.portInput = "48000";
    state.menu.select_item_by_id("save-host");
    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    ASSERT_EQ(state.hosts.size(), 1U);
    EXPECT_EQ(state.hosts.front().address, test_support::kTestIpv4Addresses[test_support::kIpManualCustomPort]);
    EXPECT_EQ(state.hosts.front().port, test_support::kTestPorts[test_support::kPortDefaultHost]);
    EXPECT_EQ(state.hosts.front().displayName, "Host " + std::string(test_support::kTestIpv4Addresses[test_support::kIpManualCustomPort]));
    EXPECT_EQ(state.hosts.selectedHostIndex, 0U);
  }

  TEST(ClientStateTest, RejectsDuplicateHostEntriesAndAllowsCancellationBackToHosts) {
    app::ClientState state = app::create_initial_state();

    state.hosts.focusArea = app::HostsFocusArea::toolbar;
    state.hosts.selectedToolbarButtonIndex = 2U;
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = test_support::kTestIpv4Addresses[test_support::kIpHostGridA];
    state.menu.select_item_by_id("save-host");
    app::handle_command(state, input::UiCommand::activate);

    ASSERT_EQ(state.hosts.size(), 1U);
    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::hosts);

    state.hosts.focusArea = app::HostsFocusArea::toolbar;
    state.hosts.selectedToolbarButtonIndex = 2U;
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.navigation.screenChanged);
    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::add_host);

    state.addHostDraft.addressInput = test_support::kTestIpv4Addresses[test_support::kIpHostGridA];
    state.menu.select_item_by_id("save-host");
    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.navigation.screenChanged);
    EXPECT_EQ(state.addHostDraft.validationMessage, "That host is already saved");

    state.menu.select_item_by_id("cancel-add-host");
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, SelectingAnUnpairedHostStartsPairing) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::not_paired},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_TRUE(update.requests.pairingRequested);
    EXPECT_EQ(update.requests.pairingAddress, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(update.requests.pairingPort, app::DEFAULT_HOST_PORT);
    EXPECT_TRUE(app::is_valid_pairing_pin(update.requests.pairingPin));
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::pair_host);
    EXPECT_FALSE(state.hosts.loaded);
    EXPECT_TRUE(state.hosts.empty());
    EXPECT_TRUE(state.hosts.activeLoaded);
  }

  TEST(ClientStateTest, SelectingAnOfflineUnpairedHostDoesNotOpenThePairingScreen) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::not_paired, app::HostReachability::offline},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.navigation.screenChanged);
    EXPECT_FALSE(update.requests.pairingRequested);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    EXPECT_EQ(state.shell.statusMessage, "Host is offline. Bring it online before pairing.");
  }

  TEST(ClientStateTest, BackingOutOfThePairingScreenRequestsPairingCancellation) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::not_paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::pair_host);
    ASSERT_TRUE(update.requests.pairingRequested);

    update = app::handle_command(state, input::UiCommand::back);

    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_TRUE(update.requests.pairingCancelledRequested);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, HostGridNavigationMatchesTheRenderedThreeColumnLayout) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Host A", test_support::kTestIpv4Addresses[test_support::kIpHostGridA], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host B", test_support::kTestIpv4Addresses[test_support::kIpHostGridB], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host C", test_support::kTestIpv4Addresses[test_support::kIpHostGridC], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host D", test_support::kTestIpv4Addresses[test_support::kIpHostGridD], 0, app::PairingState::paired, app::HostReachability::online},
                              });

    EXPECT_EQ(state.hosts.selectedHostIndex, 0U);
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.hosts.selectedHostIndex, 1U);
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.hosts.selectedHostIndex, 2U);
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.hosts.selectedHostIndex, 3U);
  }

  TEST(ClientStateTest, HostGridCanMoveDownIntoAPartialNextRowFromAnyColumn) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Host A", test_support::kTestIpv4Addresses[test_support::kIpHostGridA], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host B", test_support::kTestIpv4Addresses[test_support::kIpHostGridB], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host C", test_support::kTestIpv4Addresses[test_support::kIpHostGridC], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host D", test_support::kTestIpv4Addresses[test_support::kIpHostGridD], 0, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.hosts.selectedHostIndex, 1U);
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.hosts.selectedHostIndex, 3U);

    state.hosts.selectedHostIndex = 2U;
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.hosts.selectedHostIndex, 3U);
  }

  TEST(ClientStateTest, HostGridWrapsRightToTheNextRowAndLeftToThePreviousRow) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Host A", test_support::kTestIpv4Addresses[test_support::kIpHostGridA], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host B", test_support::kTestIpv4Addresses[test_support::kIpHostGridB], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host C", test_support::kTestIpv4Addresses[test_support::kIpHostGridC], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host D", test_support::kTestIpv4Addresses[test_support::kIpHostGridD], 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host E", test_support::kTestIpv4Addresses[test_support::kIpHostGridE], 0, app::PairingState::paired, app::HostReachability::online},
                              });

    state.hosts.selectedHostIndex = 2U;
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.hosts.selectedHostIndex, 3U);

    app::handle_command(state, input::UiCommand::move_left);
    EXPECT_EQ(state.hosts.selectedHostIndex, 2U);
  }

  TEST(ClientStateTest, SelectingAPairedHostOpensTheAppsScreen) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.navigation.screenChanged);
    EXPECT_TRUE(update.requests.appsBrowseRequested);
    EXPECT_FALSE(update.requests.appsBrowseShowHidden);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, EnteringTheAppsScreenUnloadsTheHostsPageData) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));

    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::apps);
    EXPECT_FALSE(state.hosts.loaded);
    EXPECT_TRUE(state.hosts.empty());
    EXPECT_TRUE(state.hosts.activeLoaded);
    EXPECT_EQ(state.hosts.active.address, test_support::kTestIpv4Addresses[test_support::kIpOffice]);
  }

  TEST(ClientStateTest, SelectingAnOfflinePairedHostDoesNotOpenTheAppsScreen) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::offline},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.navigation.screenChanged);
    EXPECT_TRUE(update.requests.appsBrowseRequested);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    EXPECT_TRUE(state.shell.statusMessage.empty());
  }

  TEST(ClientStateTest, AppliesFetchedAppListsAndPreservesPerAppFlags) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    state.hosts.active.runningGameId = 101;
    state.hosts.active.apps = {
      {"Steam", 101, false, true, true, "cached-steam", true, false},
    };

    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], {
                                                                                                                                                             {"Steam", 101, true, false, false, "cached-steam", false, false},
                                                                                                                                                             {"Desktop", 102, false, false, false, "cached-desktop", true, false},
                                                                                                                                                           },
                               0x55AAU,
                               true,
                               "Loaded 2 app(s)");

    ASSERT_EQ(state.hosts.active.apps.size(), 2U);
    EXPECT_EQ(state.hosts.active.appListState, app::HostAppListState::ready);
    EXPECT_TRUE(state.hosts.active.apps[0].hidden);
    EXPECT_TRUE(state.hosts.active.apps[0].favorite);
    EXPECT_TRUE(state.hosts.active.apps[0].boxArtCached);
    EXPECT_TRUE(state.hosts.active.apps[0].running);
    EXPECT_TRUE(state.hosts.active.apps[1].boxArtCached);
    EXPECT_TRUE(state.shell.statusMessage.empty());
  }

  TEST(ClientStateTest, AppliesFetchedAppListsWhenBackgroundTasksReportTheResolvedHttpPort) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    state.hosts.front().resolvedHttpPort = test_support::kTestPorts[test_support::kPortResolvedHttp];
    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));

    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortResolvedHttp], {
                                                                                                                                                              {"Steam", 101, true, false, false, "steam-cover", true, false},
                                                                                                                                                            },
                               0xBEEFU,
                               true,
                               "Loaded 1 app(s)");

    ASSERT_EQ(state.hosts.active.apps.size(), 1U);
    EXPECT_EQ(state.hosts.active.appListState, app::HostAppListState::ready);
    EXPECT_EQ(state.hosts.active.apps.front().name, "Steam");
    EXPECT_TRUE(state.shell.statusMessage.empty());
  }

  TEST(ClientStateTest, MarksCoverArtCachedWhenBackgroundTasksReportTheResolvedHttpsPort) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    state.hosts.active.httpsPort = test_support::kTestPorts[test_support::kPortResolvedHttps];
    state.hosts.active.apps = {
      {"Steam", 101, true, false, false, "steam-cover", false, false},
    };

    app::mark_cover_art_cached(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortResolvedHttps], 101);

    ASSERT_EQ(state.hosts.active.apps.size(), 1U);
    EXPECT_TRUE(state.hosts.active.apps.front().boxArtCached);
  }

  TEST(ClientStateTest, SuccessfulAppListRefreshMarksHostsDirtyForPersistence) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_FALSE(state.hosts.dirty);
    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));

    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], {
                                                                                                                                                             {"Steam", 101, true, false, false, "steam-cover", true, false},
                                                                                                                                                           },
                               0xACEDU,
                               true,
                               "Loaded 1 app(s)");

    EXPECT_TRUE(state.hosts.dirty);
    ASSERT_EQ(state.hosts.active.apps.size(), 1U);
    EXPECT_EQ(state.hosts.active.appListContentHash, 0xACEDU);
  }

  TEST(ClientStateTest, FailedRefreshKeepsCachedAppsAvailable) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    state.hosts.active.apps = {
      {"Steam", 101, false, false, false, "cached-steam", true, false},
    };
    state.hosts.active.appListContentHash = 0x1234U;

    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], {}, 0, false, "Timed out while refreshing apps");

    EXPECT_EQ(state.hosts.active.appListState, app::HostAppListState::ready);
    ASSERT_EQ(state.hosts.active.apps.size(), 1U);
    EXPECT_EQ(state.hosts.active.apps.front().name, "Steam");
    EXPECT_EQ(state.shell.statusMessage, "Timed out while refreshing apps");
  }

  TEST(ClientStateTest, LeavingTheAppsScreenUnloadsTheInMemoryAppList) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    state.hosts.active.apps = {
      {"Steam", 101, false, false, false, "cached-steam", true, false},
    };
    state.hosts.active.appListState = app::HostAppListState::ready;

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::back);

    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    EXPECT_TRUE(state.hosts.active.apps.empty());
    EXPECT_EQ(state.hosts.active.appListState, app::HostAppListState::idle);
  }

  TEST(ClientStateTest, ExplicitUnpairedAppListFailureMarksTheHostAsNotPaired) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);

    app::apply_app_list_result(
      state,
      test_support::kTestIpv4Addresses[test_support::kIpOffice],
      test_support::kTestPorts[test_support::kPortDefaultHost],
      {},
      0,
      false,
      "The host reports that this client is no longer paired. Pair the host again."
    );

    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::not_paired);
    EXPECT_EQ(state.hosts.front().appListState, app::HostAppListState::failed);
  }

  TEST(ClientStateTest, ExplicitUnpairedAppListFailureClearsCachedApps) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.hosts.front().apps = {
      {"Steam", 101, false, false, false, "cached-steam", true, false},
    };
    state.hosts.front().appListContentHash = 0x1234U;

    app::apply_app_list_result(
      state,
      test_support::kTestIpv4Addresses[test_support::kIpOffice],
      test_support::kTestPorts[test_support::kPortDefaultHost],
      {},
      0,
      false,
      "The client is not authorized. Certificate verification failed."
    );

    EXPECT_TRUE(state.hosts.front().apps.empty());
    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::not_paired);
    EXPECT_EQ(state.hosts.front().appListState, app::HostAppListState::failed);
  }

  TEST(ClientStateTest, TransientAppListFailuresDoNotMarkTheHostAsNotPaired) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);

    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], {}, 0, false, "Timed out while refreshing apps");

    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::paired);
  }

  TEST(ClientStateTest, AppGridWrapsHorizontallyAndFindsTheClosestItemInPartialRows) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], {
                                                                                                                                                             {"App 1", 1, false, false, false, "app-1", false, false},
                                                                                                                                                             {"App 2", 2, false, false, false, "app-2", false, false},
                                                                                                                                                             {"App 3", 3, false, false, false, "app-3", false, false},
                                                                                                                                                             {"App 4", 4, false, false, false, "app-4", false, false},
                                                                                                                                                             {"App 5", 5, false, false, false, "app-5", false, false},
                                                                                                                                                           },
                               0x99U,
                               true,
                               "Loaded 5 app(s)");

    state.apps.selectedAppIndex = 3U;
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.apps.selectedAppIndex, 4U);

    app::handle_command(state, input::UiCommand::move_left);
    EXPECT_EQ(state.apps.selectedAppIndex, 3U);

    state.apps.selectedAppIndex = 2U;
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.apps.selectedAppIndex, 4U);
  }

  TEST(ClientStateTest, LogViewerCanScrollAndCyclePlacement) {
    app::ClientState state = app::create_initial_state();
    app::apply_log_viewer_contents(state, {"line-1", "line-2", "line-3", "line-4"}, "Loaded log file preview");

    ASSERT_EQ(state.modal.id, app::ModalId::log_viewer);
    EXPECT_EQ(state.shell.statusMessage, "Loaded log file preview");
    EXPECT_EQ(state.settings.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_EQ(state.settings.logViewerScrollOffset, 0U);

    app::handle_command(state, input::UiCommand::previous_page);
    EXPECT_EQ(state.settings.logViewerScrollOffset, 1U);

    app::handle_command(state, input::UiCommand::fast_previous_page);
    EXPECT_EQ(state.settings.logViewerScrollOffset, 3U);

    app::handle_command(state, input::UiCommand::next_page);
    EXPECT_EQ(state.settings.logViewerScrollOffset, 2U);

    app::handle_command(state, input::UiCommand::delete_character);
    EXPECT_EQ(state.settings.logViewerPlacement, app::LogViewerPlacement::left);

    app::handle_command(state, input::UiCommand::open_context_menu);
    EXPECT_EQ(state.settings.logViewerPlacement, app::LogViewerPlacement::right);

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.navigation.modalClosed);
    EXPECT_EQ(state.modal.id, app::ModalId::none);
  }

  TEST(ClientStateTest, LeavingTheAppsScreenClearsTransientAppStatusAndIgnoresLaterRefreshText) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::apps);
    EXPECT_TRUE(state.shell.statusMessage.empty());

    app::AppUpdate update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.navigation.screenChanged);
    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    EXPECT_TRUE(state.shell.statusMessage.empty());

    app::apply_app_list_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], {
                                                                                                                                                             {"Steam", 101, false, false, false, "steam-cover", false, false},
                                                                                                                                                           },
                               0,
                               false,
                               "The host applist response did not contain any app entries");

    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    EXPECT_TRUE(state.shell.statusMessage.empty());
    EXPECT_EQ(state.hosts.active.appListState, app::HostAppListState::idle);
    EXPECT_TRUE(state.hosts.active.appListStatusMessage.empty());
  }

  TEST(ClientStateTest, SettingsCanRequestDeletionOfSavedFilesFromTheResetCategory) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::settings);

    app::replace_saved_files(state, {
                                      {"E:\\UDATA\\12345678\\moonlight.log", "moonlight.log", 128U},
                                    });
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.settings.selectedCategory, app::SettingsCategory::reset);
    ASSERT_EQ(state.settings.focusArea, app::SettingsFocusArea::options);
    ASSERT_TRUE(state.detailMenu.select_item_by_id("delete-saved-file:E:\\UDATA\\12345678\\moonlight.log"));

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.navigation.modalOpened);
    EXPECT_EQ(state.modal.id, app::ModalId::confirmation);

    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.persistence.savedFileDeleteRequested);
    EXPECT_EQ(update.persistence.savedFileDeletePath, "E:\\UDATA\\12345678\\moonlight.log");
  }

  TEST(ClientStateTest, SettingsFactoryResetUsesAConfirmationDialog) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.shell.activeScreen, app::ScreenId::settings);

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.settings.selectedCategory, app::SettingsCategory::reset);
    ASSERT_EQ(state.settings.focusArea, app::SettingsFocusArea::options);

    const app::AppUpdate openUpdate = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(openUpdate.navigation.modalOpened);
    ASSERT_EQ(state.modal.id, app::ModalId::confirmation);

    const app::AppUpdate confirmUpdate = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(confirmUpdate.persistence.factoryResetRequested);
  }

  TEST(ClientStateTest, HostContextMenuCanDeleteTheSelectedHost) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::not_paired},
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::open_context_menu);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.persistence.hostsChanged);
    ASSERT_EQ(state.hosts.size(), 1U);
    EXPECT_EQ(state.hosts.front().displayName, "Office PC");
  }

  TEST(ClientStateTest, DeletingAPairedHostRequestsPersistentCleanupAndMarksItForManualRePairing) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });
    state.hosts.front().apps = {
      {"Steam", 101, false, false, false, "steam-cover", true, false},
      {"Desktop", 102, false, false, false, "desktop-cover", true, false},
      {"Duplicate", 103, false, false, false, "steam-cover", true, false},
    };
    state.hosts.front().appListState = app::HostAppListState::ready;

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::open_context_menu);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.persistence.hostsChanged);
    EXPECT_TRUE(update.persistence.hostDeleteCleanupRequested);
    EXPECT_TRUE(update.persistence.deletedHostWasPaired);
    EXPECT_EQ(update.persistence.deletedHostAddress, test_support::kTestIpv4Addresses[test_support::kIpOffice]);
    EXPECT_EQ(update.persistence.deletedHostPort, test_support::kTestPorts[test_support::kPortDefaultHost]);
    ASSERT_EQ(update.persistence.deletedHostCoverArtCacheKeys.size(), 2U);
    EXPECT_EQ(update.persistence.deletedHostCoverArtCacheKeys[0], "steam-cover");
    EXPECT_EQ(update.persistence.deletedHostCoverArtCacheKeys[1], "desktop-cover");
    EXPECT_TRUE(state.hosts.empty());
    EXPECT_TRUE(app::host_requires_manual_pairing(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost]));
  }

  TEST(ClientStateTest, SuccessfulRePairingClearsTheManualRePairRequirementAfterHostDeletion) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::open_context_menu);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate deleteUpdate = app::handle_command(state, input::UiCommand::activate);

    ASSERT_TRUE(deleteUpdate.persistence.hostDeleteCleanupRequested);
    ASSERT_TRUE(app::host_requires_manual_pairing(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost]));

    app::replace_hosts(state, {
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::not_paired, app::HostReachability::online},
                              });

    EXPECT_TRUE(app::apply_pairing_result(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], true, "Paired successfully"));
    EXPECT_FALSE(app::host_requires_manual_pairing(state, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost]));
    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::paired);
  }

  TEST(ClientStateTest, RequestsAConnectionTestFromTheAddHostScreen) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = test_support::kTestIpv4Addresses[test_support::kIpHostGridA];
    state.addHostDraft.portInput = "48000";
    state.menu.select_item_by_id("test-connection");

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.requests.connectionTestRequested);
    EXPECT_EQ(update.requests.connectionTestAddress, test_support::kTestIpv4Addresses[test_support::kIpHostGridA]);
    EXPECT_EQ(update.requests.connectionTestPort, test_support::kTestPorts[test_support::kPortDefaultHost]);
    EXPECT_EQ(state.shell.statusMessage, "Testing connection to " + std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]) + ":48000...");
  }

  TEST(ClientStateTest, StagesCancelsDeletesAndAcceptsAddHostKeypadEdits) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = test_support::kTestIpv4Addresses[test_support::kIpHostGridA];

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_FALSE(update.navigation.screenChanged);
    EXPECT_TRUE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 0U);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, test_support::kTestIpv4Addresses[test_support::kIpHostGridA]);

    app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]) + "1");

    app::handle_command(state, input::UiCommand::delete_character);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, test_support::kTestIpv4Addresses[test_support::kIpHostGridA]);

    app::handle_command(state, input::UiCommand::back);
    EXPECT_FALSE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.addressInput, test_support::kTestIpv4Addresses[test_support::kIpHostGridA]);

    app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(state.addHostDraft.keypad.visible);
    app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]) + "1");
    app::handle_command(state, input::UiCommand::confirm);
    EXPECT_FALSE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.addressInput, std::string(test_support::kTestIpv4Addresses[test_support::kIpHostGridA]) + "1");
  }

  TEST(ClientStateTest, AddHostKeypadWrapsHorizontallyAcrossRowEdges) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::activate);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_TRUE(state.addHostDraft.keypad.visible);

    state.addHostDraft.keypad.selectedButtonIndex = 2U;
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 0U);

    app::handle_command(state, input::UiCommand::move_left);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 2U);

    state.addHostDraft.keypad.selectedButtonIndex = 10U;
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 9U);

    app::handle_command(state, input::UiCommand::move_left);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 10U);
  }

  TEST(ClientStateTest, AddHostKeypadWrapsVerticallyAcrossTopAndBottomRows) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::activate);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_TRUE(state.addHostDraft.keypad.visible);

    state.addHostDraft.keypad.selectedButtonIndex = 1U;
    app::handle_command(state, input::UiCommand::move_up);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 10U);

    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 1U);

    state.addHostDraft.keypad.selectedButtonIndex = 8U;
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 10U);

    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 1U);
  }

  TEST(ClientStateTest, SuccessfulPairingReturnsToHostsAndKeepsTheHostSelected) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::not_paired, app::HostReachability::online},
                                {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::paired, app::HostReachability::online},
                              });

    state.hosts.selectedHostIndex = 1U;
    EXPECT_TRUE(app::apply_pairing_result(state, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], app::DEFAULT_HOST_PORT, true, "Paired successfully"));

    EXPECT_EQ(state.shell.activeScreen, app::ScreenId::hosts);
    EXPECT_EQ(state.hosts.selectedHostIndex, 0U);
    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::paired);
  }

}  // namespace
