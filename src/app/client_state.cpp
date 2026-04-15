/**
 * @file src/app/client_state.cpp
 * @brief Implements client state models and transitions.
 */
// class header include
#include "src/app/client_state.h"

// standard includes
#include "src/network/host_pairing.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  constexpr std::size_t OVERLAY_SCROLL_STEP = 4U;
  constexpr std::size_t LOG_VIEWER_SCROLL_STEP = 1U;
  constexpr std::size_t LOG_VIEWER_FAST_SCROLL_STEP = 8U;
  constexpr std::size_t HOST_TOOLBAR_BUTTON_COUNT = 3U;
  constexpr std::size_t DEFAULT_EMPTY_HOSTS_TOOLBAR_INDEX = HOST_TOOLBAR_BUTTON_COUNT - 1U;
  constexpr std::size_t HOST_GRID_COLUMN_COUNT = 3U;
  constexpr std::size_t APP_GRID_COLUMN_COUNT = 4U;
  constexpr std::size_t ADD_HOST_KEYPAD_COLUMN_COUNT = 3U;
  constexpr const char *DELETE_SAVED_FILE_MENU_ID_PREFIX = "delete-saved-file:";
  constexpr const char *SETTINGS_CATEGORY_PREFIX = "settings-category:";
  constexpr std::array<char, 11> ADD_HOST_ADDRESS_KEYPAD_CHARACTERS {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0'};
  constexpr std::array<char, 10> ADD_HOST_PORT_KEYPAD_CHARACTERS {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};

  /**
   * @brief Describes the keypad characters available for the active add-host field.
   */
  struct AddHostKeypadLayout {
    const char *characters;  ///< Null-terminated backing storage for the keypad characters.
    std::size_t buttonCount;  ///< Number of selectable keypad buttons in the layout.
  };

  /**
   * @brief Returns the keypad character layout for the active add-host field.
   *
   * @param state Current client state containing the active add-host field.
   * @return The keypad layout that matches the active add-host field.
   */
  AddHostKeypadLayout add_host_keypad_layout(const app::ClientState &state) {
    if (state.addHostDraft.activeField == app::AddHostField::address) {
      return {ADD_HOST_ADDRESS_KEYPAD_CHARACTERS.data(), ADD_HOST_ADDRESS_KEYPAD_CHARACTERS.size()};
    }

    return {ADD_HOST_PORT_KEYPAD_CHARACTERS.data(), ADD_HOST_PORT_KEYPAD_CHARACTERS.size()};
  }

  /**
   * @brief Returns the currently selected keypad character for the active add-host field.
   *
   * @param state Current client state containing the keypad selection.
   * @param character Receives the selected keypad character when one is available.
   * @return True when a keypad character was written to @p character.
   */
  bool selected_add_host_keypad_character(const app::ClientState &state, char *character) {
    const AddHostKeypadLayout layout = add_host_keypad_layout(state);
    if (character == nullptr || layout.buttonCount == 0U) {
      return false;
    }

    *character = layout.characters[state.addHostDraft.keypad.selectedButtonIndex % layout.buttonCount];
    return true;
  }

  std::string add_host_field_menu_id(app::AddHostField field) {
    return field == app::AddHostField::address ? "edit-address" : "edit-port";
  }

  std::string settings_category_menu_id(app::SettingsCategory category) {
    switch (category) {
      case app::SettingsCategory::logging:
        return std::string(SETTINGS_CATEGORY_PREFIX) + "logging";
      case app::SettingsCategory::display:
        return std::string(SETTINGS_CATEGORY_PREFIX) + "display";
      case app::SettingsCategory::input:
        return std::string(SETTINGS_CATEGORY_PREFIX) + "input";
      case app::SettingsCategory::reset:
        return std::string(SETTINGS_CATEGORY_PREFIX) + "reset";
    }

    return std::string(SETTINGS_CATEGORY_PREFIX) + "logging";
  }

  app::SettingsCategory settings_category_from_menu_id(std::string_view itemId) {
    if (itemId == settings_category_menu_id(app::SettingsCategory::display)) {
      return app::SettingsCategory::display;
    }
    if (itemId == settings_category_menu_id(app::SettingsCategory::input)) {
      return app::SettingsCategory::input;
    }
    if (itemId == settings_category_menu_id(app::SettingsCategory::reset)) {
      return app::SettingsCategory::reset;
    }
    return app::SettingsCategory::logging;
  }

  const char *settings_category_description(app::SettingsCategory category) {
    switch (category) {
      case app::SettingsCategory::logging:
        return "Control the runtime log file, the in-app log viewer, and xemu debugger output verbosity.";
      case app::SettingsCategory::display:
        return "Display options will live here when video and layout tuning settings are added.";
      case app::SettingsCategory::input:
        return "Input options will live here when controller and navigation customization is added.";
      case app::SettingsCategory::reset:
        return "Review and delete Moonlight saved data, or remove everything with a full factory reset.";
    }

    return "Control the runtime log file, the in-app log viewer, and xemu debugger output verbosity.";
  }

  std::string pairing_reset_endpoint_key(std::string_view address, uint16_t port) {
    return app::normalize_ipv4_address(address) + ":" + std::to_string(app::effective_host_port(port));
  }

  void remember_deleted_host_pairing(app::ClientState &state, const app::HostRecord &host) {
    if (host.pairingState != app::PairingState::paired) {
      return;
    }

    const std::string key = pairing_reset_endpoint_key(host.address, host.port);
    if (key.empty()) {
      return;
    }

    if (std::find(state.hosts.pairingResetEndpoints.begin(), state.hosts.pairingResetEndpoints.end(), key) == state.hosts.pairingResetEndpoints.end()) {
      state.hosts.pairingResetEndpoints.push_back(key);
    }
  }

  void clear_deleted_host_pairing(app::ClientState &state, const std::string &address, uint16_t port) {
    const std::string key = pairing_reset_endpoint_key(address, port);
    if (key.empty()) {
      return;
    }

    state.hosts.pairingResetEndpoints.erase(
      std::remove(state.hosts.pairingResetEndpoints.begin(), state.hosts.pairingResetEndpoints.end(), key),
      state.hosts.pairingResetEndpoints.end()
    );
  }

  void reset_add_host_draft(app::ClientState &state, app::ScreenId returnScreen);

  void remember_host_selection(app::ClientState &state, const app::HostRecord &host) {
    state.hosts.selectedAddress = host.address;
    state.hosts.selectedPort = host.port;
  }

  void clear_active_host(app::ClientState &state) {
    state.hosts.active = {};
    state.hosts.activeLoaded = false;
  }

  void clear_active_host_app_list(app::ClientState &state) {
    if (!state.hosts.activeLoaded) {
      return;
    }

    state.hosts.active.apps.clear();
    state.hosts.active.appListState = app::HostAppListState::idle;
    state.hosts.active.appListStatusMessage.clear();
    state.hosts.active.appListContentHash = 0U;
    state.hosts.active.lastAppListRefreshTick = 0U;
    state.hosts.active.runningGameId = 0U;
    state.apps.selectedAppIndex = 0U;
    state.apps.scrollPage = 0U;
    state.apps.showHiddenApps = false;
  }

  void copy_host_to_active_host(app::ClientState &state, const app::HostRecord &host) {
    state.hosts.active = host;
    state.hosts.activeLoaded = true;
    remember_host_selection(state, host);
  }

  void unload_hosts_page_state(app::ClientState &state) {
    if (!state.hosts.loaded) {
      return;
    }

    if (!state.hosts.items.empty() && state.hosts.selectedHostIndex < state.hosts.items.size()) {
      remember_host_selection(state, state.hosts.items[state.hosts.selectedHostIndex]);
    }

    state.hosts.items.clear();
    state.hosts.loaded = false;
    state.hosts.selectedHostIndex = 0U;
    state.hosts.focusArea = app::HostsFocusArea::toolbar;
  }

  void unload_apps_page_state(app::ClientState &state) {
    if (state.hosts.activeLoaded) {
      remember_host_selection(state, state.hosts.active);
    }
    clear_active_host_app_list(state);
  }

  void unload_settings_page_state(app::ClientState &state) {
    state.settings.savedFiles.clear();
    state.settings.savedFilesDirty = true;
    state.settings.logViewerLines.clear();
    state.settings.logViewerScrollOffset = 0U;
  }

  void unload_pair_host_screen_state(app::ClientState &state) {
    state.pairingDraft = {{}, app::DEFAULT_HOST_PORT, {}, app::PairingStage::idle, {}};
  }

  void unload_screen_state(app::ClientState &state, app::ScreenId nextScreen) {
    if (state.shell.activeScreen == nextScreen) {
      return;
    }

    switch (state.shell.activeScreen) {
      case app::ScreenId::home:
      case app::ScreenId::hosts:
        if (nextScreen == app::ScreenId::apps || nextScreen == app::ScreenId::pair_host || nextScreen == app::ScreenId::settings) {
          unload_hosts_page_state(state);
        }
        return;
      case app::ScreenId::apps:
        unload_apps_page_state(state);
        return;
      case app::ScreenId::add_host:
        reset_add_host_draft(state, app::ScreenId::hosts);
        return;
      case app::ScreenId::pair_host:
        unload_pair_host_screen_state(state);
        return;
      case app::ScreenId::settings:
        unload_settings_page_state(state);
        return;
    }
  }

  void sync_selected_settings_category_from_menu(app::ClientState &state) {
    if (const ui::MenuItem *selectedItem = state.menu.selected_item(); selectedItem != nullptr) {
      state.settings.selectedCategory = settings_category_from_menu_id(selectedItem->id);
    }
  }

  bool starts_with(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0U) == 0U;
  }

  void reset_add_host_draft(app::ClientState &state, app::ScreenId returnScreen) {
    state.addHostDraft = {
      {},
      {},
      app::AddHostField::address,
      {false, 0U, {}},
      returnScreen,
      {},
      {},
      false,
    };
  }

  void reset_confirmation(app::ClientState &state) {
    state.confirmation = {};
  }

  void open_confirmation(
    app::ClientState &state,
    app::ConfirmationAction action,
    std::string title,
    std::vector<std::string> lines,
    std::string targetPath = {}
  ) {
    state.confirmation.action = action;
    state.confirmation.targetPath = std::move(targetPath);
    state.confirmation.title = std::move(title);
    state.confirmation.lines = std::move(lines);
    state.modal.id = app::ModalId::confirmation;
    state.modal.selectedActionIndex = 0U;
  }

  app::HostRecord *find_host_by_endpoint(std::vector<app::HostRecord> &hosts, const std::string &address, uint16_t port) {
    const auto iterator = std::find_if(hosts.begin(), hosts.end(), [&address, port](const app::HostRecord &host) {
      return app::host_matches_endpoint(host, address, port);
    });
    return iterator == hosts.end() ? nullptr : &(*iterator);
  }

  app::HostRecord *find_loaded_host_by_endpoint(app::ClientState &state, const std::string &address, uint16_t port) {
    if (app::HostRecord *host = find_host_by_endpoint(state.hosts.items, address, port); host != nullptr) {
      return host;
    }
    if (state.hosts.activeLoaded && app::host_matches_endpoint(state.hosts.active, address, port)) {
      return &state.hosts.active;
    }
    return nullptr;
  }

  std::vector<std::size_t> visible_app_indices(const app::HostRecord &host, bool showHiddenApps) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < host.apps.size(); ++index) {
      if (showHiddenApps || !host.apps[index].hidden) {
        indices.push_back(index);
      }
    }
    return indices;
  }

  const app::HostAppRecord *find_app_by_id(const std::vector<app::HostAppRecord> &apps, int appId) {
    const auto iterator = std::find_if(apps.begin(), apps.end(), [appId](const app::HostAppRecord &record) {
      return record.id == appId;
    });
    return iterator == apps.end() ? nullptr : &(*iterator);
  }

  std::size_t visible_app_index_for_id(const app::HostRecord &host, bool showHiddenApps, int appId) {
    std::size_t visibleIndex = 0U;
    for (const app::HostAppRecord &record : host.apps) {
      if (!showHiddenApps && record.hidden) {
        continue;
      }
      if (record.id == appId) {
        return visibleIndex;
      }
      ++visibleIndex;
    }
    return static_cast<std::size_t>(-1);
  }

  void refresh_running_flags(app::HostRecord *host) {
    if (host == nullptr) {
      return;
    }

    for (app::HostAppRecord &appRecord : host->apps) {
      appRecord.running = static_cast<uint32_t>(appRecord.id) == host->runningGameId;
    }
  }

  void clamp_selected_host_index(app::ClientState &state) {
    if (state.hosts.items.empty()) {
      state.hosts.selectedHostIndex = 0U;
      state.hosts.focusArea = app::HostsFocusArea::toolbar;
      state.hosts.selectedToolbarButtonIndex = DEFAULT_EMPTY_HOSTS_TOOLBAR_INDEX;
      return;
    }

    if (state.hosts.selectedHostIndex >= state.hosts.items.size()) {
      state.hosts.selectedHostIndex = state.hosts.items.size() - 1U;
    }
  }

  void reset_hosts_home_selection(app::ClientState &state) {
    if (state.hosts.items.empty()) {
      state.hosts.focusArea = app::HostsFocusArea::toolbar;
      state.hosts.selectedToolbarButtonIndex = DEFAULT_EMPTY_HOSTS_TOOLBAR_INDEX;
      state.hosts.selectedHostIndex = 0U;
      return;
    }

    state.hosts.focusArea = app::HostsFocusArea::grid;
    state.hosts.selectedHostIndex = 0U;
  }

  void clamp_selected_app_index(app::ClientState &state) {
    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr) {
      state.apps.selectedAppIndex = 0U;
      return;
    }

    const std::vector<std::size_t> indices = visible_app_indices(*host, state.apps.showHiddenApps);
    if (indices.empty()) {
      state.apps.selectedAppIndex = 0U;
      return;
    }

    if (state.apps.selectedAppIndex >= indices.size()) {
      state.apps.selectedAppIndex = indices.size() - 1U;
    }
  }

  std::vector<ui::MenuItem> build_menu_for_state(const app::ClientState &state) {
    switch (state.shell.activeScreen) {
      case app::ScreenId::settings:
        return {
          {settings_category_menu_id(app::SettingsCategory::logging), "Logging", settings_category_description(app::SettingsCategory::logging), true},
          {settings_category_menu_id(app::SettingsCategory::display), "Display", settings_category_description(app::SettingsCategory::display), true},
          {settings_category_menu_id(app::SettingsCategory::input), "Input", settings_category_description(app::SettingsCategory::input), true},
          {settings_category_menu_id(app::SettingsCategory::reset), "Reset", settings_category_description(app::SettingsCategory::reset), true},
        };
      case app::ScreenId::add_host:
        return {
          {"edit-address", "Host Address", "Enter the IPv4 address for the PC that should be added to Moonlight.", true},
          {"edit-port", "Host Port", "Override the default Moonlight host port when the PC listens on a custom value.", true},
          {"test-connection", "Test Connection", "Check whether the current host address and port respond before saving anything.", true},
          {"start-pairing", "Start Pairing", "Connect to the current host and begin PIN-based pairing.", true},
          {"save-host", "Save Host", "Store this host in the saved host list and return to the home screen.", true},
          {"cancel-add-host", "Cancel", "Discard the current host draft and return without saving.", true},
        };
      case app::ScreenId::pair_host:
        return {
          {"cancel-pairing", "Cancel", "Stop the current pairing attempt and return to the previous screen.", true},
        };
      case app::ScreenId::home:
      case app::ScreenId::hosts:
      case app::ScreenId::apps:
        return {};
    }

    return {};
  }

  std::vector<ui::MenuItem> build_detail_menu_for_state(const app::ClientState &state) {
    if (state.shell.activeScreen != app::ScreenId::settings) {
      return {};
    }

    switch (state.settings.selectedCategory) {
      case app::SettingsCategory::logging:
        return {
          {"view-log-file", "View Log File", "Open the runtime log file viewer so you can inspect the most recent log lines without leaving the shell.", true},
          {"cycle-log-level", std::string("File Logging Level: ") + logging::to_string(state.settings.loggingLevel), "Choose the minimum severity written to moonlight.log. Lower levels produce more detail but increase disk writes.", true},
          {"cycle-xemu-console-log-level", std::string("xemu Console Level: ") + logging::to_string(state.settings.xemuConsoleLoggingLevel), "Choose the minimum severity mirrored to xemu through DbgPrint() when you launch xemu with a serial console.", true},
        };
      case app::SettingsCategory::display:
        return {
          {"display-placeholder", "Display settings are not implemented yet", "Display-specific options are planned, but there are no adjustable display settings in this build yet.", true},
        };
      case app::SettingsCategory::input:
        return {
          {"input-placeholder", "Input settings are not implemented yet", "Input-specific options are planned, but there are no adjustable controller settings in this build yet.", true},
        };
      case app::SettingsCategory::reset:
        {
          std::vector<ui::MenuItem> items = {
            {"factory-reset", "Factory Reset", "Delete every Moonlight saved file, including hosts, pairing identity, cached art, and logs.", true},
          };
          for (const startup::SavedFileEntry &savedFile : state.settings.savedFiles) {
            items.push_back({std::string(DELETE_SAVED_FILE_MENU_ID_PREFIX) + savedFile.path, "Delete " + savedFile.displayName, "Delete only this saved file from disk while leaving the rest of the Moonlight data intact.", true});
          }
          return items;
        }
    }

    return {};
  }

  void rebuild_menu(app::ClientState &state, const std::string &preferredItemId = {}, bool preserveSelection = true) {
    const std::string previousSelection = preserveSelection && state.menu.selected_item() != nullptr ? state.menu.selected_item()->id : std::string {};
    state.menu.set_items(build_menu_for_state(state));
    if (!preferredItemId.empty() && state.menu.select_item_by_id(preferredItemId)) {
      return;
    }
    if (!previousSelection.empty()) {
      state.menu.select_item_by_id(previousSelection);
    }

    const std::string previousDetailSelection = preserveSelection && state.detailMenu.selected_item() != nullptr ? state.detailMenu.selected_item()->id : std::string {};
    state.detailMenu.set_items(build_detail_menu_for_state(state));
    if (!preferredItemId.empty() && state.detailMenu.select_item_by_id(preferredItemId)) {
      return;
    }
    if (!previousDetailSelection.empty()) {
      state.detailMenu.select_item_by_id(previousDetailSelection);
    }
  }

  void rebuild_settings_detail_menu(app::ClientState &state, const std::string &preferredItemId = {}, bool preserveSelection = true) {
    const std::string previousSelection = preserveSelection && state.detailMenu.selected_item() != nullptr ? state.detailMenu.selected_item()->id : std::string {};
    state.detailMenu.set_items(build_detail_menu_for_state(state));
    if (!preferredItemId.empty() && state.detailMenu.select_item_by_id(preferredItemId)) {
      return;
    }
    if (!previousSelection.empty()) {
      state.detailMenu.select_item_by_id(previousSelection);
    }
  }

  void close_modal(app::ClientState &state) {
    state.modal = {};
    reset_confirmation(state);
  }

  void set_screen(app::ClientState &state, app::ScreenId screen, const std::string &preferredItemId = {}) {
    unload_screen_state(state, screen);
    state.shell.activeScreen = screen;
    if (screen == app::ScreenId::settings) {
      state.settings.savedFilesDirty = true;
      state.settings.focusArea = app::SettingsFocusArea::categories;
    }
    close_modal(state);
    rebuild_menu(state, preferredItemId, false);
    if (screen == app::ScreenId::settings) {
      sync_selected_settings_category_from_menu(state);
      rebuild_settings_detail_menu(state);
    }
    clamp_selected_host_index(state);
    clamp_selected_app_index(state);
  }

  void open_modal(app::ClientState &state, app::ModalId modalId, std::size_t selectedActionIndex = 0U) {
    state.modal.id = modalId;
    state.modal.selectedActionIndex = selectedActionIndex;
  }

  void cycle_log_viewer_placement(app::ClientState &state) {
    switch (state.settings.logViewerPlacement) {
      case app::LogViewerPlacement::full:
        state.settings.logViewerPlacement = app::LogViewerPlacement::left;
        return;
      case app::LogViewerPlacement::left:
        state.settings.logViewerPlacement = app::LogViewerPlacement::right;
        return;
      case app::LogViewerPlacement::right:
        state.settings.logViewerPlacement = app::LogViewerPlacement::full;
        return;
    }
  }

  void scroll_log_viewer(app::ClientState &state, bool towardOlderEntries, std::size_t step) {
    if (state.settings.logViewerLines.empty() || step == 0U) {
      state.settings.logViewerScrollOffset = 0U;
      return;
    }

    const std::size_t maxOffset = state.settings.logViewerLines.size() > 1U ? state.settings.logViewerLines.size() - 1U : 0U;
    if (towardOlderEntries) {
      state.settings.logViewerScrollOffset = std::min(maxOffset, state.settings.logViewerScrollOffset + step);
      return;
    }

    state.settings.logViewerScrollOffset = state.settings.logViewerScrollOffset > step ? state.settings.logViewerScrollOffset - step : 0U;
  }

  std::size_t modal_action_count(const app::ClientState &state) {
    switch (state.modal.id) {
      case app::ModalId::host_actions:
        return 4U;
      case app::ModalId::app_actions:
        return 3U;
      case app::ModalId::confirmation:
        return 2U;
      case app::ModalId::none:
      case app::ModalId::support:
      case app::ModalId::host_details:
      case app::ModalId::app_details:
      case app::ModalId::log_viewer:
        return 0U;
    }
    return 0U;
  }

  bool move_modal_selection(app::ClientState &state, int direction) {
    const std::size_t count = modal_action_count(state);
    if (count == 0U) {
      return false;
    }

    const std::size_t current = state.modal.selectedActionIndex % count;
    state.modal.selectedActionIndex = direction < 0 ? (current + count - 1U) % count : (current + 1U) % count;
    return state.modal.selectedActionIndex != current;
  }

  void open_add_host_keypad(app::ClientState &state, app::AddHostField field) {
    state.addHostDraft.activeField = field;
    state.addHostDraft.keypad.visible = true;
    state.addHostDraft.keypad.selectedButtonIndex = 0U;
    state.addHostDraft.keypad.stagedInput = field == app::AddHostField::address ? state.addHostDraft.addressInput : state.addHostDraft.portInput;
    state.shell.statusMessage = field == app::AddHostField::address ? "Editing host address" : "Editing host port";
    rebuild_menu(state, add_host_field_menu_id(field));
  }

  void close_add_host_keypad(app::ClientState &state) {
    state.addHostDraft.keypad.visible = false;
    state.addHostDraft.keypad.stagedInput.clear();
    rebuild_menu(state, add_host_field_menu_id(state.addHostDraft.activeField));
  }

  void accept_add_host_keypad(app::ClientState &state) {
    if (state.addHostDraft.activeField == app::AddHostField::address) {
      state.addHostDraft.addressInput = state.addHostDraft.keypad.stagedInput;
      state.shell.statusMessage = "Updated host address";
    } else {
      state.addHostDraft.portInput = state.addHostDraft.keypad.stagedInput;
      state.shell.statusMessage = state.addHostDraft.portInput.empty() ? "Using default Moonlight host port 47989" : "Updated host port";
    }

    state.addHostDraft.validationMessage.clear();
    state.addHostDraft.connectionMessage.clear();
    close_add_host_keypad(state);
  }

  void cancel_add_host_keypad(app::ClientState &state) {
    state.shell.statusMessage = state.addHostDraft.activeField == app::AddHostField::address ? "Cancelled host address edit" : "Cancelled host port edit";
    close_add_host_keypad(state);
  }

  bool move_add_host_keypad_selection(app::ClientState &state, int rowDelta, int columnDelta) {
    const AddHostKeypadLayout layout = add_host_keypad_layout(state);
    if (layout.buttonCount == 0U) {
      return false;
    }

    const auto rowCount = static_cast<int>((layout.buttonCount + ADD_HOST_KEYPAD_COLUMN_COUNT - 1U) / ADD_HOST_KEYPAD_COLUMN_COUNT);
    const std::size_t currentIndex = state.addHostDraft.keypad.selectedButtonIndex % layout.buttonCount;
    const auto currentRow = static_cast<int>(currentIndex / ADD_HOST_KEYPAD_COLUMN_COUNT);
    const auto currentColumn = static_cast<int>(currentIndex % ADD_HOST_KEYPAD_COLUMN_COUNT);

    auto wrap_index = [](int value, int count) {
      if (count <= 0) {
        return 0;
      }

      int wrappedValue = value % count;
      if (wrappedValue < 0) {
        wrappedValue += count;
      }
      return wrappedValue;
    };

    int targetRow = currentRow;
    int targetColumn = currentColumn;
    if (rowDelta != 0) {
      targetRow = wrap_index(currentRow + rowDelta, rowCount);
      const std::size_t rowStart = static_cast<std::size_t>(targetRow) * ADD_HOST_KEYPAD_COLUMN_COUNT;
      const std::size_t rowWidth = std::min<std::size_t>(ADD_HOST_KEYPAD_COLUMN_COUNT, layout.buttonCount - rowStart);
      targetColumn = static_cast<int>(std::min<std::size_t>(currentColumn, rowWidth - 1U));
    }

    const std::size_t targetRowStart = static_cast<std::size_t>(targetRow) * ADD_HOST_KEYPAD_COLUMN_COUNT;
    if (const std::size_t targetRowWidth = std::min<std::size_t>(ADD_HOST_KEYPAD_COLUMN_COUNT, layout.buttonCount - targetRowStart); columnDelta != 0 && targetRowWidth > 0U) {
      targetColumn = wrap_index(targetColumn + columnDelta, static_cast<int>(targetRowWidth));
    }

    const auto nextIndex = targetRowStart + static_cast<std::size_t>(targetColumn);
    state.addHostDraft.keypad.selectedButtonIndex = nextIndex;
    return nextIndex != currentIndex;
  }

  void append_to_active_add_host_field(app::ClientState &state, char character) {
    state.addHostDraft.keypad.stagedInput.push_back(character);
    state.addHostDraft.validationMessage.clear();
    state.addHostDraft.connectionMessage.clear();
  }

  void backspace_active_add_host_field(app::ClientState &state) {
    if (!state.addHostDraft.keypad.stagedInput.empty()) {
      state.addHostDraft.keypad.stagedInput.pop_back();
    }
  }

  bool normalize_add_host_inputs(const app::ClientState &state, std::string *normalizedAddress, uint16_t *parsedPort, std::string *errorMessage) {
    const std::string address = app::normalize_ipv4_address(state.addHostDraft.addressInput);
    if (address.empty()) {
      if (errorMessage != nullptr) {
        *errorMessage = "Enter a valid IPv4 host address";
      }
      return false;
    }

    uint16_t port = 0;
    if (!state.addHostDraft.portInput.empty() && !app::try_parse_host_port(state.addHostDraft.portInput, &port)) {
      if (errorMessage != nullptr) {
        *errorMessage = "Enter a valid host port";
      }
      return false;
    }

    if (normalizedAddress != nullptr) {
      *normalizedAddress = address;
    }
    if (parsedPort != nullptr) {
      *parsedPort = port;
    }
    return true;
  }

  app::HostRecord make_host_record(const std::string &address, uint16_t port) {
    return {
      app::build_default_host_display_name(address),
      address,
      port,
      app::PairingState::not_paired,
      app::HostReachability::unknown,
      {},
      {},
      {},
      {},
      {},
      address,
      {},
      0,
      0,
      {},
      app::HostAppListState::idle,
      {},
      0,
    };
  }

  void move_toolbar_selection(app::ClientState &state, int direction) {
    const std::size_t current = state.hosts.selectedToolbarButtonIndex % HOST_TOOLBAR_BUTTON_COUNT;
    state.hosts.selectedToolbarButtonIndex = direction < 0 ? (current + HOST_TOOLBAR_BUTTON_COUNT - 1U) % HOST_TOOLBAR_BUTTON_COUNT : (current + 1U) % HOST_TOOLBAR_BUTTON_COUNT;
  }

  std::size_t grid_row_count(std::size_t itemCount, std::size_t columnCount) {
    return itemCount == 0U || columnCount == 0U ? 0U : ((itemCount + columnCount - 1U) / columnCount);
  }

  std::size_t grid_row_start(std::size_t row, std::size_t columnCount) {
    return row * columnCount;
  }

  std::size_t grid_row_end(std::size_t itemCount, std::size_t row, std::size_t columnCount) {
    return std::min(itemCount, grid_row_start(row, columnCount) + columnCount);
  }

  std::size_t closest_index_in_row(std::size_t itemCount, std::size_t row, std::size_t columnCount, std::size_t preferredColumn) {
    const std::size_t rowStart = grid_row_start(row, columnCount);
    const std::size_t rowEnd = grid_row_end(itemCount, row, columnCount);
    if (rowStart >= rowEnd) {
      return itemCount == 0U ? 0U : (itemCount - 1U);
    }

    return rowStart + std::min(preferredColumn, (rowEnd - rowStart) - 1U);
  }

  bool move_grid_selection(std::size_t itemCount, std::size_t columnCount, int rowDelta, int columnDelta, std::size_t *selectedIndex, bool *movedAboveFirstRow = nullptr) {
    if (movedAboveFirstRow != nullptr) {
      *movedAboveFirstRow = false;
    }
    if (selectedIndex == nullptr || itemCount == 0U || columnCount == 0U) {
      return false;
    }

    std::size_t currentIndex = std::min(*selectedIndex, itemCount - 1U);
    const std::size_t rowCount = grid_row_count(itemCount, columnCount);
    const std::size_t currentRow = currentIndex / columnCount;
    const std::size_t currentColumn = currentIndex % columnCount;

    if (columnDelta > 0) {
      for (int step = 0; step < columnDelta; ++step) {
        if (const std::size_t rowEnd = grid_row_end(itemCount, currentRow, columnCount); currentIndex + 1U < rowEnd) {
          ++currentIndex;
          continue;
        }

        const std::size_t nextRow = (currentIndex / columnCount) + 1U;
        if (nextRow >= rowCount) {
          break;
        }
        currentIndex = grid_row_start(nextRow, columnCount);
      }
      *selectedIndex = currentIndex;
      return true;
    }

    if (columnDelta < 0) {
      for (int step = 0; step < -columnDelta; ++step) {
        if ((currentIndex % columnCount) > 0U) {
          --currentIndex;
          continue;
        }

        const std::size_t currentResolvedRow = currentIndex / columnCount;
        if (currentResolvedRow == 0U) {
          break;
        }
        const std::size_t previousRow = currentResolvedRow - 1U;
        currentIndex = grid_row_end(itemCount, previousRow, columnCount) - 1U;
      }
      *selectedIndex = currentIndex;
      return true;
    }

    if (rowDelta == 0) {
      return false;
    }

    const int targetRow = static_cast<int>(currentRow) + rowDelta;
    if (targetRow < 0) {
      if (movedAboveFirstRow != nullptr) {
        *movedAboveFirstRow = true;
      }
      return false;
    }

    const std::size_t clampedRow = std::min(static_cast<std::size_t>(targetRow), rowCount - 1U);
    *selectedIndex = closest_index_in_row(itemCount, clampedRow, columnCount, currentColumn);
    return true;
  }

  void move_host_grid_selection(app::ClientState &state, int rowDelta, int columnDelta) {
    if (state.hosts.items.empty()) {
      state.hosts.focusArea = app::HostsFocusArea::toolbar;
      return;
    }

    bool movedAboveFirstRow = false;
    move_grid_selection(state.hosts.items.size(), HOST_GRID_COLUMN_COUNT, rowDelta, columnDelta, &state.hosts.selectedHostIndex, &movedAboveFirstRow);
    if (movedAboveFirstRow) {
      state.hosts.focusArea = app::HostsFocusArea::toolbar;
      return;
    }
    state.hosts.focusArea = app::HostsFocusArea::grid;
  }

  void move_app_grid_selection(app::ClientState &state, int rowDelta, int columnDelta) {
    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr) {
      state.apps.selectedAppIndex = 0U;
      return;
    }

    const std::vector<std::size_t> indices = visible_app_indices(*host, state.apps.showHiddenApps);
    if (indices.empty()) {
      state.apps.selectedAppIndex = 0U;
      return;
    }

    move_grid_selection(indices.size(), APP_GRID_COLUMN_COUNT, rowDelta, columnDelta, &state.apps.selectedAppIndex);
  }

  void enter_add_host_screen(app::ClientState &state) {
    reset_add_host_draft(state, state.shell.activeScreen == app::ScreenId::add_host ? app::ScreenId::hosts : state.shell.activeScreen);
    set_screen(state, app::ScreenId::add_host, "edit-address");
  }

  bool enter_pair_host_screen(app::ClientState &state, const std::string &address, uint16_t port) {
    if (const app::HostRecord *host = find_loaded_host_by_endpoint(state, address, port); host != nullptr && host->reachability == app::HostReachability::offline) {
      state.shell.statusMessage = "Host is offline. Bring it online before pairing.";
      return false;
    }

    if (const app::HostRecord *host = find_loaded_host_by_endpoint(state, address, port); host != nullptr) {
      copy_host_to_active_host(state, *host);
    }

    std::string pairingPin;
    if (std::string pinError; !network::generate_pairing_pin(&pairingPin, &pinError)) {
      state.shell.statusMessage = pinError.empty() ? "Failed to generate a secure pairing PIN." : std::move(pinError);
      return false;
    }

    state.pairingDraft = app::create_pairing_draft(address, app::effective_host_port(port), pairingPin);
    set_screen(state, app::ScreenId::pair_host, "cancel-pairing");
    return true;
  }

  bool enter_apps_screen(app::ClientState &state, bool showHiddenApps) {
    const app::HostRecord *host = state.hosts.items.empty() ? nullptr : &state.hosts.items[state.hosts.selectedHostIndex];
    if (host == nullptr) {
      return false;
    }
    if (host->reachability == app::HostReachability::offline) {
      state.shell.statusMessage = "Host is offline. Bring it online before opening apps.";
      return false;
    }
    if (host->pairingState != app::PairingState::paired) {
      state.shell.statusMessage = "This host is no longer paired. Pair it again before opening apps.";
      return false;
    }

    copy_host_to_active_host(state, *host);
    state.apps.showHiddenApps = showHiddenApps;
    state.apps.selectedAppIndex = 0U;
    state.apps.scrollPage = 0U;
    state.hosts.active.appListState = app::HostAppListState::loading;
    state.hosts.active.appListStatusMessage = (state.hosts.active.apps.empty() ? "Loading apps for " : "Refreshing apps for ") + state.hosts.active.displayName + "...";
    state.shell.statusMessage.clear();
    set_screen(state, app::ScreenId::apps);
    return true;
  }

  void select_host_by_endpoint(app::ClientState &state, const std::string &address, uint16_t port) {
    for (std::size_t index = 0; index < state.hosts.items.size(); ++index) {
      if (app::host_matches_endpoint(state.hosts.items[index], address, port)) {
        state.hosts.selectedHostIndex = index;
        state.hosts.focusArea = app::HostsFocusArea::grid;
        remember_host_selection(state, state.hosts.items[index]);
        return;
      }
    }
  }

  logging::LogLevel next_logging_level(logging::LogLevel currentLevel) {
    switch (currentLevel) {
      case logging::LogLevel::none:
        return logging::LogLevel::error;
      case logging::LogLevel::trace:
        return logging::LogLevel::none;
      case logging::LogLevel::debug:
        return logging::LogLevel::trace;
      case logging::LogLevel::info:
        return logging::LogLevel::debug;
      case logging::LogLevel::warning:
        return logging::LogLevel::info;
      case logging::LogLevel::error:
        return logging::LogLevel::warning;
    }
    return logging::LogLevel::none;
  }

  /**
   * @brief Closes the active modal and records the close in the outgoing update.
   *
   * @param state Client state whose modal should be dismissed.
   * @param update Update structure that tracks modal lifecycle changes.
   */
  void close_modal_and_mark_closed(app::ClientState &state, app::AppUpdate *update) {
    close_modal(state);
    if (update != nullptr) {
      update->navigation.modalClosed = true;
    }
  }

  /**
   * @brief Copies the current pairing draft into an update after entering the pairing screen.
   *
   * @param state Client state containing the generated pairing draft.
   * @param update Update structure to populate.
   */
  void assign_pairing_request_from_draft(const app::ClientState &state, app::AppUpdate *update) {
    if (update == nullptr) {
      return;
    }

    update->navigation.screenChanged = true;
    update->requests.pairingRequested = true;
    update->requests.pairingAddress = state.pairingDraft.targetAddress;
    update->requests.pairingPort = state.pairingDraft.targetPort;
    update->requests.pairingPin = state.pairingDraft.generatedPin;
  }

  /**
   * @brief Tries to enter the pairing screen for a host action and fills the update when successful.
   *
   * @param state Client state to transition.
   * @param host Host selected for the pairing flow.
   * @param update Update structure that receives the pairing request details.
   */
  void request_host_pairing(app::ClientState &state, const app::HostRecord &host, app::AppUpdate *update) {
    if (!enter_pair_host_screen(state, host.address, host.port)) {
      return;
    }

    assign_pairing_request_from_draft(state, update);
  }

  /**
   * @brief Collects unique cover-art cache keys before deleting a saved host.
   *
   * @param deletedHost Host being removed from the saved-host list.
   * @param update Update structure that receives cleanup work.
   */
  void collect_deleted_host_cover_art_keys(const app::HostRecord &deletedHost, app::AppUpdate *update) {
    if (update == nullptr) {
      return;
    }

    for (const app::HostAppRecord &appRecord : deletedHost.apps) {
      if (appRecord.boxArtCacheKey.empty()) {
        continue;
      }
      if (std::find(update->persistence.deletedHostCoverArtCacheKeys.begin(), update->persistence.deletedHostCoverArtCacheKeys.end(), appRecord.boxArtCacheKey) != update->persistence.deletedHostCoverArtCacheKeys.end()) {
        continue;
      }
      update->persistence.deletedHostCoverArtCacheKeys.push_back(appRecord.boxArtCacheKey);
    }
  }

  /**
   * @brief Deletes the currently selected host and records the cleanup side effects.
   *
   * @param state Client state containing the selected host.
   * @param update Update structure that receives deletion work.
   */
  void delete_selected_host(app::ClientState &state, app::AppUpdate *update) {
    if (update == nullptr || state.hosts.selectedHostIndex >= state.hosts.items.size()) {
      return;
    }

    const app::HostRecord deletedHost = state.hosts.items[state.hosts.selectedHostIndex];
    remember_deleted_host_pairing(state, deletedHost);
    update->persistence.hostDeleteCleanupRequested = true;
    update->persistence.deletedHostAddress = deletedHost.address;
    update->persistence.deletedHostPort = deletedHost.port;
    update->persistence.deletedHostWasPaired = deletedHost.pairingState == app::PairingState::paired;
    collect_deleted_host_cover_art_keys(deletedHost, update);
    state.hosts.items.erase(state.hosts.items.begin() + static_cast<std::ptrdiff_t>(state.hosts.selectedHostIndex));
    state.hosts.dirty = true;
    update->persistence.hostsChanged = true;
    clamp_selected_host_index(state);
    close_modal_and_mark_closed(state, update);
    state.shell.statusMessage = "Deleted saved host";
  }

  /**
   * @brief Handles commands while the log viewer modal is active.
   *
   * @param state Client state containing the log viewer modal.
   * @param command Command being processed.
   * @param update Update structure that receives side effects.
   * @return True when the command was consumed by the log viewer.
   */
  bool handle_log_viewer_modal_command(app::ClientState &state, input::UiCommand command, app::AppUpdate *update) {
    if (state.modal.id != app::ModalId::log_viewer) {
      return false;
    }

    switch (command) {
      case input::UiCommand::back:
      case input::UiCommand::activate:
      case input::UiCommand::confirm:
        close_modal_and_mark_closed(state, update);
        return true;
      case input::UiCommand::delete_character:
      case input::UiCommand::open_context_menu:
        cycle_log_viewer_placement(state);
        state.settings.dirty = true;
        if (update != nullptr) {
          update->persistence.settingsChanged = true;
        }
        return true;
      case input::UiCommand::previous_page:
        scroll_log_viewer(state, true, LOG_VIEWER_SCROLL_STEP);
        return true;
      case input::UiCommand::next_page:
        scroll_log_viewer(state, false, LOG_VIEWER_SCROLL_STEP);
        return true;
      case input::UiCommand::fast_previous_page:
        scroll_log_viewer(state, true, LOG_VIEWER_FAST_SCROLL_STEP);
        return true;
      case input::UiCommand::fast_next_page:
        scroll_log_viewer(state, false, LOG_VIEWER_FAST_SCROLL_STEP);
        return true;
      case input::UiCommand::move_up:
      case input::UiCommand::move_down:
      case input::UiCommand::move_left:
      case input::UiCommand::move_right:
      case input::UiCommand::toggle_overlay:
      case input::UiCommand::none:
        return true;
    }

    return true;
  }

  /**
   * @brief Handles confirmation modal activation commands.
   *
   * @param state Client state containing the confirmation dialog.
   * @param update Update structure that receives the confirmed action.
   * @return True after consuming the confirmation action.
   */
  bool handle_confirmation_modal_activation(app::ClientState &state, app::AppUpdate *update) {
    const bool confirmed = state.modal.selectedActionIndex % 2U == 0U;
    const app::ConfirmationAction action = state.confirmation.action;
    const std::string targetPath = state.confirmation.targetPath;
    close_modal_and_mark_closed(state, update);
    if (!confirmed) {
      state.shell.statusMessage = "Cancelled the pending reset action";
      return true;
    }
    if (update == nullptr) {
      return true;
    }
    if (action == app::ConfirmationAction::delete_saved_file) {
      update->persistence.savedFileDeleteRequested = true;
      update->persistence.savedFileDeletePath = targetPath;
      return true;
    }
    if (action == app::ConfirmationAction::factory_reset) {
      update->persistence.factoryResetRequested = true;
    }
    return true;
  }

  /**
   * @brief Handles activation inside the host actions modal.
   *
   * @param state Client state containing the selected host.
   * @param update Update structure that receives host-action side effects.
   * @return True after consuming the modal activation.
   */
  bool handle_host_actions_modal_activation(app::ClientState &state, app::AppUpdate *update) {
    const app::HostRecord *host = app::selected_host(state);
    if (host == nullptr) {
      close_modal_and_mark_closed(state, update);
      return true;
    }

    switch (state.modal.selectedActionIndex % 4U) {
      case 0:
        close_modal_and_mark_closed(state, update);
        if (host->pairingState == app::PairingState::paired) {
          if (update != nullptr) {
            update->requests.appsBrowseRequested = true;
            update->requests.appsBrowseShowHidden = true;
          }
          return true;
        }
        request_host_pairing(state, *host, update);
        return true;
      case 1:
        close_modal_and_mark_closed(state, update);
        if (update != nullptr) {
          update->requests.connectionTestRequested = true;
          update->requests.connectionTestAddress = host->address;
          update->requests.connectionTestPort = app::effective_host_port(host->port);
        }
        return true;
      case 2:
        delete_selected_host(state, update);
        return true;
      case 3:
        open_modal(state, app::ModalId::host_details);
        return true;
      default:
        return true;
    }
  }

  /**
   * @brief Handles activation inside the app actions modal.
   *
   * @param state Client state containing the selected app.
   * @param update Update structure that receives app-action side effects.
   * @return True after consuming the modal activation.
   */
  bool handle_app_actions_modal_activation(app::ClientState &state, app::AppUpdate *update) {
    const app::HostRecord *host = app::apps_host(state);
    if (const app::HostAppRecord *selectedApp = app::selected_app(state); host == nullptr || selectedApp == nullptr) {
      close_modal_and_mark_closed(state, update);
      return true;
    }

    app::HostRecord *mutableHost = state.hosts.activeLoaded ? &state.hosts.active : nullptr;
    if (mutableHost == nullptr) {
      close_modal_and_mark_closed(state, update);
      return true;
    }

    const std::vector<std::size_t> indices = visible_app_indices(*mutableHost, state.apps.showHiddenApps);
    if (indices.empty()) {
      close_modal_and_mark_closed(state, update);
      return true;
    }

    app::HostAppRecord &appRecord = mutableHost->apps[indices[state.apps.selectedAppIndex]];
    switch (state.modal.selectedActionIndex % 3U) {
      case 0:
        appRecord.hidden = !appRecord.hidden;
        state.hosts.dirty = true;
        close_modal_and_mark_closed(state, update);
        if (update != nullptr) {
          update->persistence.hostsChanged = true;
        }
        clamp_selected_app_index(state);
        return true;
      case 1:
        open_modal(state, app::ModalId::app_details);
        return true;
      case 2:
        appRecord.favorite = !appRecord.favorite;
        state.hosts.dirty = true;
        close_modal_and_mark_closed(state, update);
        if (update != nullptr) {
          update->persistence.hostsChanged = true;
        }
        return true;
      default:
        return true;
    }
  }

  /**
   * @brief Handles activation or confirmation commands for the active modal.
   *
   * @param state Client state containing the modal.
   * @param update Update structure that receives modal side effects.
   * @return True when a modal action was processed.
   */
  bool handle_modal_activation(app::ClientState &state, app::AppUpdate *update) {
    switch (state.modal.id) {
      case app::ModalId::support:
      case app::ModalId::host_details:
      case app::ModalId::app_details:
        close_modal_and_mark_closed(state, update);
        return true;
      case app::ModalId::log_viewer:
        return true;
      case app::ModalId::confirmation:
        return handle_confirmation_modal_activation(state, update);
      case app::ModalId::host_actions:
        return handle_host_actions_modal_activation(state, update);
      case app::ModalId::app_actions:
        return handle_app_actions_modal_activation(state, update);
      case app::ModalId::none:
        return false;
    }

    return false;
  }

  bool handle_modal_command(app::ClientState &state, input::UiCommand command, app::AppUpdate *update) {
    if (!state.modal.active()) {
      return false;
    }

    if (handle_log_viewer_modal_command(state, command, update)) {
      return true;
    }

    if (command == input::UiCommand::back) {
      close_modal_and_mark_closed(state, update);
      return true;
    }

    if (command == input::UiCommand::move_up || command == input::UiCommand::move_left) {
      move_modal_selection(state, -1);
      return true;
    }
    if (command == input::UiCommand::move_down || command == input::UiCommand::move_right) {
      move_modal_selection(state, 1);
      return true;
    }

    if (command != input::UiCommand::activate && command != input::UiCommand::confirm) {
      return true;
    }

    return handle_modal_activation(state, update);
  }

}  // namespace

