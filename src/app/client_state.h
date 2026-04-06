#pragma once

// standard includes
#include <cstddef>
#include <string>
#include <vector>

// standard includes
#include "src/app/host_records.h"
#include "src/app/pairing_flow.h"
#include "src/input/navigation_input.h"
#include "src/logging/logger.h"
#include "src/startup/saved_files.h"
#include "src/ui/menu_model.h"

namespace app {

  /**
   * @brief Top-level screens used by the Moonlight client shell.
   */
  enum class ScreenId {
    home,
    hosts,
    apps,
    add_host,
    pair_host,
    settings,
  };

  /**
   * @brief Focus areas on the hosts page.
   */
  enum class HostsFocusArea {
    toolbar,
    grid,
  };

  /**
   * @brief Active modal surfaced on top of the current page.
   */
  enum class ModalId {
    none,
    support,
    host_actions,
    host_details,
    app_actions,
    app_details,
    confirmation,
    log_viewer,
  };

  enum class LogViewerPlacement {
    full,
    left,
    right,
  };

  /**
   * @brief Focus areas used by the two-pane settings screen.
   */
  enum class SettingsFocusArea {
    categories,
    options,
  };

  /**
   * @brief Top-level categories shown on the left side of the settings screen.
   */
  enum class SettingsCategory {
    logging,
    display,
    input,
    reset,
  };

  /**
   * @brief Destructive confirmation requests surfaced in a modal popup.
   */
  enum class ConfirmationAction {
    none,
    delete_saved_file,
    factory_reset,
  };

  /**
   * @brief Active field for keypad-based host entry.
   */
  enum class AddHostField {
    address,
    port,
  };

  /**
   * @brief Controller selection state for the add-host keypad modal.
   */
  struct AddHostKeypadState {
    bool visible;
    std::size_t selectedButtonIndex;
    std::string stagedInput;
  };

  /**
   * @brief Controller-friendly draft state for manual host entry.
   */
  struct AddHostDraft {
    std::string addressInput;
    std::string portInput;
    AddHostField activeField;
    AddHostKeypadState keypad;
    ScreenId returnScreen;
    std::string validationMessage;
    std::string connectionMessage;
    bool lastConnectionSucceeded;
  };

  /**
   * @brief Context modal state shared by the hosts and apps pages.
   */
  struct ModalState {
    ModalId id = ModalId::none;
    std::size_t selectedActionIndex = 0;

    bool active() const {
      return id != ModalId::none;
    }
  };

  struct ConfirmationDialogState {
    ConfirmationAction action = ConfirmationAction::none;
    std::string targetPath;
    std::string title;
    std::vector<std::string> lines;
  };

  /**
   * @brief Serializable app state for the menu-driven client shell.
   */
  struct ClientState {
    ScreenId activeScreen;
    bool overlayVisible;
    bool shouldExit;
    bool hostsDirty;
    std::size_t overlayScrollOffset;
    HostsFocusArea hostsFocusArea;
    std::size_t selectedToolbarButtonIndex;
    std::size_t selectedHostIndex;
    std::size_t selectedAppIndex;
    std::size_t appsScrollPage;
    bool showHiddenApps;
    ui::MenuModel menu;
    ui::MenuModel detailMenu;
    std::vector<HostRecord> hosts;
    AddHostDraft addHostDraft;
    PairingDraft pairingDraft;
    ModalState modal;
    SettingsFocusArea settingsFocusArea = SettingsFocusArea::categories;
    SettingsCategory selectedSettingsCategory = SettingsCategory::logging;
    ConfirmationDialogState confirmation;
    std::string statusMessage;
    std::string logFilePath;
    std::vector<std::string> logViewerLines;
    std::size_t logViewerScrollOffset = 0U;
    LogViewerPlacement logViewerPlacement = LogViewerPlacement::full;
    logging::LogLevel loggingLevel = logging::LogLevel::info;
    std::vector<startup::SavedFileEntry> savedFiles;
    bool savedFilesDirty = true;
  };

  /**
   * @brief Result of updating the client shell with a UI command.
   */
  struct AppUpdate {
    bool screenChanged;
    bool overlayChanged;
    bool overlayVisibilityChanged;
    bool exitRequested;
    bool hostsChanged;
    bool connectionTestRequested;
    bool pairingRequested;
    bool pairingCancelledRequested;
    bool appsBrowseRequested;
    bool appsBrowseShowHidden;
    bool logViewRequested;
    bool savedFileDeleteRequested;
    bool factoryResetRequested;
    bool modalOpened;
    bool modalClosed;
    std::string activatedItemId;
    std::string connectionTestAddress;
    uint16_t connectionTestPort;
    std::string pairingAddress;
    uint16_t pairingPort;
    std::string pairingPin;
    std::string savedFileDeletePath;
  };

