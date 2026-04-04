#pragma once

// standard includes
#include <cstddef>
#include <string>
#include <vector>

// standard includes
#include "src/app/host_records.h"
#include "src/app/pairing_flow.h"
#include "src/input/navigation_input.h"
#include "src/ui/menu_model.h"

namespace app {

  /**
   * @brief Top-level screens used by the Moonlight client shell.
   */
  enum class ScreenId {
    home,
    hosts,
    add_host,
    pair_host,
    settings,
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
   * @brief Serializable app state for the menu-driven client shell.
   */
  struct ClientState {
    ScreenId activeScreen;
    bool overlayVisible;
    bool shouldExit;
    bool hostsDirty;
    std::size_t overlayScrollOffset;
    ui::MenuModel menu;
    std::vector<HostRecord> hosts;
    AddHostDraft addHostDraft;
    PairingDraft pairingDraft;
    std::string statusMessage;
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
    std::string activatedItemId;
    std::string connectionTestAddress;
    uint16_t connectionTestPort;
    std::string pairingAddress;
    uint16_t pairingPort;
    std::string pairingPin;
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
   * @brief Return the currently selected saved host on the Hosts screen.
   *
   * @param state App state containing the hosts list and menu selection.
   * @return Selected host record, or nullptr when no saved host is selected.
   */
  const HostRecord *selected_host(const ClientState &state);

  /**
   * @brief Apply a UI command to the client shell.
   *
   * @param state Mutable app state.
   * @param command UI command from controller or keyboard input.
   * @return Summary of the resulting state transition.
   */
  AppUpdate handle_command(ClientState &state, input::UiCommand command);

}  // namespace app