namespace app {

  ClientState create_initial_state() {
    ClientState state;
    state.shell.activeScreen = ScreenId::hosts;
    state.shell.overlayVisible = false;
    state.shell.shouldExit = false;
    state.hosts.dirty = false;
    state.hosts.loaded = true;
    state.shell.overlayScrollOffset = 0U;
    state.hosts.focusArea = HostsFocusArea::toolbar;
    state.hosts.selectedToolbarButtonIndex = DEFAULT_EMPTY_HOSTS_TOOLBAR_INDEX;
    state.hosts.selectedHostIndex = 0U;
    state.apps.selectedAppIndex = 0U;
    state.apps.scrollPage = 0U;
    state.apps.showHiddenApps = false;
    state.hosts.activeLoaded = false;
    state.hosts.selectedPort = 0;
    state.addHostDraft.activeField = AddHostField::address;
    state.addHostDraft.keypad.visible = false;
    state.addHostDraft.keypad.selectedButtonIndex = 0U;
    state.addHostDraft.returnScreen = ScreenId::hosts;
    state.addHostDraft.lastConnectionSucceeded = false;
    state.settings.focusArea = SettingsFocusArea::categories;
    state.pairingDraft.targetPort = DEFAULT_HOST_PORT;
    state.pairingDraft.stage = PairingStage::idle;
    state.settings.selectedCategory = SettingsCategory::logging;
    state.settings.logViewerScrollOffset = 0U;
    state.settings.logViewerPlacement = LogViewerPlacement::full;
    state.settings.loggingLevel = logging::LogLevel::none;
    state.settings.xemuConsoleLoggingLevel = logging::LogLevel::none;
    state.settings.dirty = false;
    state.settings.savedFilesDirty = true;
    return state;
  }

