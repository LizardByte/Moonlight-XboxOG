// class header include
#include "src/app/client_state.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(ClientStateTest, StartsOnTheHostsScreenWithTheToolbarSelected) {
    const app::ClientState state = app::create_initial_state();

    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    EXPECT_EQ(state.hostsFocusArea, app::HostsFocusArea::toolbar);
    EXPECT_EQ(state.selectedToolbarButtonIndex, 2U);
    EXPECT_FALSE(state.overlayVisible);
    EXPECT_FALSE(state.shouldExit);
    EXPECT_FALSE(state.hostsDirty);
    EXPECT_TRUE(state.menu.items().empty());
  }

  TEST(ClientStateTest, ReplacesHostsFromPersistenceWithoutMarkingThemDirty) {
    app::ClientState state = app::create_initial_state();

    app::replace_hosts(state, {
                                {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired},
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired},
                              },
                       "Loaded 2 saved host(s)");

    ASSERT_EQ(state.hosts.size(), 2U);
    EXPECT_FALSE(state.hostsDirty);
    EXPECT_EQ(state.statusMessage, "Loaded 2 saved host(s)");
    EXPECT_EQ(state.hostsFocusArea, app::HostsFocusArea::grid);
    EXPECT_EQ(state.selectedHostIndex, 0U);
  }

  TEST(ClientStateTest, HostsToolbarCanOpenSettingsAndAddHost) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(update.activatedItemId, "settings-button");
    EXPECT_EQ(state.activeScreen, app::ScreenId::settings);

    update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);

    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(update.activatedItemId, "add-host-button");
    EXPECT_EQ(state.activeScreen, app::ScreenId::add_host);
  }

  TEST(ClientStateTest, SettingsCanRequestLogViewingAndCycleLoggingLevel) {
    app::ClientState state = app::create_initial_state();

    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::settings);

    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.selectedSettingsCategory, app::SettingsCategory::logging);
    EXPECT_EQ(state.settingsFocusArea, app::SettingsFocusArea::options);

    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.logViewRequested);
    EXPECT_EQ(update.activatedItemId, "view-log-file");

    app::handle_command(state, input::UiCommand::move_down);
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_FALSE(update.logViewRequested);
    EXPECT_EQ(state.loggingLevel, logging::LogLevel::debug);
    EXPECT_EQ(state.statusMessage, "Logging level set to DEBUG");
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
    EXPECT_GT(state.overlayScrollOffset, 0U);

    update = app::handle_command(state, input::UiCommand::next_page);
    EXPECT_TRUE(update.overlayChanged);
    EXPECT_EQ(state.overlayScrollOffset, 0U);
  }

  TEST(ClientStateTest, BackFromHostsDoesNotRequestShutdown) {
    app::ClientState state = app::create_initial_state();

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::back);

    EXPECT_FALSE(update.exitRequested);
    EXPECT_FALSE(state.shouldExit);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, CanSaveAManualHostEntryWithACustomPort) {
    app::ClientState state = app::create_initial_state();

    state.hostsFocusArea = app::HostsFocusArea::toolbar;
    state.selectedToolbarButtonIndex = 2U;
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
    EXPECT_EQ(state.selectedHostIndex, 0U);
  }

  TEST(ClientStateTest, RejectsDuplicateHostEntriesAndAllowsCancellationBackToHosts) {
    app::ClientState state = app::create_initial_state();

    state.hostsFocusArea = app::HostsFocusArea::toolbar;
    state.selectedToolbarButtonIndex = 2U;
    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = "192.168.0.10";
    state.menu.select_item_by_id("save-host");
    app::handle_command(state, input::UiCommand::activate);

    ASSERT_EQ(state.hosts.size(), 1U);
    ASSERT_EQ(state.activeScreen, app::ScreenId::hosts);

    state.hostsFocusArea = app::HostsFocusArea::toolbar;
    state.selectedToolbarButtonIndex = 2U;
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    ASSERT_EQ(state.activeScreen, app::ScreenId::add_host);

    state.addHostDraft.addressInput = "192.168.0.10";
    state.menu.select_item_by_id("save-host");
    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.screenChanged);
    EXPECT_EQ(state.addHostDraft.validationMessage, "That host is already saved");

    state.menu.select_item_by_id("cancel-add-host");
    update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, SelectingAnUnpairedHostStartsPairing) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.screenChanged);
    EXPECT_TRUE(update.pairingRequested);
    EXPECT_EQ(update.pairingAddress, "192.168.1.20");
    EXPECT_EQ(update.pairingPort, app::DEFAULT_HOST_PORT);
    EXPECT_TRUE(app::is_valid_pairing_pin(update.pairingPin));
    EXPECT_EQ(state.activeScreen, app::ScreenId::pair_host);
  }

  TEST(ClientStateTest, SelectingAnOfflineUnpairedHostDoesNotOpenThePairingScreen) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired, app::HostReachability::offline},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.screenChanged);
    EXPECT_FALSE(update.pairingRequested);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    EXPECT_EQ(state.statusMessage, "Host is offline. Bring it online before pairing.");
  }

  TEST(ClientStateTest, BackingOutOfThePairingScreenRequestsPairingCancellation) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::pair_host);
    ASSERT_TRUE(update.pairingRequested);

    update = app::handle_command(state, input::UiCommand::back);

    EXPECT_TRUE(update.screenChanged);
    EXPECT_TRUE(update.pairingCancelledRequested);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, HostGridNavigationMatchesTheRenderedThreeColumnLayout) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Host A", "192.168.0.10", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host B", "192.168.0.11", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host C", "192.168.0.12", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host D", "192.168.0.13", 0, app::PairingState::paired, app::HostReachability::online},
                              });

    EXPECT_EQ(state.selectedHostIndex, 0U);
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.selectedHostIndex, 1U);
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.selectedHostIndex, 2U);
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.selectedHostIndex, 3U);
  }

  TEST(ClientStateTest, HostGridCanMoveDownIntoAPartialNextRowFromAnyColumn) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Host A", "192.168.0.10", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host B", "192.168.0.11", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host C", "192.168.0.12", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host D", "192.168.0.13", 0, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.selectedHostIndex, 1U);
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.selectedHostIndex, 3U);

    state.selectedHostIndex = 2U;
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.selectedHostIndex, 3U);
  }

  TEST(ClientStateTest, HostGridWrapsRightToTheNextRowAndLeftToThePreviousRow) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Host A", "192.168.0.10", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host B", "192.168.0.11", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host C", "192.168.0.12", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host D", "192.168.0.13", 0, app::PairingState::paired, app::HostReachability::online},
                                {"Host E", "192.168.0.14", 0, app::PairingState::paired, app::HostReachability::online},
                              });

    state.selectedHostIndex = 2U;
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.selectedHostIndex, 3U);

    app::handle_command(state, input::UiCommand::move_left);
    EXPECT_EQ(state.selectedHostIndex, 2U);
  }

  TEST(ClientStateTest, SelectingAPairedHostOpensTheAppsScreen) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.screenChanged);
    EXPECT_TRUE(update.appsBrowseRequested);
    EXPECT_FALSE(update.appsBrowseShowHidden);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
  }

  TEST(ClientStateTest, SelectingAnOfflinePairedHostDoesNotOpenTheAppsScreen) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::offline},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_FALSE(update.screenChanged);
    EXPECT_TRUE(update.appsBrowseRequested);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    EXPECT_TRUE(state.statusMessage.empty());
  }

  TEST(ClientStateTest, AppliesFetchedAppListsAndPreservesPerAppFlags) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.hosts.front().runningGameId = 101;
    state.hosts.front().apps = {
      {"Steam", 101, false, true, true, "cached-steam", true, false},
    };

    app::apply_app_list_result(state, "10.0.0.25", 48000, {
                                                            {"Steam", 101, true, false, false, "cached-steam", false, false},
                                                            {"Desktop", 102, false, false, false, "cached-desktop", true, false},
                                                          },
                               0x55AAU,
                               true,
                               "Loaded 2 Sunshine app(s)");

    ASSERT_EQ(state.hosts.front().apps.size(), 2U);
    EXPECT_EQ(state.hosts.front().appListState, app::HostAppListState::ready);
    EXPECT_TRUE(state.hosts.front().apps[0].hidden);
    EXPECT_TRUE(state.hosts.front().apps[0].favorite);
    EXPECT_TRUE(state.hosts.front().apps[0].boxArtCached);
    EXPECT_TRUE(state.hosts.front().apps[0].running);
    EXPECT_TRUE(state.hosts.front().apps[1].boxArtCached);
    EXPECT_TRUE(state.statusMessage.empty());
  }

  TEST(ClientStateTest, AppliesFetchedAppListsWhenBackgroundTasksReportTheResolvedHttpPort) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    state.hosts.front().resolvedHttpPort = 47989;
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);

    app::apply_app_list_result(state, "10.0.0.25", 47989, {
                                                            {"Steam", 101, true, false, false, "steam-cover", true, false},
                                                          },
                               0xBEEFU,
                               true,
                               "Loaded 1 Sunshine app(s)");

    ASSERT_EQ(state.hosts.front().apps.size(), 1U);
    EXPECT_EQ(state.hosts.front().appListState, app::HostAppListState::ready);
    EXPECT_EQ(state.hosts.front().apps.front().name, "Steam");
    EXPECT_TRUE(state.statusMessage.empty());
  }

  TEST(ClientStateTest, MarksCoverArtCachedWhenBackgroundTasksReportTheResolvedHttpsPort) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    state.hosts.front().httpsPort = 47990;
    state.hosts.front().apps = {
      {"Steam", 101, true, false, false, "steam-cover", false, false},
    };

    app::mark_cover_art_cached(state, "10.0.0.25", 47990, 101);

    ASSERT_EQ(state.hosts.front().apps.size(), 1U);
    EXPECT_TRUE(state.hosts.front().apps.front().boxArtCached);
  }

  TEST(ClientStateTest, FailedRefreshKeepsCachedAppsAvailable) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    state.hosts.front().apps = {
      {"Steam", 101, false, false, false, "cached-steam", true, false},
    };
    state.hosts.front().appListContentHash = 0x1234U;

    app::apply_app_list_result(state, "10.0.0.25", 48000, {}, 0, false, "Timed out while refreshing apps");

    EXPECT_EQ(state.hosts.front().appListState, app::HostAppListState::ready);
    ASSERT_EQ(state.hosts.front().apps.size(), 1U);
    EXPECT_EQ(state.hosts.front().apps.front().name, "Steam");
    EXPECT_EQ(state.statusMessage, "Timed out while refreshing apps");
  }

  TEST(ClientStateTest, ExplicitUnpairedAppListFailureMarksTheHostAsNotPaired) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);

    app::apply_app_list_result(
      state,
      "10.0.0.25",
      48000,
      {},
      0,
      false,
      "The host reports that this client is no longer paired. Pair the host again from Sunshine."
    );

    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::not_paired);
    EXPECT_EQ(state.hosts.front().appListState, app::HostAppListState::failed);
  }

  TEST(ClientStateTest, ExplicitUnpairedAppListFailureClearsCachedApps) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    state.hosts.front().apps = {
      {"Steam", 101, false, false, false, "cached-steam", true, false},
    };
    state.hosts.front().appListContentHash = 0x1234U;

    app::apply_app_list_result(
      state,
      "10.0.0.25",
      48000,
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
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);

    app::apply_app_list_result(state, "10.0.0.25", 48000, {}, 0, false, "Timed out while refreshing apps");

    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::paired);
  }

  TEST(ClientStateTest, AppGridWrapsHorizontallyAndFindsTheClosestItemInPartialRows) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    app::apply_app_list_result(state, "10.0.0.25", 48000, {
                                                            {"App 1", 1, false, false, false, "app-1", false, false},
                                                            {"App 2", 2, false, false, false, "app-2", false, false},
                                                            {"App 3", 3, false, false, false, "app-3", false, false},
                                                            {"App 4", 4, false, false, false, "app-4", false, false},
                                                            {"App 5", 5, false, false, false, "app-5", false, false},
                                                          },
                               0x99U,
                               true,
                               "Loaded 5 Sunshine app(s)");

    state.selectedAppIndex = 3U;
    app::handle_command(state, input::UiCommand::move_right);
    EXPECT_EQ(state.selectedAppIndex, 4U);

    app::handle_command(state, input::UiCommand::move_left);
    EXPECT_EQ(state.selectedAppIndex, 3U);

    state.selectedAppIndex = 2U;
    app::handle_command(state, input::UiCommand::move_down);
    EXPECT_EQ(state.selectedAppIndex, 4U);
  }

  TEST(ClientStateTest, LogViewerCanScrollAndCyclePlacement) {
    app::ClientState state = app::create_initial_state();
    app::apply_log_viewer_contents(state, {"line-1", "line-2", "line-3", "line-4"}, "Loaded log file preview");

    ASSERT_EQ(state.modal.id, app::ModalId::log_viewer);
    EXPECT_EQ(state.statusMessage, "Loaded log file preview");
    EXPECT_EQ(state.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_EQ(state.logViewerScrollOffset, 0U);

    app::handle_command(state, input::UiCommand::previous_page);
    EXPECT_EQ(state.logViewerScrollOffset, 1U);

    app::handle_command(state, input::UiCommand::fast_previous_page);
    EXPECT_EQ(state.logViewerScrollOffset, 3U);

    app::handle_command(state, input::UiCommand::next_page);
    EXPECT_EQ(state.logViewerScrollOffset, 2U);

    app::handle_command(state, input::UiCommand::delete_character);
    EXPECT_EQ(state.logViewerPlacement, app::LogViewerPlacement::left);

    app::handle_command(state, input::UiCommand::open_context_menu);
    EXPECT_EQ(state.logViewerPlacement, app::LogViewerPlacement::right);

    const app::AppUpdate update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.modalClosed);
    EXPECT_EQ(state.modal.id, app::ModalId::none);
  }

  TEST(ClientStateTest, LeavingTheAppsScreenClearsTransientAppStatusAndIgnoresLaterRefreshText) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    ASSERT_TRUE(app::begin_selected_host_app_browse(state, false));
    ASSERT_EQ(state.activeScreen, app::ScreenId::apps);
    EXPECT_TRUE(state.statusMessage.empty());

    app::AppUpdate update = app::handle_command(state, input::UiCommand::back);
    EXPECT_TRUE(update.screenChanged);
    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    EXPECT_TRUE(state.statusMessage.empty());

    app::apply_app_list_result(state, "10.0.0.25", 48000, {
                                                            {"Steam", 101, false, false, false, "steam-cover", false, false},
                                                          },
                               0,
                               false,
                               "The host applist response did not contain any app entries");

    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    EXPECT_TRUE(state.statusMessage.empty());
    EXPECT_EQ(state.hosts.front().appListState, app::HostAppListState::failed);
    EXPECT_EQ(state.hosts.front().appListStatusMessage, "The host applist response did not contain any app entries");
  }

  TEST(ClientStateTest, SettingsCanRequestDeletionOfSavedFilesFromTheResetCategory) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::settings);

    app::replace_saved_files(state, {
                                      {"E:\\UDATA\\12345678\\moonlight.log", "moonlight.log", 128U},
                                    });
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.selectedSettingsCategory, app::SettingsCategory::reset);
    ASSERT_EQ(state.settingsFocusArea, app::SettingsFocusArea::options);
    ASSERT_TRUE(state.detailMenu.select_item_by_id("delete-saved-file:E:\\UDATA\\12345678\\moonlight.log"));

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.modalOpened);
    EXPECT_EQ(state.modal.id, app::ModalId::confirmation);

    update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.savedFileDeleteRequested);
    EXPECT_EQ(update.savedFileDeletePath, "E:\\UDATA\\12345678\\moonlight.log");
  }

  TEST(ClientStateTest, SettingsFactoryResetUsesAConfirmationDialog) {
    app::ClientState state = app::create_initial_state();
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::move_left);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.activeScreen, app::ScreenId::settings);

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::activate);
    ASSERT_EQ(state.selectedSettingsCategory, app::SettingsCategory::reset);
    ASSERT_EQ(state.settingsFocusArea, app::SettingsFocusArea::options);

    const app::AppUpdate openUpdate = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(openUpdate.modalOpened);
    ASSERT_EQ(state.modal.id, app::ModalId::confirmation);

    const app::AppUpdate confirmUpdate = app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(confirmUpdate.factoryResetRequested);
  }

  TEST(ClientStateTest, HostContextMenuCanDeleteTheSelectedHost) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired},
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::open_context_menu);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);

    EXPECT_TRUE(update.hostsChanged);
    ASSERT_EQ(state.hosts.size(), 1U);
    EXPECT_EQ(state.hosts.front().displayName, "Office PC");
  }

  TEST(ClientStateTest, DeletingAPairedHostRequestsPersistentCleanupAndMarksItForManualRePairing) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
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

    EXPECT_TRUE(update.hostsChanged);
    EXPECT_TRUE(update.hostDeleteCleanupRequested);
    EXPECT_TRUE(update.deletedHostWasPaired);
    EXPECT_EQ(update.deletedHostAddress, "10.0.0.25");
    EXPECT_EQ(update.deletedHostPort, 48000);
    ASSERT_EQ(update.deletedHostCoverArtCacheKeys.size(), 2U);
    EXPECT_EQ(update.deletedHostCoverArtCacheKeys[0], "steam-cover");
    EXPECT_EQ(update.deletedHostCoverArtCacheKeys[1], "desktop-cover");
    EXPECT_TRUE(state.hosts.empty());
    EXPECT_TRUE(app::host_requires_manual_pairing(state, "10.0.0.25", 48000));
  }

  TEST(ClientStateTest, SuccessfulRePairingClearsTheManualRePairRequirementAfterHostDeletion) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::open_context_menu);
    app::handle_command(state, input::UiCommand::move_down);
    app::handle_command(state, input::UiCommand::move_down);
    const app::AppUpdate deleteUpdate = app::handle_command(state, input::UiCommand::activate);

    ASSERT_TRUE(deleteUpdate.hostDeleteCleanupRequested);
    ASSERT_TRUE(app::host_requires_manual_pairing(state, "10.0.0.25", 48000));

    app::replace_hosts(state, {
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::not_paired, app::HostReachability::online},
                              });

    EXPECT_TRUE(app::apply_pairing_result(state, "10.0.0.25", 48000, true, "Paired successfully"));
    EXPECT_FALSE(app::host_requires_manual_pairing(state, "10.0.0.25", 48000));
    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::paired);
  }

  TEST(ClientStateTest, RequestsAConnectionTestFromTheAddHostScreen) {
    app::ClientState state = app::create_initial_state();

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

    app::handle_command(state, input::UiCommand::activate);
    state.addHostDraft.addressInput = "192.168.0.10";

    app::AppUpdate update = app::handle_command(state, input::UiCommand::activate);
    EXPECT_FALSE(update.screenChanged);
    EXPECT_TRUE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.keypad.selectedButtonIndex, 0U);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.10");

    app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.101");

    app::handle_command(state, input::UiCommand::delete_character);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.10");

    app::handle_command(state, input::UiCommand::back);
    EXPECT_FALSE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.addressInput, "192.168.0.10");

    app::handle_command(state, input::UiCommand::activate);
    EXPECT_TRUE(state.addHostDraft.keypad.visible);
    app::handle_command(state, input::UiCommand::activate);
    EXPECT_EQ(state.addHostDraft.keypad.stagedInput, "192.168.0.101");
    app::handle_command(state, input::UiCommand::confirm);
    EXPECT_FALSE(state.addHostDraft.keypad.visible);
    EXPECT_EQ(state.addHostDraft.addressInput, "192.168.0.101");
  }

  TEST(ClientStateTest, SuccessfulPairingReturnsToHostsAndKeepsTheHostSelected) {
    app::ClientState state = app::create_initial_state();
    app::replace_hosts(state, {
                                {"Living Room PC", "192.168.1.20", 0, app::PairingState::not_paired, app::HostReachability::online},
                                {"Office PC", "10.0.0.25", 48000, app::PairingState::paired, app::HostReachability::online},
                              });

    state.selectedHostIndex = 1U;
    EXPECT_TRUE(app::apply_pairing_result(state, "192.168.1.20", app::DEFAULT_HOST_PORT, true, "Paired successfully"));

    EXPECT_EQ(state.activeScreen, app::ScreenId::hosts);
    EXPECT_EQ(state.selectedHostIndex, 0U);
    EXPECT_EQ(state.hosts.front().pairingState, app::PairingState::paired);
  }

}  // namespace
