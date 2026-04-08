// class header include
#include "src/app/client_state.h"

// standard includes
#include "src/network/host_pairing.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

    if (std::find(state.pairingResetEndpoints.begin(), state.pairingResetEndpoints.end(), key) == state.pairingResetEndpoints.end()) {
      state.pairingResetEndpoints.push_back(key);
    }
  }

  void clear_deleted_host_pairing(app::ClientState &state, const std::string &address, uint16_t port) {
    const std::string key = pairing_reset_endpoint_key(address, port);
    if (key.empty()) {
      return;
    }

    state.pairingResetEndpoints.erase(
      std::remove(state.pairingResetEndpoints.begin(), state.pairingResetEndpoints.end(), key),
      state.pairingResetEndpoints.end()
    );
  }

  void sync_selected_settings_category_from_menu(app::ClientState &state) {
    if (const ui::MenuItem *selectedItem = state.menu.selected_item(); selectedItem != nullptr) {
      state.selectedSettingsCategory = settings_category_from_menu_id(selectedItem->id);
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

  const app::HostRecord *find_host_by_endpoint(const std::vector<app::HostRecord> &hosts, const std::string &address, uint16_t port) {  // NOSONAR(cpp:S1144) used by endpoint-aware selection and background update flows
    const auto iterator = std::find_if(hosts.begin(), hosts.end(), [&address, port](const app::HostRecord &host) {
      return app::host_matches_endpoint(host, address, port);
    });
    return iterator == hosts.end() ? nullptr : &(*iterator);
  }

  app::HostRecord *find_host_by_endpoint(std::vector<app::HostRecord> &hosts, const std::string &address, uint16_t port) {
    const auto iterator = std::find_if(hosts.begin(), hosts.end(), [&address, port](const app::HostRecord &host) {
      return app::host_matches_endpoint(host, address, port);
    });
    return iterator == hosts.end() ? nullptr : &(*iterator);
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
    if (state.hosts.empty()) {
      state.selectedHostIndex = 0U;
      state.hostsFocusArea = app::HostsFocusArea::toolbar;
      state.selectedToolbarButtonIndex = DEFAULT_EMPTY_HOSTS_TOOLBAR_INDEX;
      return;
    }

    if (state.selectedHostIndex >= state.hosts.size()) {
      state.selectedHostIndex = state.hosts.size() - 1U;
    }
  }

  void reset_hosts_home_selection(app::ClientState &state) {
    if (state.hosts.empty()) {
      state.hostsFocusArea = app::HostsFocusArea::toolbar;
      state.selectedToolbarButtonIndex = DEFAULT_EMPTY_HOSTS_TOOLBAR_INDEX;
      state.selectedHostIndex = 0U;
      return;
    }

    state.hostsFocusArea = app::HostsFocusArea::grid;
    state.selectedHostIndex = 0U;
  }

  void clamp_selected_app_index(app::ClientState &state) {
    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr) {
      state.selectedAppIndex = 0U;
      return;
    }

    const std::vector<std::size_t> indices = visible_app_indices(*host, state.showHiddenApps);
    if (indices.empty()) {
      state.selectedAppIndex = 0U;
      return;
    }

    if (state.selectedAppIndex >= indices.size()) {
      state.selectedAppIndex = indices.size() - 1U;
    }
  }

  std::vector<ui::MenuItem> build_menu_for_state(const app::ClientState &state) {
    switch (state.activeScreen) {
      case app::ScreenId::settings:
        return {
          {settings_category_menu_id(app::SettingsCategory::logging), "Logging", true},
          {settings_category_menu_id(app::SettingsCategory::display), "Display", true},
          {settings_category_menu_id(app::SettingsCategory::input), "Input", true},
          {settings_category_menu_id(app::SettingsCategory::reset), "Reset", true},
        };
      case app::ScreenId::add_host:
        return {
          {"edit-address", "Host Address", true},
          {"edit-port", "Host Port", true},
          {"test-connection", "Test Connection", true},
          {"start-pairing", "Start Pairing", true},
          {"save-host", "Save Host", true},
          {"cancel-add-host", "Cancel", true},
        };
      case app::ScreenId::pair_host:
        return {
          {"cancel-pairing", "Cancel", true},
        };
      case app::ScreenId::home:
      case app::ScreenId::hosts:
      case app::ScreenId::apps:
        return {};
    }

    return {};
  }

  std::vector<ui::MenuItem> build_detail_menu_for_state(const app::ClientState &state) {
    if (state.activeScreen != app::ScreenId::settings) {
      return {};
    }

    switch (state.selectedSettingsCategory) {
      case app::SettingsCategory::logging:
        return {
          {"view-log-file", "View Log File", true},
          {"cycle-log-level", std::string("Logging Level: ") + logging::to_string(state.loggingLevel), true},
        };
      case app::SettingsCategory::display:
        return {
          {"display-placeholder", "Display settings are not implemented yet", true},
        };
      case app::SettingsCategory::input:
        return {
          {"input-placeholder", "Input settings are not implemented yet", true},
        };
      case app::SettingsCategory::reset:
        {
          std::vector<ui::MenuItem> items = {
            {"factory-reset", "Factory Reset", true},
          };
          for (const startup::SavedFileEntry &savedFile : state.savedFiles) {
            items.push_back({std::string(DELETE_SAVED_FILE_MENU_ID_PREFIX) + savedFile.path, "Delete " + savedFile.displayName, true});
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
    state.activeScreen = screen;
    if (screen == app::ScreenId::settings) {
      state.savedFilesDirty = true;
      state.settingsFocusArea = app::SettingsFocusArea::categories;
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
    switch (state.logViewerPlacement) {
      case app::LogViewerPlacement::full:
        state.logViewerPlacement = app::LogViewerPlacement::left;
        return;
      case app::LogViewerPlacement::left:
        state.logViewerPlacement = app::LogViewerPlacement::right;
        return;
      case app::LogViewerPlacement::right:
        state.logViewerPlacement = app::LogViewerPlacement::full;
        return;
    }
  }

  void scroll_log_viewer(app::ClientState &state, bool towardOlderEntries, std::size_t step) {
    if (state.logViewerLines.empty() || step == 0U) {
      state.logViewerScrollOffset = 0U;
      return;
    }

    const std::size_t maxOffset = state.logViewerLines.size() > 1U ? state.logViewerLines.size() - 1U : 0U;
    if (towardOlderEntries) {
      state.logViewerScrollOffset = std::min(maxOffset, state.logViewerScrollOffset + step);
      return;
    }

    state.logViewerScrollOffset = state.logViewerScrollOffset > step ? state.logViewerScrollOffset - step : 0U;
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
    state.statusMessage = field == app::AddHostField::address ? "Editing host address" : "Editing host port";
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
      state.statusMessage = "Updated host address";
    } else {
      state.addHostDraft.portInput = state.addHostDraft.keypad.stagedInput;
      state.statusMessage = state.addHostDraft.portInput.empty() ? "Using default Moonlight host port 47989" : "Updated host port";
    }

    state.addHostDraft.validationMessage.clear();
    state.addHostDraft.connectionMessage.clear();
    close_add_host_keypad(state);
  }

  void cancel_add_host_keypad(app::ClientState &state) {
    state.statusMessage = state.addHostDraft.activeField == app::AddHostField::address ? "Cancelled host address edit" : "Cancelled host port edit";
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
    const std::size_t targetRowWidth = std::min<std::size_t>(ADD_HOST_KEYPAD_COLUMN_COUNT, layout.buttonCount - targetRowStart);
    if (columnDelta != 0 && targetRowWidth > 0U) {
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
    const std::size_t current = state.selectedToolbarButtonIndex % HOST_TOOLBAR_BUTTON_COUNT;
    state.selectedToolbarButtonIndex = direction < 0 ? (current + HOST_TOOLBAR_BUTTON_COUNT - 1U) % HOST_TOOLBAR_BUTTON_COUNT : (current + 1U) % HOST_TOOLBAR_BUTTON_COUNT;
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
    if (state.hosts.empty()) {
      state.hostsFocusArea = app::HostsFocusArea::toolbar;
      return;
    }

    bool movedAboveFirstRow = false;
    move_grid_selection(state.hosts.size(), HOST_GRID_COLUMN_COUNT, rowDelta, columnDelta, &state.selectedHostIndex, &movedAboveFirstRow);
    if (movedAboveFirstRow) {
      state.hostsFocusArea = app::HostsFocusArea::toolbar;
      return;
    }
    state.hostsFocusArea = app::HostsFocusArea::grid;
  }

  void move_app_grid_selection(app::ClientState &state, int rowDelta, int columnDelta) {
    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr) {
      state.selectedAppIndex = 0U;
      return;
    }

    const std::vector<std::size_t> indices = visible_app_indices(*host, state.showHiddenApps);
    if (indices.empty()) {
      state.selectedAppIndex = 0U;
      return;
    }

    move_grid_selection(indices.size(), APP_GRID_COLUMN_COUNT, rowDelta, columnDelta, &state.selectedAppIndex);
  }

  void enter_add_host_screen(app::ClientState &state) {
    reset_add_host_draft(state, state.activeScreen == app::ScreenId::add_host ? app::ScreenId::hosts : state.activeScreen);
    set_screen(state, app::ScreenId::add_host, "edit-address");
  }

  bool enter_pair_host_screen(app::ClientState &state, const std::string &address, uint16_t port) {
    if (const app::HostRecord *host = find_host_by_endpoint(state.hosts, address, port); host != nullptr && host->reachability == app::HostReachability::offline) {
      state.statusMessage = "Host is offline. Bring it online before pairing.";
      return false;
    }

    std::string pairingPin;
    if (std::string pinError; !network::generate_pairing_pin(&pairingPin, &pinError)) {
      state.statusMessage = pinError.empty() ? "Failed to generate a secure pairing PIN." : std::move(pinError);
      return false;
    }

    state.pairingDraft = app::create_pairing_draft(address, app::effective_host_port(port), pairingPin);
    set_screen(state, app::ScreenId::pair_host, "cancel-pairing");
    return true;
  }

  bool enter_apps_screen(app::ClientState &state, bool showHiddenApps) {
    app::HostRecord *host = state.hosts.empty() ? nullptr : &state.hosts[state.selectedHostIndex];
    if (host == nullptr) {
      return false;
    }
    if (host->reachability == app::HostReachability::offline) {
      state.statusMessage = "Host is offline. Bring it online before opening apps.";
      return false;
    }
    if (host->pairingState != app::PairingState::paired) {
      state.statusMessage = "This host is no longer paired. Pair it again before opening apps.";
      return false;
    }

    state.showHiddenApps = showHiddenApps;
    state.selectedAppIndex = 0U;
    state.appsScrollPage = 0U;
    host->appListState = app::HostAppListState::loading;
    host->appListStatusMessage = (host->apps.empty() ? "Loading apps for " : "Refreshing apps for ") + host->displayName + "...";
    state.statusMessage.clear();
    set_screen(state, app::ScreenId::apps);
    return true;
  }

  void select_host_by_endpoint(app::ClientState &state, const std::string &address, uint16_t port) {
    for (std::size_t index = 0; index < state.hosts.size(); ++index) {
      if (app::host_matches_endpoint(state.hosts[index], address, port)) {
        state.selectedHostIndex = index;
        state.hostsFocusArea = app::HostsFocusArea::grid;
        return;
      }
    }
  }

  logging::LogLevel next_logging_level(logging::LogLevel currentLevel) {
    switch (currentLevel) {
      case logging::LogLevel::trace:
        return logging::LogLevel::warning;
      case logging::LogLevel::debug:
        return logging::LogLevel::trace;
      case logging::LogLevel::info:
        return logging::LogLevel::debug;
      case logging::LogLevel::warning:
        return logging::LogLevel::error;
      case logging::LogLevel::error:
        return logging::LogLevel::info;
    }
    return logging::LogLevel::info;
  }

  bool handle_modal_command(app::ClientState &state, input::UiCommand command, app::AppUpdate *update) {  // NOSONAR(cpp:S3776) modal command routing stays centralized for predictable UI behavior
    if (!state.modal.active()) {
      return false;
    }

    if (state.modal.id == app::ModalId::log_viewer) {
      switch (command) {
        case input::UiCommand::back:
        case input::UiCommand::activate:
        case input::UiCommand::confirm:
          close_modal(state);
          update->modalClosed = true;
          return true;
        case input::UiCommand::delete_character:
        case input::UiCommand::open_context_menu:
          cycle_log_viewer_placement(state);
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
    }

    if (command == input::UiCommand::back) {
      close_modal(state);
      update->modalClosed = true;
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

    switch (state.modal.id) {
      case app::ModalId::support:
      case app::ModalId::host_details:
      case app::ModalId::app_details:
        close_modal(state);
        update->modalClosed = true;
        return true;
      case app::ModalId::log_viewer:
        return true;
      case app::ModalId::confirmation:
        {
          const bool confirmed = state.modal.selectedActionIndex % 2U == 0U;
          const app::ConfirmationAction action = state.confirmation.action;
          const std::string targetPath = state.confirmation.targetPath;
          close_modal(state);
          update->modalClosed = true;
          if (!confirmed) {
            state.statusMessage = "Cancelled the pending reset action";
            return true;
          }
          if (action == app::ConfirmationAction::delete_saved_file) {
            update->savedFileDeleteRequested = true;
            update->savedFileDeletePath = targetPath;
            return true;
          }
          if (action == app::ConfirmationAction::factory_reset) {
            update->factoryResetRequested = true;
            return true;
          }
          return true;
        }
      case app::ModalId::host_actions:
        {
          const app::HostRecord *host = app::selected_host(state);
          if (host == nullptr) {
            close_modal(state);
            update->modalClosed = true;
            return true;
          }

          switch (state.modal.selectedActionIndex % 4U) {
            case 0:
              close_modal(state);
              update->modalClosed = true;
              if (host->pairingState == app::PairingState::paired) {
                update->appsBrowseRequested = true;
                update->appsBrowseShowHidden = true;
              } else {
                if (enter_pair_host_screen(state, host->address, host->port)) {  // NOSONAR(cpp:S134) host action flow is intentionally kept inline with its UI side effects
                  update->screenChanged = true;
                  update->pairingRequested = true;
                  update->pairingAddress = state.pairingDraft.targetAddress;
                  update->pairingPort = state.pairingDraft.targetPort;
                  update->pairingPin = state.pairingDraft.generatedPin;
                }
              }
              return true;
            case 1:
              close_modal(state);
              update->modalClosed = true;
              update->connectionTestRequested = true;
              update->connectionTestAddress = host->address;
              update->connectionTestPort = app::effective_host_port(host->port);
              return true;
            case 2:
              if (state.selectedHostIndex < state.hosts.size()) {
                const app::HostRecord deletedHost = state.hosts[state.selectedHostIndex];
                remember_deleted_host_pairing(state, deletedHost);
                update->hostDeleteCleanupRequested = true;
                update->deletedHostAddress = deletedHost.address;
                update->deletedHostPort = deletedHost.port;
                update->deletedHostWasPaired = deletedHost.pairingState == app::PairingState::paired;
                for (const app::HostAppRecord &appRecord : deletedHost.apps) {
                  if (!appRecord.boxArtCacheKey.empty() && std::find(update->deletedHostCoverArtCacheKeys.begin(), update->deletedHostCoverArtCacheKeys.end(), appRecord.boxArtCacheKey) == update->deletedHostCoverArtCacheKeys.end()) {
                    update->deletedHostCoverArtCacheKeys.push_back(appRecord.boxArtCacheKey);
                  }
                }
                state.hosts.erase(state.hosts.begin() + static_cast<std::ptrdiff_t>(state.selectedHostIndex));
                state.hostsDirty = true;
                update->hostsChanged = true;
                clamp_selected_host_index(state);
                close_modal(state);
                update->modalClosed = true;
                state.statusMessage = "Deleted saved host";
              }
              return true;
            case 3:
              open_modal(state, app::ModalId::host_details);
              return true;
            default:
              return true;
          }
          return true;
        }
      case app::ModalId::app_actions:
        {
          const app::HostRecord *host = app::apps_host(state);
          if (const app::HostAppRecord *selectedApp = app::selected_app(state); host == nullptr || selectedApp == nullptr) {
            close_modal(state);
            update->modalClosed = true;
            return true;
          }

          app::HostRecord *mutableHost = &state.hosts[state.selectedHostIndex];
          const std::vector<std::size_t> indices = visible_app_indices(*mutableHost, state.showHiddenApps);
          if (indices.empty()) {
            close_modal(state);
            update->modalClosed = true;
            return true;
          }
          app::HostAppRecord &appRecord = mutableHost->apps[indices[state.selectedAppIndex]];

          switch (state.modal.selectedActionIndex % 3U) {
            case 0:
              appRecord.hidden = !appRecord.hidden;
              close_modal(state);
              update->modalClosed = true;
              clamp_selected_app_index(state);
              return true;
            case 1:
              open_modal(state, app::ModalId::app_details);
              return true;
            case 2:
              appRecord.favorite = !appRecord.favorite;
              close_modal(state);
              update->modalClosed = true;
              return true;
            default:
              return true;
          }
          return true;
        }
      case app::ModalId::none:
        return false;
    }

    return false;
  }

}  // namespace

namespace app {

  ClientState create_initial_state() {
    return {
      ScreenId::hosts,
      false,
      false,
      false,
      0U,
      HostsFocusArea::toolbar,
      DEFAULT_EMPTY_HOSTS_TOOLBAR_INDEX,
      0U,
      0U,
      0U,
      false,
      ui::MenuModel(),
      ui::MenuModel(),
      {},
      {{}, {}, AddHostField::address, {false, 0U, {}}, ScreenId::hosts, {}, {}, false},
      {{}, DEFAULT_HOST_PORT, {}, PairingStage::idle, {}},
      {},
      SettingsFocusArea::categories,
      SettingsCategory::logging,
      {},
      {},
      {},
      {},
      0U,
      LogViewerPlacement::full,
      logging::LogLevel::info,
      {},
      true,
      {},
    };
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
    state.hosts = std::move(hosts);
    state.hostsDirty = false;
    state.statusMessage = std::move(statusMessage);
    reset_hosts_home_selection(state);
    clamp_selected_host_index(state);
    clamp_selected_app_index(state);

    if (state.activeScreen == ScreenId::settings || state.activeScreen == ScreenId::add_host || state.activeScreen == ScreenId::pair_host) {
      rebuild_menu(state);
    }
  }

  void replace_saved_files(ClientState &state, std::vector<startup::SavedFileEntry> savedFiles) {
    state.savedFiles = std::move(savedFiles);
    state.savedFilesDirty = false;
    if (state.activeScreen == ScreenId::settings) {
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
    if (state.activeScreen == ScreenId::add_host) {
      state.addHostDraft.connectionMessage = message;
      state.addHostDraft.lastConnectionSucceeded = success;
    }
    if (!state.hosts.empty() && state.selectedHostIndex < state.hosts.size()) {
      state.hosts[state.selectedHostIndex].reachability = success ? HostReachability::online : HostReachability::offline;
    }
    state.statusMessage = std::move(message);
  }

  bool apply_pairing_result(ClientState &state, const std::string &address, uint16_t port, bool success, std::string message) {
    state.pairingDraft.targetAddress = address;
    state.pairingDraft.targetPort = effective_host_port(port);
    state.pairingDraft.stage = success ? PairingStage::paired : PairingStage::failed;
    if (!success) {
      state.pairingDraft.generatedPin.clear();
    }
    state.pairingDraft.statusMessage = message;
    state.statusMessage = std::move(message);

    HostRecord *host = find_host_by_endpoint(state.hosts, address, port);
    if (host == nullptr) {
      return false;
    }

    if (success) {
      clear_deleted_host_pairing(state, address, port);
      host->pairingState = PairingState::paired;
      host->reachability = HostReachability::online;
      select_host_by_endpoint(state, address, port);
      set_screen(state, ScreenId::hosts);
      state.hostsDirty = true;
      return true;
    }

    host->pairingState = PairingState::not_paired;
    return false;
  }

  void apply_app_list_result(  // NOSONAR(cpp:S3776) app-list merge logic is intentionally centralized to preserve host selection state
    ClientState &state,
    const std::string &address,
    uint16_t port,
    std::vector<HostAppRecord> apps,
    uint64_t appListContentHash,
    bool success,
    std::string message
  ) {
    HostRecord *host = find_host_by_endpoint(state.hosts, address, port);
    if (host == nullptr) {
      return;
    }

    const bool hostIsActiveAppsScreenSelection = state.activeScreen == ScreenId::apps && selected_host(state) == host;
    const HostAppRecord *currentSelection = selected_app(state);
    const int selectedAppId = currentSelection == nullptr ? 0 : currentSelection->id;

    if (!success) {
      if (const bool hostIsUnpaired = network::error_indicates_unpaired_client(message); hostIsUnpaired) {
        host->pairingState = PairingState::not_paired;
        host->apps.clear();
        host->appListContentHash = 0;
        host->lastAppListRefreshTick = 0U;
        host->appListState = HostAppListState::failed;
        host->appListStatusMessage = message;
        if (hostIsActiveAppsScreenSelection) {
          state.statusMessage = std::move(message);
        }
        refresh_running_flags(host);
        clamp_selected_app_index(state);
        return;
      }
      host->appListState = host->apps.empty() ? HostAppListState::failed : HostAppListState::ready;
      host->appListStatusMessage = host->apps.empty() ? message : "Using cached apps. Last refresh failed: " + message;
      if (hostIsActiveAppsScreenSelection) {
        state.statusMessage = std::move(message);
      }
      refresh_running_flags(host);
      clamp_selected_app_index(state);
      return;
    }

    if (const bool appListChanged = host->apps.empty() || host->appListContentHash == 0U || host->appListContentHash != appListContentHash; appListChanged) {
      std::vector<HostAppRecord> mergedApps;
      mergedApps.reserve(apps.size());
      for (HostAppRecord &appRecord : apps) {
        if (const HostAppRecord *savedApp = find_app_by_id(host->apps, appRecord.id); savedApp != nullptr) {
          appRecord.hidden = appRecord.hidden || savedApp->hidden;
          appRecord.favorite = savedApp->favorite;
          appRecord.boxArtCached = appRecord.boxArtCached || savedApp->boxArtCached;
          if (appRecord.boxArtCacheKey.empty()) {  // NOSONAR(cpp:S134) merge path keeps persisted app metadata updates together
            appRecord.boxArtCacheKey = savedApp->boxArtCacheKey;
          }
        }
        appRecord.running = static_cast<uint32_t>(appRecord.id) == host->runningGameId;
        mergedApps.push_back(std::move(appRecord));
      }
      host->apps = std::move(mergedApps);
    } else {
      refresh_running_flags(host);
    }

    host->appListContentHash = appListContentHash;
    host->appListState = HostAppListState::ready;
    host->appListStatusMessage = message;
    if (hostIsActiveAppsScreenSelection) {
      state.statusMessage.clear();
    }

    if (selectedAppId != 0) {
      const std::size_t restoredIndex = visible_app_index_for_id(*host, state.showHiddenApps, selectedAppId);
      if (restoredIndex != static_cast<std::size_t>(-1)) {
        state.selectedAppIndex = restoredIndex;
      }
    }
    clamp_selected_app_index(state);
  }

  void mark_cover_art_cached(ClientState &state, const std::string &address, uint16_t port, int appId) {
    HostRecord *host = find_host_by_endpoint(state.hosts, address, port);
    if (host == nullptr) {
      return;
    }

    for (HostAppRecord &appRecord : host->apps) {
      if (appRecord.id == appId) {
        appRecord.boxArtCached = true;
        return;
      }
    }
  }

  void set_log_file_path(ClientState &state, std::string logFilePath) {
    state.logFilePath = std::move(logFilePath);
  }

  void apply_log_viewer_contents(ClientState &state, std::vector<std::string> lines, std::string statusMessage) {
    state.logViewerLines = std::move(lines);
    state.logViewerScrollOffset = 0U;
    state.statusMessage = std::move(statusMessage);
    open_modal(state, ModalId::log_viewer);
  }

  bool host_requires_manual_pairing(const ClientState &state, const std::string &address, uint16_t port) {
    const std::string key = pairing_reset_endpoint_key(address, port);
    return !key.empty() && std::find(state.pairingResetEndpoints.begin(), state.pairingResetEndpoints.end(), key) != state.pairingResetEndpoints.end();
  }

  const HostRecord *selected_host(const ClientState &state) {
    if (state.hosts.empty() || state.selectedHostIndex >= state.hosts.size()) {
      return nullptr;
    }
    return &state.hosts[state.selectedHostIndex];
  }

  const HostAppRecord *selected_app(const ClientState &state) {
    const HostRecord *host = apps_host(state);
    if (host == nullptr) {
      return nullptr;
    }
    const std::vector<std::size_t> indices = visible_app_indices(*host, state.showHiddenApps);
    if (indices.empty()) {
      return nullptr;
    }
    const std::size_t visibleIndex = std::min(state.selectedAppIndex, indices.size() - 1U);
    return &host->apps[indices[visibleIndex]];
  }

  const HostRecord *apps_host(const ClientState &state) {
    return selected_host(state);
  }

  AppUpdate handle_command(ClientState &state, input::UiCommand command) {  // NOSONAR(cpp:S3776) top-level UI command routing intentionally remains in one place
    AppUpdate update {};

    if (command == input::UiCommand::toggle_overlay) {
      state.overlayVisible = !state.overlayVisible;
      if (!state.overlayVisible) {
        state.overlayScrollOffset = 0U;
      }
      update.overlayChanged = true;
      update.overlayVisibilityChanged = true;
      return update;
    }

    if (state.overlayVisible) {
      if (command == input::UiCommand::previous_page) {
        state.overlayScrollOffset += OVERLAY_SCROLL_STEP;
        update.overlayChanged = true;
        return update;
      }
      if (command == input::UiCommand::next_page) {
        state.overlayScrollOffset = state.overlayScrollOffset > OVERLAY_SCROLL_STEP ? state.overlayScrollOffset - OVERLAY_SCROLL_STEP : 0U;
        update.overlayChanged = true;
        return update;
      }
      if (command == input::UiCommand::fast_previous_page) {
        state.overlayScrollOffset += OVERLAY_SCROLL_STEP * 3U;
        update.overlayChanged = true;
        return update;
      }
      if (command == input::UiCommand::fast_next_page) {
        const std::size_t fastStep = OVERLAY_SCROLL_STEP * 3U;
        state.overlayScrollOffset = state.overlayScrollOffset > fastStep ? state.overlayScrollOffset - fastStep : 0U;
        update.overlayChanged = true;
        return update;
      }
    }

    if (state.activeScreen == ScreenId::add_host && state.addHostDraft.keypad.visible) {
      switch (command) {
        case input::UiCommand::move_up:
          move_add_host_keypad_selection(state, -1, 0);
          return update;
        case input::UiCommand::move_down:
          move_add_host_keypad_selection(state, 1, 0);
          return update;
        case input::UiCommand::move_left:
          move_add_host_keypad_selection(state, 0, -1);
          return update;
        case input::UiCommand::move_right:
          move_add_host_keypad_selection(state, 0, 1);
          return update;
        case input::UiCommand::back:
          cancel_add_host_keypad(state);
          return update;
        case input::UiCommand::delete_character:
          backspace_active_add_host_field(state);
          return update;
        case input::UiCommand::confirm:
          accept_add_host_keypad(state);
          return update;
        case input::UiCommand::activate:
          {
            char character = '\0';
            if (selected_add_host_keypad_character(state, &character)) {
              append_to_active_add_host_field(state, character);
            }
            return update;
          }
        case input::UiCommand::open_context_menu:
        case input::UiCommand::previous_page:
        case input::UiCommand::next_page:
        case input::UiCommand::fast_previous_page:
        case input::UiCommand::fast_next_page:
        case input::UiCommand::toggle_overlay:
        case input::UiCommand::none:
          return update;
      }
    }

    if (handle_modal_command(state, command, &update)) {
      return update;
    }

    if (command == input::UiCommand::delete_character && !state.statusMessage.empty()) {
      state.statusMessage.clear();
      return update;
    }

    if (state.activeScreen == ScreenId::settings) {
      if (command == input::UiCommand::move_left && state.settingsFocusArea == SettingsFocusArea::options) {
        state.settingsFocusArea = SettingsFocusArea::categories;
        return update;
      }
      if (command == input::UiCommand::move_right && state.settingsFocusArea == SettingsFocusArea::categories && !state.detailMenu.items().empty()) {
        state.settingsFocusArea = SettingsFocusArea::options;
        return update;
      }

      if (state.settingsFocusArea == SettingsFocusArea::categories) {
        const ui::MenuUpdate categoryUpdate = state.menu.handle_command(command);
        if (categoryUpdate.backRequested) {
          set_screen(state, ScreenId::hosts);
          update.screenChanged = true;
          return update;
        }
        if (categoryUpdate.selectionChanged) {
          sync_selected_settings_category_from_menu(state);
          rebuild_settings_detail_menu(state, {}, false);
          return update;
        }
        if (!categoryUpdate.activationRequested) {
          return update;
        }

        update.activatedItemId = categoryUpdate.activatedItemId;
        sync_selected_settings_category_from_menu(state);
        rebuild_menu(state, categoryUpdate.activatedItemId);
        if (!state.detailMenu.items().empty()) {
          state.settingsFocusArea = SettingsFocusArea::options;
        }
        return update;
      }

      const ui::MenuUpdate detailUpdate = state.detailMenu.handle_command(command);
      if (detailUpdate.backRequested) {
        state.settingsFocusArea = SettingsFocusArea::categories;
        return update;
      }
      if (!detailUpdate.activationRequested) {
        return update;
      }

      update.activatedItemId = detailUpdate.activatedItemId;
      if (detailUpdate.activatedItemId == "view-log-file") {
        update.logViewRequested = true;
        return update;
      }
      if (detailUpdate.activatedItemId == "cycle-log-level") {
        state.loggingLevel = next_logging_level(state.loggingLevel);
        state.statusMessage = std::string("Logging level set to ") + logging::to_string(state.loggingLevel);
        rebuild_menu(state, "cycle-log-level");
        return update;
      }
      if (detailUpdate.activatedItemId == "factory-reset") {
        open_confirmation(
          state,
          ConfirmationAction::factory_reset,
          "Factory Reset",
          {
            "Delete all Moonlight saved data?",
            "This removes hosts, logs, pairing identity, and cached cover art.",
          }
        );
        update.modalOpened = true;
        return update;
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
        update.modalOpened = true;
        return update;
      }
      state.statusMessage = detailUpdate.activatedItemId + " is not implemented yet";
      return update;
    }

    if (state.activeScreen == ScreenId::hosts) {
      switch (command) {
        case input::UiCommand::move_left:
          if (state.hostsFocusArea == HostsFocusArea::toolbar) {
            move_toolbar_selection(state, -1);
          } else {
            move_host_grid_selection(state, 0, -1);
          }
          return update;
        case input::UiCommand::move_right:
          if (state.hostsFocusArea == HostsFocusArea::toolbar) {
            move_toolbar_selection(state, 1);
          } else {
            move_host_grid_selection(state, 0, 1);
          }
          return update;
        case input::UiCommand::move_down:
          if (state.hostsFocusArea == HostsFocusArea::toolbar) {
            if (!state.hosts.empty()) {  // NOSONAR(cpp:S134) hosts-screen focus transition stays inline with navigation handling
              state.hostsFocusArea = HostsFocusArea::grid;
            }
          } else {
            move_host_grid_selection(state, 1, 0);
          }
          return update;
        case input::UiCommand::move_up:
          if (state.hostsFocusArea == HostsFocusArea::grid) {
            move_host_grid_selection(state, -1, 0);
          }
          return update;
        case input::UiCommand::open_context_menu:
          if (state.hostsFocusArea == HostsFocusArea::grid && selected_host(state) != nullptr) {
            open_modal(state, ModalId::host_actions);
            update.modalOpened = true;
          }
          return update;
        case input::UiCommand::activate:
        case input::UiCommand::confirm:
          if (state.hostsFocusArea == HostsFocusArea::toolbar) {
            if (state.selectedToolbarButtonIndex % HOST_TOOLBAR_BUTTON_COUNT == 0U) {  // NOSONAR(cpp:S134) toolbar actions stay explicit for controller navigation parity
              set_screen(state, ScreenId::settings, settings_category_menu_id(SettingsCategory::logging));
              update.screenChanged = true;
              update.activatedItemId = "settings-button";
              return update;
            }
            if (state.selectedToolbarButtonIndex % HOST_TOOLBAR_BUTTON_COUNT == 1U) {  // NOSONAR(cpp:S134) toolbar actions stay explicit for controller navigation parity
              open_modal(state, ModalId::support);
              update.modalOpened = true;
              update.activatedItemId = "support-button";
              return update;
            }
            enter_add_host_screen(state);
            update.screenChanged = true;
            update.activatedItemId = "add-host-button";
            return update;
          }

          if (const HostRecord *host = selected_host(state); host != nullptr) {
            update.activatedItemId = "select-host";
            if (host->pairingState == PairingState::paired) {  // NOSONAR(cpp:S134) host activation keeps browse-vs-pair branching with its update flags
              update.appsBrowseRequested = true;
              update.appsBrowseShowHidden = false;
            } else {
              if (enter_pair_host_screen(state, host->address, host->port)) {
                update.screenChanged = true;
                update.pairingRequested = true;
                update.pairingAddress = state.pairingDraft.targetAddress;
                update.pairingPort = state.pairingDraft.targetPort;
                update.pairingPin = state.pairingDraft.generatedPin;
              }
            }
          }
          return update;
        case input::UiCommand::back:
        case input::UiCommand::delete_character:
        case input::UiCommand::previous_page:
        case input::UiCommand::next_page:
        case input::UiCommand::fast_previous_page:
        case input::UiCommand::fast_next_page:
        case input::UiCommand::toggle_overlay:
        case input::UiCommand::none:
          return update;
      }
    }

    if (state.activeScreen == ScreenId::apps) {
      switch (command) {
        case input::UiCommand::move_left:
          move_app_grid_selection(state, 0, -1);
          return update;
        case input::UiCommand::move_right:
          move_app_grid_selection(state, 0, 1);
          return update;
        case input::UiCommand::move_up:
          move_app_grid_selection(state, -1, 0);
          return update;
        case input::UiCommand::move_down:
          move_app_grid_selection(state, 1, 0);
          return update;
        case input::UiCommand::open_context_menu:
          if (selected_app(state) != nullptr) {
            open_modal(state, ModalId::app_actions);
            update.modalOpened = true;
          }
          return update;
        case input::UiCommand::activate:
        case input::UiCommand::confirm:
          if (const HostAppRecord *appRecord = selected_app(state); appRecord != nullptr) {
            state.statusMessage = "Launching " + appRecord->name + " is not implemented yet";
            update.activatedItemId = "launch-app";
          }
          return update;
        case input::UiCommand::back:
          state.statusMessage.clear();
          set_screen(state, ScreenId::hosts);
          update.screenChanged = true;
          return update;
        case input::UiCommand::delete_character:
        case input::UiCommand::previous_page:
        case input::UiCommand::next_page:
        case input::UiCommand::fast_previous_page:
        case input::UiCommand::fast_next_page:
        case input::UiCommand::toggle_overlay:
        case input::UiCommand::none:
          return update;
      }
    }

    const ui::MenuUpdate menuUpdate = state.menu.handle_command(command);
    if (menuUpdate.overlayToggleRequested) {
      state.overlayVisible = !state.overlayVisible;
      update.overlayChanged = true;
      update.overlayVisibilityChanged = true;
      return update;
    }

    if (menuUpdate.backRequested) {
      if (state.activeScreen == ScreenId::settings || state.activeScreen == ScreenId::add_host || state.activeScreen == ScreenId::pair_host) {
        if (state.activeScreen == ScreenId::pair_host) {
          update.pairingCancelledRequested = true;
        }
        state.statusMessage = state.activeScreen == ScreenId::apps ? std::string {} : state.statusMessage;
        set_screen(state, ScreenId::hosts);
        update.screenChanged = true;
      }
      return update;
    }

    if (!menuUpdate.activationRequested) {
      return update;
    }

    update.activatedItemId = menuUpdate.activatedItemId;

    if (state.activeScreen == ScreenId::pair_host) {
      if (menuUpdate.activatedItemId == "cancel-pairing") {
        update.pairingCancelledRequested = true;
        set_screen(state, ScreenId::hosts);
        update.screenChanged = true;
      }
      return update;
    }

    if (state.activeScreen != ScreenId::add_host) {
      return update;
    }

    if (menuUpdate.activatedItemId == "edit-address") {
      open_add_host_keypad(state, AddHostField::address);
      return update;
    }
    if (menuUpdate.activatedItemId == "edit-port") {
      open_add_host_keypad(state, AddHostField::port);
      return update;
    }

    std::string normalizedAddress;
    uint16_t parsedPort = 0;
    std::string validationError;
    const bool draftIsValid = normalize_add_host_inputs(state, &normalizedAddress, &parsedPort, &validationError);

    if (menuUpdate.activatedItemId == "test-connection") {
      if (!draftIsValid) {
        state.addHostDraft.validationMessage = validationError;
        state.statusMessage = validationError;
        return update;
      }
      state.addHostDraft.validationMessage.clear();
      state.addHostDraft.connectionMessage = "Testing connection to " + normalizedAddress + (parsedPort == 0 ? std::string {} : ":" + std::to_string(parsedPort)) + "...";
      state.statusMessage = state.addHostDraft.connectionMessage;
      update.connectionTestRequested = true;
      update.connectionTestAddress = normalizedAddress;
      update.connectionTestPort = effective_host_port(parsedPort);
      return update;
    }

    if (menuUpdate.activatedItemId == "save-host") {
      if (!draftIsValid) {
        state.addHostDraft.validationMessage = validationError;
        state.statusMessage = validationError;
        return update;
      }
      if (find_host_by_endpoint(state.hosts, normalizedAddress, parsedPort) != nullptr) {
        state.addHostDraft.validationMessage = "That host is already saved";
        state.statusMessage = state.addHostDraft.validationMessage;
        return update;
      }

      state.hosts.push_back(make_host_record(normalizedAddress, parsedPort));
      state.selectedHostIndex = state.hosts.size() - 1U;
      state.hostsFocusArea = HostsFocusArea::grid;
      state.hostsDirty = true;
      update.hostsChanged = true;
      state.addHostDraft.validationMessage.clear();
      state.addHostDraft.connectionMessage.clear();
      state.statusMessage = "Saved host " + normalizedAddress;
      set_screen(state, ScreenId::hosts);
      update.screenChanged = true;
      return update;
    }

    if (menuUpdate.activatedItemId == "start-pairing") {
      if (!draftIsValid) {
        state.addHostDraft.validationMessage = validationError;
        state.statusMessage = validationError;
        return update;
      }

      const HostRecord *host = find_host_by_endpoint(state.hosts, normalizedAddress, parsedPort);
      if (host == nullptr) {
        state.hosts.push_back(make_host_record(normalizedAddress, parsedPort));
        state.selectedHostIndex = state.hosts.size() - 1U;
        state.hostsDirty = true;
        update.hostsChanged = true;
        host = &state.hosts.back();
      }

      if (enter_pair_host_screen(state, host->address, host->port)) {
        update.screenChanged = true;
        update.pairingRequested = true;
        update.pairingAddress = state.pairingDraft.targetAddress;
        update.pairingPort = state.pairingDraft.targetPort;
        update.pairingPin = state.pairingDraft.generatedPin;
      }
      return update;
    }

    if (menuUpdate.activatedItemId == "cancel-add-host") {
      state.addHostDraft.validationMessage.clear();
      state.addHostDraft.connectionMessage.clear();
      set_screen(state, ScreenId::hosts);
      update.screenChanged = true;
    }

    return update;
  }

}  // namespace app