  const char *to_string(ScreenId screen) {
    switch (screen) {
      case ScreenId::home:
        return "home";
      case ScreenId::hosts:
        return "hosts";
      case ScreenId::apps:
        return "apps";
      case ScreenId::add_host:
        return "add_host";
      case ScreenId::pair_host:
        return "pair_host";
      case ScreenId::settings:
        return "settings";
    }
    return "unknown";
  }

  void replace_hosts(ClientState &state, std::vector<HostRecord> hosts, std::string statusMessage) {
    state.hosts.items = std::move(hosts);
    state.hosts.loaded = true;
    state.hosts.dirty = false;
    state.shell.statusMessage = std::move(statusMessage);
    bool restoredSelection = false;
    if (!state.hosts.selectedAddress.empty()) {
      for (std::size_t index = 0; index < state.hosts.items.size(); ++index) {
        if (host_matches_endpoint(state.hosts.items[index], state.hosts.selectedAddress, state.hosts.selectedPort)) {
          state.hosts.selectedHostIndex = index;
          state.hosts.focusArea = HostsFocusArea::grid;
          restoredSelection = true;
          break;
        }
      }
    }
    if (!restoredSelection) {
      reset_hosts_home_selection(state);
    }
    clamp_selected_host_index(state);
    clamp_selected_app_index(state);
    if (state.shell.activeScreen == ScreenId::hosts) {
      clear_active_host(state);
    }

    if (state.shell.activeScreen == ScreenId::settings || state.shell.activeScreen == ScreenId::add_host || state.shell.activeScreen == ScreenId::pair_host) {
      rebuild_menu(state);
    }
  }