  /**
   * @brief Create the initial app state shown after startup.
   *
   * @return The initial client state.
   */
  ClientState create_initial_state();

  /**
   * @brief Return a display label for a screen identifier.
   *
   * @param screen Screen identifier to stringify.
   * @return Stable lowercase screen name.
   */
  const char *to_string(ScreenId screen);

  /**
   * @brief Replace the in-memory host list from a persisted snapshot.
   *
   * @param state Mutable app state.
   * @param hosts Loaded host records.
   * @param statusMessage Optional status line shown in the shell.
   */
  void replace_hosts(ClientState &state, std::vector<HostRecord> hosts, std::string statusMessage = {});

  /**
   * @brief Replace the in-memory saved-file inventory shown on the settings page.
   *
   * @param state Mutable app state.
   * @param savedFiles Saved files currently found on disk.
   */
  void replace_saved_files(ClientState &state, std::vector<startup::SavedFileEntry> savedFiles);

  /**
   * @brief Return the current host address shown in the add-host flow.
   *
   * @param state App state containing the add-host draft.
   * @return Current draft IPv4 address text.
   */
  std::string current_add_host_address(const ClientState &state);

  /**
   * @brief Return the effective TCP port for the current add-host draft.
   *
   * @param state App state containing the add-host draft.
   * @return Effective host port using the default when the field is empty.
   */
  uint16_t current_add_host_port(const ClientState &state);

  /**
   * @brief Return the current pairing PIN shown in the pairing flow.
   *
   * @param state App state containing the pairing draft.
   * @return Four-digit PIN string.
   */
  std::string current_pairing_pin(const ClientState &state);

  /**
   * @brief Apply the result of a host connection test to the current shell state.
   *
   * @param state Mutable app state.
   * @param success Whether the test succeeded.
   * @param message User-visible status message.
   */
  void apply_connection_test_result(ClientState &state, bool success, std::string message);

  /**
   * @brief Apply the result of a pairing attempt to the current shell state.
   *
   * @param state Mutable app state.
   * @param address Host address used for pairing.
   * @param port Host port used for pairing.
   * @param success Whether the pairing attempt succeeded.
   * @param message User-visible status message.
   * @return True when the host list changed and should be persisted.
   */
  bool apply_pairing_result(ClientState &state, const std::string &address, uint16_t port, bool success, std::string message);

  /**
   * @brief Apply a fetched Sunshine app list to a saved host.
   *
   * @param state Mutable app state.
   * @param address Host address used for the fetch.
   * @param port Host port used for the fetch.
   * @param apps Fresh app records returned by the host.
   * @param success Whether the fetch succeeded.
   * @param message User-visible status message.
   */
  void apply_app_list_result(
    ClientState &state,
    const std::string &address,
    uint16_t port,
    std::vector<HostAppRecord> apps,
    uint64_t appListContentHash,
    bool success,
    std::string message
  );

  /**
   * @brief Mark one cached cover-art entry as available for a host app.
   *
   * @param state Mutable app state.
   * @param address Host address owning the app.
   * @param port Host port owning the app.
   * @param appId App identifier whose cached art is now available.
   */
  void mark_cover_art_cached(ClientState &state, const std::string &address, uint16_t port, int appId);

  void set_log_file_path(ClientState &state, std::string logFilePath);

  void apply_log_viewer_contents(ClientState &state, std::vector<std::string> lines, std::string statusMessage);

  /**
   * @brief Enter the apps screen for the currently selected host after authorization has been refreshed.
   *
   * @param state Mutable app state.
   * @param showHiddenApps Whether hidden apps should still be shown.
   * @return true when the apps page was entered.
   */
  bool begin_selected_host_app_browse(ClientState &state, bool showHiddenApps);

  /**
   * @brief Return the currently selected saved host on the Hosts screen.
   *
   * @param state App state containing the hosts list and menu selection.
   * @return Selected host record, or nullptr when no saved host is selected.
   */
  const HostRecord *selected_host(const ClientState &state);

  /**
   * @brief Return the currently selected app on the Apps screen.
   *
   * @param state App state containing the selected host and apps list.
   * @return Selected app record, or nullptr when no visible app is selected.
   */
  const HostAppRecord *selected_app(const ClientState &state);

  /**
   * @brief Return the host currently shown by the Apps screen.
   *
   * @param state App state containing the selected host.
   * @return Host record backing the apps page, or nullptr when unavailable.
   */
  const HostRecord *apps_host(const ClientState &state);

  /**
   * @brief Apply a UI command to the client shell.
   *
   * @param state Mutable app state.
   * @param command UI command from controller or keyboard input.
   * @return Summary of the resulting state transition.
   */
  AppUpdate handle_command(ClientState &state, input::UiCommand command);

}  // namespace app