  void replace_saved_files(ClientState &state, std::vector<startup::SavedFileEntry> savedFiles) {
    state.settings.savedFiles = std::move(savedFiles);
    state.settings.savedFilesDirty = false;
    if (state.shell.activeScreen == ScreenId::settings) {
      rebuild_menu(state, state.detailMenu.selected_item() != nullptr ? state.detailMenu.selected_item()->id : std::string {});
    }
  }

  std::string current_add_host_address(const ClientState &state) {
    return state.addHostDraft.addressInput;
  }

  uint16_t current_add_host_port(const ClientState &state) {
    uint16_t port = 0;
    return try_parse_host_port(state.addHostDraft.portInput, &port) ? effective_host_port(port) : DEFAULT_HOST_PORT;
  }

  bool begin_selected_host_app_browse(ClientState &state, bool showHiddenApps) {
    return enter_apps_screen(state, showHiddenApps);
  }

  std::string current_pairing_pin(const ClientState &state) {
    return state.pairingDraft.generatedPin;
  }

  void apply_connection_test_result(ClientState &state, bool success, std::string message) {
    if (state.shell.activeScreen == ScreenId::add_host) {
      state.addHostDraft.connectionMessage = message;
      state.addHostDraft.lastConnectionSucceeded = success;
    }
    if (!state.hosts.items.empty() && state.hosts.selectedHostIndex < state.hosts.items.size()) {
      state.hosts.items[state.hosts.selectedHostIndex].reachability = success ? HostReachability::online : HostReachability::offline;
    } else if (state.hosts.activeLoaded) {
      state.hosts.active.reachability = success ? HostReachability::online : HostReachability::offline;
    }
    state.shell.statusMessage = std::move(message);
  }

  bool apply_pairing_result(ClientState &state, const std::string &address, uint16_t port, bool success, std::string message) {
    state.pairingDraft.targetAddress = address;
    state.pairingDraft.targetPort = effective_host_port(port);
    state.pairingDraft.stage = success ? PairingStage::paired : PairingStage::failed;
    if (!success) {
      state.pairingDraft.generatedPin.clear();
    }
    state.pairingDraft.statusMessage = message;
    state.shell.statusMessage = std::move(message);

    HostRecord *host = find_loaded_host_by_endpoint(state, address, port);
    if (host == nullptr) {
      return false;
    }

    if (success) {
      clear_deleted_host_pairing(state, address, port);
      host->pairingState = PairingState::paired;
      host->reachability = HostReachability::online;
      if (state.hosts.loaded) {
        select_host_by_endpoint(state, address, port);
      } else {
        remember_host_selection(state, *host);
      }
      set_screen(state, ScreenId::hosts);
      state.hosts.dirty = true;
      return true;
    }

    host->pairingState = PairingState::not_paired;
    return false;
  }

  /**
   * @brief Finds the host that should receive an app-list refresh result.
   *
   * @param state Client state that owns saved and active hosts.
   * @param address Address reported by the background task.
   * @param port Port reported by the background task.
   * @return Pointer to the matching host, or null when no host matches.
   */
  HostRecord *find_app_list_result_host(ClientState &state, const std::string &address, uint16_t port) {
    if (HostRecord *host = find_host_by_endpoint(state.hosts.items, address, port); host != nullptr) {
      return host;
    }
    if (state.shell.activeScreen == ScreenId::apps && state.hosts.activeLoaded && host_matches_endpoint(state.hosts.active, address, port)) {
      return &state.hosts.active;
    }
    return nullptr;
  }

  /**
   * @brief Returns whether the given host is the active apps-screen selection.
   *
   * @param state Client state to inspect.
   * @param host Host potentially backing the apps screen.
   * @return True when the host is the active apps-screen selection.
   */
  bool app_list_result_targets_active_selection(const ClientState &state, const HostRecord *host) {
    return host != nullptr && state.shell.activeScreen == ScreenId::apps && state.hosts.activeLoaded && host == &state.hosts.active;
  }

  /**
   * @brief Returns the selected app identifier so it can be restored after a refresh.
   *
   * @param state Client state whose selected app should be preserved.
   * @return The selected app ID, or zero when no app is selected.
   */
  int selected_app_id_for_restore(const ClientState &state) {
    const HostAppRecord *currentSelection = selected_app(state);
    return currentSelection == nullptr ? 0 : currentSelection->id;
  }

  /**
   * @brief Applies a failed app-list refresh for an unpaired host.
   *
   * @param state Client state to update.
   * @param host Host receiving the failure.
   * @param hostIsActiveAppsScreenSelection True when the host backs the active apps screen.
   * @param message Failure message returned by the refresh task.
   */
  void apply_unpaired_app_list_failure(ClientState &state, HostRecord *host, bool hostIsActiveAppsScreenSelection, std::string message) {
    if (host == nullptr) {
      return;
    }

    const bool persistedAppCacheChanged = !host->apps.empty() || host->appListContentHash != 0U;
    host->pairingState = PairingState::not_paired;
    host->apps.clear();
    host->appListContentHash = 0;
    host->lastAppListRefreshTick = 0U;
    host->appListState = HostAppListState::failed;
    host->appListStatusMessage = message;
    state.hosts.dirty = state.hosts.dirty || persistedAppCacheChanged;
    if (hostIsActiveAppsScreenSelection) {
      state.shell.statusMessage = std::move(message);
    }
    refresh_running_flags(host);
    clamp_selected_app_index(state);
  }

  /**
   * @brief Applies a failed app-list refresh that can fall back to cached app data.
   *
   * @param state Client state to update.
   * @param host Host receiving the failure.
   * @param hostIsActiveAppsScreenSelection True when the host backs the active apps screen.
   * @param message Failure message returned by the refresh task.
   */
  void apply_cached_app_list_failure(ClientState &state, HostRecord *host, bool hostIsActiveAppsScreenSelection, std::string message) {
    if (host == nullptr) {
      return;
    }

    host->appListState = host->apps.empty() ? HostAppListState::failed : HostAppListState::ready;
    host->appListStatusMessage = host->apps.empty() ? message : "Using cached apps. Last refresh failed: " + message;
    if (hostIsActiveAppsScreenSelection) {
      state.shell.statusMessage = std::move(message);
    }
    refresh_running_flags(host);
    clamp_selected_app_index(state);
  }

  /**
   * @brief Merges saved app metadata into a freshly fetched app list.
   *
   * @param host Host providing persisted per-app metadata.
   * @param apps Freshly fetched apps to merge.
   * @return Merged app records ready to persist.
   */
  std::vector<HostAppRecord> merge_host_app_records(const HostRecord &host, std::vector<HostAppRecord> apps) {
    std::vector<HostAppRecord> mergedApps;
    mergedApps.reserve(apps.size());
    for (HostAppRecord &appRecord : apps) {
      if (const HostAppRecord *savedApp = find_app_by_id(host.apps, appRecord.id); savedApp != nullptr) {
        appRecord.hidden = appRecord.hidden || savedApp->hidden;
        appRecord.favorite = savedApp->favorite;
        appRecord.boxArtCached = appRecord.boxArtCached || savedApp->boxArtCached;
        if (appRecord.boxArtCacheKey.empty()) {
          appRecord.boxArtCacheKey = savedApp->boxArtCacheKey;
        }
      }
      appRecord.running = static_cast<uint32_t>(appRecord.id) == host.runningGameId;
      mergedApps.push_back(std::move(appRecord));
    }
    return mergedApps;
  }

  /**
   * @brief Restores the previously selected app after a successful refresh.
   *
   * @param state Client state whose selected app should be restored.
   * @param host Host containing the refreshed app list.
   * @param selectedAppId App ID that was selected before the refresh.
   */
  void restore_selected_app_after_refresh(ClientState &state, const HostRecord &host, int selectedAppId) {
    if (selectedAppId != 0) {
      const std::size_t restoredIndex = visible_app_index_for_id(host, state.apps.showHiddenApps, selectedAppId);
      if (restoredIndex != static_cast<std::size_t>(-1)) {
        state.apps.selectedAppIndex = restoredIndex;
      }
    }
    clamp_selected_app_index(state);
  }

  void apply_app_list_result(
    ClientState &state,
    const std::string &address,
    uint16_t port,
    std::vector<HostAppRecord> apps,
    uint64_t appListContentHash,
    bool success,
    std::string message
  ) {
    HostRecord *host = find_app_list_result_host(state, address, port);
    if (host == nullptr) {
      return;
    }

    const bool hostIsActiveAppsScreenSelection = app_list_result_targets_active_selection(state, host);
    const int selectedAppId = selected_app_id_for_restore(state);

    if (!success) {
      if (network::error_indicates_unpaired_client(message)) {
        apply_unpaired_app_list_failure(state, host, hostIsActiveAppsScreenSelection, std::move(message));
        return;
      }

      apply_cached_app_list_failure(state, host, hostIsActiveAppsScreenSelection, std::move(message));
      return;
    }

    bool persistedAppCacheChanged = false;
    if (const bool appListChanged = host->apps.empty() || host->appListContentHash == 0U || host->appListContentHash != appListContentHash; appListChanged) {
      host->apps = merge_host_app_records(*host, std::move(apps));
      persistedAppCacheChanged = true;
    } else {
      refresh_running_flags(host);
    }

    persistedAppCacheChanged = persistedAppCacheChanged || host->appListContentHash != appListContentHash;
    host->appListContentHash = appListContentHash;
    host->appListState = HostAppListState::ready;
    host->appListStatusMessage = message;
    state.hosts.dirty = state.hosts.dirty || persistedAppCacheChanged;
    if (hostIsActiveAppsScreenSelection) {
      state.shell.statusMessage.clear();
    }

    restore_selected_app_after_refresh(state, *host, selectedAppId);
  }

  void mark_cover_art_cached(ClientState &state, const std::string &address, uint16_t port, int appId) {
    HostRecord *host = find_host_by_endpoint(state.hosts.items, address, port);
    if (host == nullptr && state.shell.activeScreen == ScreenId::apps && state.hosts.activeLoaded && host_matches_endpoint(state.hosts.active, address, port)) {
      host = &state.hosts.active;
    }
    if (host == nullptr) {
      return;
    }

    for (HostAppRecord &appRecord : host->apps) {
      if (appRecord.id == appId) {
        if (appRecord.boxArtCached) {
          return;
        }
        appRecord.boxArtCached = true;
        state.hosts.dirty = true;
        return;
      }
    }
  }

  void set_log_file_path(ClientState &state, std::string logFilePath) {
    state.settings.logFilePath = std::move(logFilePath);
  }

  void apply_log_viewer_contents(ClientState &state, std::vector<std::string> lines, std::string statusMessage) {
    state.settings.logViewerLines = std::move(lines);
    state.settings.logViewerScrollOffset = 0U;
    state.shell.statusMessage = std::move(statusMessage);
    open_modal(state, ModalId::log_viewer);
  }

  bool host_requires_manual_pairing(const ClientState &state, const std::string &address, uint16_t port) {
    const std::string key = pairing_reset_endpoint_key(address, port);
    return !key.empty() && std::find(state.hosts.pairingResetEndpoints.begin(), state.hosts.pairingResetEndpoints.end(), key) != state.hosts.pairingResetEndpoints.end();
  }

  const HostRecord *selected_host(const ClientState &state) {
    if ((state.shell.activeScreen == ScreenId::apps || state.shell.activeScreen == ScreenId::pair_host) && state.hosts.activeLoaded) {
      return &state.hosts.active;
    }
    if (state.hosts.items.empty() || state.hosts.selectedHostIndex >= state.hosts.items.size()) {
      return nullptr;
    }
    return &state.hosts.items[state.hosts.selectedHostIndex];
  }

  const HostAppRecord *selected_app(const ClientState &state) {
    const HostRecord *host = apps_host(state);
    if (host == nullptr) {
      return nullptr;
    }
    const std::vector<std::size_t> indices = visible_app_indices(*host, state.apps.showHiddenApps);
    if (indices.empty()) {
      return nullptr;
    }
    const std::size_t visibleIndex = std::min(state.apps.selectedAppIndex, indices.size() - 1U);
    return &host->apps[indices[visibleIndex]];
  }

  const HostRecord *apps_host(const ClientState &state) {
    if (state.shell.activeScreen != ScreenId::apps || !state.hosts.activeLoaded) {
      return nullptr;
    }
    return &state.hosts.active;
  }

  /**
   * @brief Handles overlay toggle and scrolling commands.
   *
   * @param state Client state containing the overlay state.
   * @param command Command being processed.
   * @param update Update structure that receives overlay changes.
   * @return True when the command was consumed by the overlay.
   */
  bool handle_overlay_command(ClientState &state, input::UiCommand command, AppUpdate *update) {
    if (command == input::UiCommand::toggle_overlay) {
      state.shell.overlayVisible = !state.shell.overlayVisible;
      if (!state.shell.overlayVisible) {
        state.shell.overlayScrollOffset = 0U;
      }
      if (update != nullptr) {
        update->navigation.overlayChanged = true;
        update->navigation.overlayVisibilityChanged = true;
      }
      return true;
    }

    if (!state.shell.overlayVisible || update == nullptr) {
      return false;
    }

    switch (command) {
      case input::UiCommand::previous_page:
        state.shell.overlayScrollOffset += OVERLAY_SCROLL_STEP;
        update->navigation.overlayChanged = true;
        return true;
      case input::UiCommand::next_page:
        state.shell.overlayScrollOffset = state.shell.overlayScrollOffset > OVERLAY_SCROLL_STEP ? state.shell.overlayScrollOffset - OVERLAY_SCROLL_STEP : 0U;
        update->navigation.overlayChanged = true;
        return true;
      case input::UiCommand::fast_previous_page:
        state.shell.overlayScrollOffset += OVERLAY_SCROLL_STEP * 3U;
        update->navigation.overlayChanged = true;
        return true;
      case input::UiCommand::fast_next_page:
        {
          const std::size_t fastStep = OVERLAY_SCROLL_STEP * 3U;
          state.shell.overlayScrollOffset = state.shell.overlayScrollOffset > fastStep ? state.shell.overlayScrollOffset - fastStep : 0U;
          update->navigation.overlayChanged = true;
          return true;
        }
      case input::UiCommand::move_up:
      case input::UiCommand::move_down:
      case input::UiCommand::move_left:
      case input::UiCommand::move_right:
      case input::UiCommand::activate:
      case input::UiCommand::confirm:
      case input::UiCommand::back:
      case input::UiCommand::delete_character:
      case input::UiCommand::open_context_menu:
      case input::UiCommand::toggle_overlay:
      case input::UiCommand::none:
        return false;
    }

    return false;
  }

  /**
   * @brief Handles commands while the add-host keypad modal is visible.
   *
   * @param state Client state containing the keypad draft.
   * @param command Command being processed.
   * @return True when the command was consumed by the keypad.
   */
  bool handle_add_host_keypad_command(ClientState &state, input::UiCommand command) {
    if (state.shell.activeScreen != ScreenId::add_host || !state.addHostDraft.keypad.visible) {
      return false;
    }

    switch (command) {
      case input::UiCommand::move_up:
        move_add_host_keypad_selection(state, -1, 0);
        return true;
      case input::UiCommand::move_down:
        move_add_host_keypad_selection(state, 1, 0);
        return true;
      case input::UiCommand::move_left:
        move_add_host_keypad_selection(state, 0, -1);
        return true;
      case input::UiCommand::move_right:
        move_add_host_keypad_selection(state, 0, 1);
        return true;
      case input::UiCommand::back:
        cancel_add_host_keypad(state);
        return true;
      case input::UiCommand::delete_character:
        backspace_active_add_host_field(state);
        return true;
      case input::UiCommand::confirm:
        accept_add_host_keypad(state);
        return true;
      case input::UiCommand::activate:
        {
          if (char character = '\0'; selected_add_host_keypad_character(state, &character)) {
            append_to_active_add_host_field(state, character);
          }
          return true;
        }
      case input::UiCommand::open_context_menu:
      case input::UiCommand::previous_page:
      case input::UiCommand::next_page:
      case input::UiCommand::fast_previous_page:
      case input::UiCommand::fast_next_page:
      case input::UiCommand::toggle_overlay:
      case input::UiCommand::none:
        return true;
    }

    return true;
  }

  /**
   * @brief Handles activation of a settings detail row.
   *
   * @param state Client state containing the settings menus.
   * @param detailUpdate Activated detail-menu update.
   * @param update Update structure that receives side effects.
   */
  void handle_settings_detail_activation(ClientState &state, const ui::MenuUpdate &detailUpdate, AppUpdate *update) {
    if (update == nullptr) {
      return;
    }

    update->navigation.activatedItemId = detailUpdate.activatedItemId;
    if (detailUpdate.activatedItemId == "view-log-file") {
      update->requests.logViewRequested = true;
      return;
    }
    if (detailUpdate.activatedItemId == "cycle-log-level") {
      state.settings.loggingLevel = next_logging_level(state.settings.loggingLevel);
      state.settings.dirty = true;
      update->persistence.settingsChanged = true;
      state.shell.statusMessage = std::string("Logging level set to ") + logging::to_string(state.settings.loggingLevel);
      rebuild_menu(state, "cycle-log-level");
      return;
    }
    if (detailUpdate.activatedItemId == "cycle-xemu-console-log-level") {
      state.settings.xemuConsoleLoggingLevel = next_logging_level(state.settings.xemuConsoleLoggingLevel);
      state.settings.dirty = true;
      update->persistence.settingsChanged = true;
      state.shell.statusMessage = std::string("xemu console logging level set to ") + logging::to_string(state.settings.xemuConsoleLoggingLevel);
      rebuild_menu(state, "cycle-xemu-console-log-level");
      return;
    }
    if (detailUpdate.activatedItemId == "factory-reset") {
      open_confirmation(
        state,
        ConfirmationAction::factory_reset,
        "Factory Reset",
        {
          "Delete all Moonlight saved data?",
          "This removes hosts, settings, logs, pairing identity, and cached cover art.",
        }
      );
      update->navigation.modalOpened = true;
      return;
    }
    if (starts_with(detailUpdate.activatedItemId, DELETE_SAVED_FILE_MENU_ID_PREFIX)) {
      const std::string targetPath = detailUpdate.activatedItemId.substr(std::char_traits<char>::length(DELETE_SAVED_FILE_MENU_ID_PREFIX));
      open_confirmation(
        state,
        ConfirmationAction::delete_saved_file,
        "Delete Saved File",
        {
          "Delete this saved file?",
          targetPath,
        },
        targetPath
      );
      update->navigation.modalOpened = true;
      return;
    }
    state.shell.statusMessage = detailUpdate.activatedItemId + " is not implemented yet";
  }

  /**
   * @brief Handles commands on the settings screen.
   *
   * @param state Client state containing the settings menus.
   * @param command Command being processed.
   * @param update Update structure that receives side effects.
   * @return True when the settings screen consumed the command.
   */
  bool handle_settings_screen_command(ClientState &state, input::UiCommand command, AppUpdate *update) {
    if (state.shell.activeScreen != ScreenId::settings || update == nullptr) {
      return false;
    }

    if (command == input::UiCommand::move_left && state.settings.focusArea == SettingsFocusArea::options) {
      state.settings.focusArea = SettingsFocusArea::categories;
      return true;
    }
    if (command == input::UiCommand::move_right && state.settings.focusArea == SettingsFocusArea::categories && !state.detailMenu.items().empty()) {
      state.settings.focusArea = SettingsFocusArea::options;
      return true;
    }

    if (state.settings.focusArea == SettingsFocusArea::categories) {
      const ui::MenuUpdate categoryUpdate = state.menu.handle_command(command);
      if (categoryUpdate.backRequested) {
        set_screen(state, ScreenId::hosts);
        update->navigation.screenChanged = true;
        return true;
      }
      if (categoryUpdate.selectionChanged) {
        sync_selected_settings_category_from_menu(state);
        rebuild_settings_detail_menu(state, {}, false);
        return true;
      }
      if (!categoryUpdate.activationRequested) {
        return true;
      }

      update->navigation.activatedItemId = categoryUpdate.activatedItemId;
      sync_selected_settings_category_from_menu(state);
      rebuild_menu(state, categoryUpdate.activatedItemId);
      if (!state.detailMenu.items().empty()) {
        state.settings.focusArea = SettingsFocusArea::options;
      }
      return true;
    }

    const ui::MenuUpdate detailUpdate = state.detailMenu.handle_command(command);
    if (detailUpdate.backRequested) {
      state.settings.focusArea = SettingsFocusArea::categories;
      return true;
    }
    if (!detailUpdate.activationRequested) {
      return true;
    }

    handle_settings_detail_activation(state, detailUpdate, update);
    return true;
  }

  /**
   * @brief Activates a selected host from the hosts screen.
   *
   * @param state Client state containing the selected host.
   * @param update Update structure that receives browse or pairing work.
   */
  void activate_selected_host(ClientState &state, AppUpdate *update) {
    const HostRecord *host = selected_host(state);
    if (host == nullptr || update == nullptr) {
      return;
    }

    update->navigation.activatedItemId = "select-host";
    if (host->pairingState == PairingState::paired) {
      update->requests.appsBrowseRequested = true;
      update->requests.appsBrowseShowHidden = false;
      return;
    }

    request_host_pairing(state, *host, update);
  }

  /**
   * @brief Handles activation of the hosts toolbar.
   *
   * @param state Client state containing the toolbar selection.
   * @param update Update structure that receives side effects.
   */
  void activate_hosts_toolbar(ClientState &state, AppUpdate *update) {
    if (update == nullptr) {
      return;
    }

    const std::size_t toolbarIndex = state.hosts.selectedToolbarButtonIndex % HOST_TOOLBAR_BUTTON_COUNT;
    if (toolbarIndex == 0U) {
      set_screen(state, ScreenId::settings, settings_category_menu_id(SettingsCategory::logging));
      update->navigation.screenChanged = true;
      update->navigation.activatedItemId = "settings-button";
      return;
    }
    if (toolbarIndex == 1U) {
      open_modal(state, ModalId::support);
      update->navigation.modalOpened = true;
      update->navigation.activatedItemId = "support-button";
      return;
    }

    enter_add_host_screen(state);
    update->navigation.screenChanged = true;
    update->navigation.activatedItemId = "add-host-button";
  }

  /**
   * @brief Handles commands on the hosts screen.
   *
   * @param state Client state containing the hosts screen selection.
   * @param command Command being processed.
   * @param update Update structure that receives side effects.
   * @return True when the hosts screen consumed the command.
   */
  bool handle_hosts_screen_command(ClientState &state, input::UiCommand command, AppUpdate *update) {
    if (state.shell.activeScreen != ScreenId::hosts || update == nullptr) {
      return false;
    }

    switch (command) {
      case input::UiCommand::move_left:
        if (state.hosts.focusArea == HostsFocusArea::toolbar) {
          move_toolbar_selection(state, -1);
        } else {
          move_host_grid_selection(state, 0, -1);
        }
        return true;
      case input::UiCommand::move_right:
        if (state.hosts.focusArea == HostsFocusArea::toolbar) {
          move_toolbar_selection(state, 1);
        } else {
          move_host_grid_selection(state, 0, 1);
        }
        return true;
      case input::UiCommand::move_down:
        if (state.hosts.focusArea == HostsFocusArea::toolbar) {
          if (!state.hosts.items.empty()) {
            state.hosts.focusArea = HostsFocusArea::grid;
          }
        } else {
          move_host_grid_selection(state, 1, 0);
        }
        return true;
      case input::UiCommand::move_up:
        if (state.hosts.focusArea == HostsFocusArea::grid) {
          move_host_grid_selection(state, -1, 0);
        }
        return true;
      case input::UiCommand::open_context_menu:
        if (state.hosts.focusArea == HostsFocusArea::grid && selected_host(state) != nullptr) {
          open_modal(state, ModalId::host_actions);
          update->navigation.modalOpened = true;
        }
        return true;
      case input::UiCommand::activate:
      case input::UiCommand::confirm:
        if (state.hosts.focusArea == HostsFocusArea::toolbar) {
          activate_hosts_toolbar(state, update);
          return true;
        }
        activate_selected_host(state, update);
        return true;
      case input::UiCommand::back:
      case input::UiCommand::delete_character:
      case input::UiCommand::previous_page:
      case input::UiCommand::next_page:
      case input::UiCommand::fast_previous_page:
      case input::UiCommand::fast_next_page:
      case input::UiCommand::toggle_overlay:
      case input::UiCommand::none:
        return true;
    }

    return true;
  }

  /**
   * @brief Handles commands on the apps screen.
   *
   * @param state Client state containing the apps selection.
   * @param command Command being processed.
   * @param update Update structure that receives side effects.
   * @return True when the apps screen consumed the command.
   */
  bool handle_apps_screen_command(ClientState &state, input::UiCommand command, AppUpdate *update) {
    if (state.shell.activeScreen != ScreenId::apps || update == nullptr) {
      return false;
    }

    switch (command) {
      case input::UiCommand::move_left:
        move_app_grid_selection(state, 0, -1);
        return true;
      case input::UiCommand::move_right:
        move_app_grid_selection(state, 0, 1);
        return true;
      case input::UiCommand::move_up:
        move_app_grid_selection(state, -1, 0);
        return true;
      case input::UiCommand::move_down:
        move_app_grid_selection(state, 1, 0);
        return true;
      case input::UiCommand::open_context_menu:
        if (selected_app(state) != nullptr) {
          open_modal(state, ModalId::app_actions);
          update->navigation.modalOpened = true;
        }
        return true;
      case input::UiCommand::activate:
      case input::UiCommand::confirm:
        if (const HostAppRecord *appRecord = selected_app(state); appRecord != nullptr) {
          state.shell.statusMessage = "Launching " + appRecord->name + " is not implemented yet";
          update->navigation.activatedItemId = "launch-app";
        }
        return true;
      case input::UiCommand::back:
        state.shell.statusMessage.clear();
        set_screen(state, ScreenId::hosts);
        update->navigation.screenChanged = true;
        return true;
      case input::UiCommand::delete_character:
      case input::UiCommand::previous_page:
      case input::UiCommand::next_page:
      case input::UiCommand::fast_previous_page:
      case input::UiCommand::fast_next_page:
      case input::UiCommand::toggle_overlay:
      case input::UiCommand::none:
        return true;
    }

    return true;
  }

  /**
   * @brief Applies an invalid add-host draft error to the UI state.
   *
   * @param state Client state containing the add-host draft.
   * @param validationError Validation message to display.
   */
  void apply_add_host_validation_error(ClientState &state, std::string_view validationError) {
    state.addHostDraft.validationMessage = validationError;
    state.shell.statusMessage = validationError;
  }

  /**
   * @brief Handles activation of add-host screen menu actions.
   *
   * @param state Client state containing the add-host draft.
   * @param activatedItemId Activated menu item identifier.
   * @param update Update structure that receives side effects.
   */
  void handle_add_host_menu_activation(ClientState &state, std::string_view activatedItemId, AppUpdate *update) {
    if (update == nullptr) {
      return;
    }

    if (activatedItemId == "edit-address") {
      open_add_host_keypad(state, AddHostField::address);
      return;
    }
    if (activatedItemId == "edit-port") {
      open_add_host_keypad(state, AddHostField::port);
      return;
    }

    std::string normalizedAddress;
    uint16_t parsedPort = 0;
    std::string validationError;
    const bool draftIsValid = normalize_add_host_inputs(state, &normalizedAddress, &parsedPort, &validationError);
    if (activatedItemId == "test-connection") {
      if (!draftIsValid) {
        apply_add_host_validation_error(state, validationError);
        return;
      }
      state.addHostDraft.validationMessage.clear();
      state.addHostDraft.connectionMessage = "Testing connection to " + normalizedAddress + (parsedPort == 0 ? std::string {} : ":" + std::to_string(parsedPort)) + "...";
      state.shell.statusMessage = state.addHostDraft.connectionMessage;
      update->requests.connectionTestRequested = true;
      update->requests.connectionTestAddress = normalizedAddress;
      update->requests.connectionTestPort = effective_host_port(parsedPort);
      return;
    }
    if (activatedItemId == "save-host") {
      if (!draftIsValid) {
        apply_add_host_validation_error(state, validationError);
        return;
      }
      if (find_host_by_endpoint(state.hosts.items, normalizedAddress, parsedPort) != nullptr) {
        apply_add_host_validation_error(state, "That host is already saved");
        return;
      }

      state.hosts.items.push_back(make_host_record(normalizedAddress, parsedPort));
      state.hosts.selectedHostIndex = state.hosts.items.size() - 1U;
      state.hosts.focusArea = HostsFocusArea::grid;
      state.hosts.dirty = true;
      update->persistence.hostsChanged = true;
      state.addHostDraft.validationMessage.clear();
      state.addHostDraft.connectionMessage.clear();
      state.shell.statusMessage = "Saved host " + normalizedAddress;
      set_screen(state, ScreenId::hosts);
      update->navigation.screenChanged = true;
      return;
    }
    if (activatedItemId == "start-pairing") {
      if (!draftIsValid) {
        apply_add_host_validation_error(state, validationError);
        return;
      }

      const HostRecord *host = find_host_by_endpoint(state.hosts.items, normalizedAddress, parsedPort);
      if (host == nullptr) {
        state.hosts.items.push_back(make_host_record(normalizedAddress, parsedPort));
        state.hosts.selectedHostIndex = state.hosts.items.size() - 1U;
        state.hosts.dirty = true;
        update->persistence.hostsChanged = true;
        host = &state.hosts.items.back();
      }

      request_host_pairing(state, *host, update);
      return;
    }
    if (activatedItemId == "cancel-add-host") {
      state.addHostDraft.validationMessage.clear();
      state.addHostDraft.connectionMessage.clear();
      set_screen(state, ScreenId::hosts);
      update->navigation.screenChanged = true;
    }
  }

  AppUpdate handle_command(ClientState &state, input::UiCommand command) {
    AppUpdate update {};

    if (handle_overlay_command(state, command, &update)) {
      return update;
    }

    if (handle_add_host_keypad_command(state, command)) {
      return update;
    }

    if (handle_modal_command(state, command, &update)) {
      return update;
    }

    if (command == input::UiCommand::delete_character && !state.shell.statusMessage.empty()) {
      state.shell.statusMessage.clear();
      return update;
    }

    if (handle_settings_screen_command(state, command, &update)) {
      return update;
    }

    if (handle_hosts_screen_command(state, command, &update)) {
      return update;
    }

    if (handle_apps_screen_command(state, command, &update)) {
      return update;
    }

    const ui::MenuUpdate menuUpdate = state.menu.handle_command(command);
    if (menuUpdate.overlayToggleRequested) {
      state.shell.overlayVisible = !state.shell.overlayVisible;
      update.navigation.overlayChanged = true;
      update.navigation.overlayVisibilityChanged = true;
      return update;
    }

    if (menuUpdate.backRequested) {
      if (state.shell.activeScreen == ScreenId::settings || state.shell.activeScreen == ScreenId::add_host || state.shell.activeScreen == ScreenId::pair_host) {
        if (state.shell.activeScreen == ScreenId::pair_host) {
          update.requests.pairingCancelledRequested = true;
        }
        state.shell.statusMessage = state.shell.activeScreen == ScreenId::apps ? std::string {} : state.shell.statusMessage;
        set_screen(state, ScreenId::hosts);
        update.navigation.screenChanged = true;
      }
      return update;
    }

    if (!menuUpdate.activationRequested) {
      return update;
    }

    update.navigation.activatedItemId = menuUpdate.activatedItemId;

    if (state.shell.activeScreen == ScreenId::pair_host) {
      if (menuUpdate.activatedItemId == "cancel-pairing") {
        update.requests.pairingCancelledRequested = true;
        set_screen(state, ScreenId::hosts);
        update.navigation.screenChanged = true;
      }
      return update;
    }

    if (state.shell.activeScreen != ScreenId::add_host) {
      return update;
    }

    handle_add_host_menu_activation(state, menuUpdate.activatedItemId, &update);
    return update;
  }

}  // namespace app
