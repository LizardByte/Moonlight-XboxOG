// class header include
#include "src/app/client_state.h"

// standard includes
#include <algorithm>
#include <cstddef>
#include <random>
#include <utility>
#include <vector>

namespace {

  constexpr const char *HOST_MENU_ID_PREFIX = "host:";
  constexpr std::size_t OVERLAY_SCROLL_STEP = 4;
  constexpr std::size_t ADD_HOST_KEYPAD_COLUMN_COUNT = 3;

  struct AddHostKeypadButton {
    const char *id;
    char character;
  };

  std::string generate_pairing_pin() {
    static std::mt19937 generator(std::random_device {}());
    std::uniform_int_distribution<int> distribution(0, 9999);

    std::string pin = std::to_string(distribution(generator));
    while (pin.size() < 4U) {
      pin.insert(pin.begin(), '0');
    }

    return pin;
  }

  std::string build_host_menu_id(const std::string &address, uint16_t port) {
    return std::string(HOST_MENU_ID_PREFIX) + address + ":" + std::to_string(app::effective_host_port(port));
  }

  bool parse_host_menu_id(const std::string &itemId, std::string *address, uint16_t *port) {
    if (itemId.rfind(HOST_MENU_ID_PREFIX, 0) != 0) {
      return false;
    }

    const std::string endpoint = itemId.substr(std::char_traits<char>::length(HOST_MENU_ID_PREFIX));
    const std::size_t separatorIndex = endpoint.find_last_of(':');
    if (separatorIndex == std::string::npos) {
      return false;
    }

    uint16_t parsedPort = 0;
    if (!app::try_parse_host_port(endpoint.substr(separatorIndex + 1), &parsedPort)) {
      return false;
    }

    if (address != nullptr) {
      *address = endpoint.substr(0, separatorIndex);
    }
    if (port != nullptr) {
      *port = parsedPort;
    }
    return true;
  }

  std::string build_endpoint_label(const std::string &address, uint16_t port) {
    if (app::effective_host_port(port) == app::DEFAULT_HOST_PORT) {
      return address;
    }

    return address + ":" + std::to_string(app::effective_host_port(port));
  }

  std::string default_add_host_address() {
    return "192.168.0.10";
  }

  const app::HostRecord *find_host_by_endpoint(const std::vector<app::HostRecord> &hosts, const std::string &address, uint16_t port) {
    const uint16_t effectivePort = app::effective_host_port(port);
    const auto iterator = std::find_if(hosts.begin(), hosts.end(), [&address, effectivePort](const app::HostRecord &host) {
      return host.address == address && app::effective_host_port(host.port) == effectivePort;
    });
    if (iterator == hosts.end()) {
      return nullptr;
    }

    return &(*iterator);
  }

  app::HostRecord *find_host_by_endpoint(std::vector<app::HostRecord> &hosts, const std::string &address, uint16_t port) {
    const uint16_t effectivePort = app::effective_host_port(port);
    const auto iterator = std::find_if(hosts.begin(), hosts.end(), [&address, effectivePort](const app::HostRecord &host) {
      return host.address == address && app::effective_host_port(host.port) == effectivePort;
    });
    if (iterator == hosts.end()) {
      return nullptr;
    }

    return &(*iterator);
  }

  const app::HostRecord *selected_host_for_menu(const app::ClientState &state) {
    if (const ui::MenuItem *selectedItem = state.menu.selected_item(); selectedItem != nullptr) {
      std::string address;
      uint16_t port = 0;
      if (parse_host_menu_id(selectedItem->id, &address, &port)) {
        return find_host_by_endpoint(state.hosts, address, port);
      }
    }

    if (!state.hosts.empty()) {
      return &state.hosts.front();
    }

    return nullptr;
  }

  std::vector<AddHostKeypadButton> build_add_host_keypad_buttons(const app::ClientState &state) {
    if (state.addHostDraft.activeField == app::AddHostField::address) {
      return {
        {"keypad-1", '1'},
        {"keypad-2", '2'},
        {"keypad-3", '3'},
        {"keypad-4", '4'},
        {"keypad-5", '5'},
        {"keypad-6", '6'},
        {"keypad-7", '7'},
        {"keypad-8", '8'},
        {"keypad-9", '9'},
        {"keypad-dot", '.'},
        {"keypad-0", '0'},
      };
    }

    return {
      {"keypad-1", '1'},
      {"keypad-2", '2'},
      {"keypad-3", '3'},
      {"keypad-4", '4'},
      {"keypad-5", '5'},
      {"keypad-6", '6'},
      {"keypad-7", '7'},
      {"keypad-8", '8'},
      {"keypad-9", '9'},
      {"keypad-0", '0'},
    };
  }

  std::string add_host_field_menu_id(app::AddHostField field) {
    return field == app::AddHostField::address ? "edit-address" : "edit-port";
  }

  void reset_add_host_draft(app::ClientState &state, app::ScreenId returnScreen) {
    state.addHostDraft = {
      default_add_host_address(),
      {},
      app::AddHostField::address,
      {false, 0U, {}},
      returnScreen,
      {},
      {},
      false,
    };
  }

  std::vector<ui::MenuItem> build_menu_for_state(const app::ClientState &state) {
    switch (state.activeScreen) {
      case app::ScreenId::home:
        return {
          {"hosts", "Hosts", true},
          {"add-host", "Add Host", true},
          {"settings", "Settings", true},
          {"exit", "Exit", true},
        };
      case app::ScreenId::hosts:
        {
          const app::HostRecord *selectedHost = selected_host_for_menu(state);
          std::vector<ui::MenuItem> items;
          items.reserve(state.hosts.size() + 6);

          for (const app::HostRecord &host : state.hosts) {
            items.push_back({
              build_host_menu_id(host.address, host.port),
              host.displayName + (host.pairingState == app::PairingState::paired ? " [paired]" : " [not paired]"),
              true,
            });
          }

          items.push_back({"add-host", "Add Host", true});
          items.push_back({"test-connection", "Test Selected Host", selectedHost != nullptr});
          items.push_back({"pair-host", "Pair Selected Host", selectedHost != nullptr});
          items.push_back({"delete-host", "Delete Selected Host", selectedHost != nullptr});
          items.push_back({"discover-hosts", "Discover Hosts (soon)", false});
          items.push_back({"back-home", "Back", true});
          return items;
        }
      case app::ScreenId::add_host:
        return {
          {"edit-address", "Host Address: " + state.addHostDraft.addressInput, true},
          {"edit-port", std::string("Port: ") + (state.addHostDraft.portInput.empty() ? "default (47984)" : state.addHostDraft.portInput), true},
          {"clear-field", "Clear Current Field", state.addHostDraft.activeField == app::AddHostField::address
            ? !state.addHostDraft.addressInput.empty()
            : !state.addHostDraft.portInput.empty()},
          {"use-default-port", "Use Default Port", state.addHostDraft.activeField == app::AddHostField::port && !state.addHostDraft.portInput.empty()},
          {"test-connection", "Test Connection", true},
          {"start-pairing", "Start Pairing", true},
          {"save-host", "Save Host", true},
          {"cancel-add-host", "Cancel", true},
        };
      case app::ScreenId::pair_host:
        return {
          {"cancel-pairing", "Cancel", true},
        };
      case app::ScreenId::settings:
        return {
          {"display-settings", "Display", true},
          {"input-settings", "Input", true},
          {"logging-settings", "Logging", true},
          {"back-home", "Back", true},
        };
    }

    return {};
  }

  void rebuild_menu(app::ClientState &state, const std::string &preferredItemId = {}, bool preserveCurrentSelection = true) {
    const std::string currentSelection = preserveCurrentSelection && state.menu.selected_item() != nullptr
      ? state.menu.selected_item()->id
      : std::string {};

    state.menu.set_items(build_menu_for_state(state));

    if (!preferredItemId.empty()) {
      state.menu.select_item_by_id(preferredItemId);
      if (state.menu.selected_item() != nullptr && state.menu.selected_item()->id == preferredItemId) {
        return;
      }
    }

    if (!currentSelection.empty()) {
      state.menu.select_item_by_id(currentSelection);
    }
  }

  void set_screen(app::ClientState &state, app::ScreenId screen, const std::string &preferredItemId = {}) {
    state.activeScreen = screen;
    rebuild_menu(state, preferredItemId, false);
  }

  void open_add_host_keypad(app::ClientState &state, app::AddHostField field) {
    state.addHostDraft.activeField = field;
    state.addHostDraft.keypad.visible = true;
    state.addHostDraft.keypad.selectedButtonIndex = 0;
    state.addHostDraft.keypad.stagedInput = field == app::AddHostField::address
      ? state.addHostDraft.addressInput
      : state.addHostDraft.portInput;
    state.statusMessage = field == app::AddHostField::address
      ? "Editing host address"
      : "Editing host port";
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
    }
    else {
      state.addHostDraft.portInput = state.addHostDraft.keypad.stagedInput;
      state.statusMessage = state.addHostDraft.portInput.empty()
        ? "Using default Moonlight host port 47984"
        : "Updated host port";
    }

    state.addHostDraft.validationMessage.clear();
    state.addHostDraft.connectionMessage.clear();
    close_add_host_keypad(state);
  }

  void cancel_add_host_keypad(app::ClientState &state) {
    state.statusMessage = state.addHostDraft.activeField == app::AddHostField::address
      ? "Cancelled host address edit"
      : "Cancelled host port edit";
    close_add_host_keypad(state);
  }

  bool move_add_host_keypad_selection(app::ClientState &state, int rowDelta, int columnDelta) {
    const std::vector<AddHostKeypadButton> buttons = build_add_host_keypad_buttons(state);
    if (buttons.empty()) {
      return false;
    }

    const int rowCount = static_cast<int>((buttons.size() + ADD_HOST_KEYPAD_COLUMN_COUNT - 1) / ADD_HOST_KEYPAD_COLUMN_COUNT);
    const std::size_t currentIndex = state.addHostDraft.keypad.selectedButtonIndex % buttons.size();
    const int currentRow = static_cast<int>(currentIndex / ADD_HOST_KEYPAD_COLUMN_COUNT);
    const int currentColumn = static_cast<int>(currentIndex % ADD_HOST_KEYPAD_COLUMN_COUNT);
    const int nextRow = (currentRow + rowCount + rowDelta) % rowCount;
    int nextColumn = (currentColumn + static_cast<int>(ADD_HOST_KEYPAD_COLUMN_COUNT) + columnDelta) % static_cast<int>(ADD_HOST_KEYPAD_COLUMN_COUNT);
    int nextIndex = (nextRow * static_cast<int>(ADD_HOST_KEYPAD_COLUMN_COUNT)) + nextColumn;
    while (nextIndex >= static_cast<int>(buttons.size()) && nextColumn > 0) {
      --nextColumn;
      nextIndex = (nextRow * static_cast<int>(ADD_HOST_KEYPAD_COLUMN_COUNT)) + nextColumn;
    }

    state.addHostDraft.keypad.selectedButtonIndex = nextIndex >= 0 && nextIndex < static_cast<int>(buttons.size())
      ? static_cast<std::size_t>(nextIndex)
      : currentIndex;
    return state.addHostDraft.keypad.selectedButtonIndex != currentIndex;
  }

  std::string *active_add_host_input_buffer(app::ClientState &state) {
    if (state.addHostDraft.keypad.visible) {
      return &state.addHostDraft.keypad.stagedInput;
    }

    return state.addHostDraft.activeField == app::AddHostField::address
      ? &state.addHostDraft.addressInput
      : &state.addHostDraft.portInput;
  }

  bool append_to_active_add_host_field(app::ClientState &state, char character) {
    std::string *target = active_add_host_input_buffer(state);

    if (state.addHostDraft.activeField == app::AddHostField::address) {
      if ((character < '0' || character > '9') && character != '.') {
        return false;
      }
      if (target->size() >= 15) {
        return false;
      }
    }
    else {
      if (character < '0' || character > '9') {
        return false;
      }
      if (target->size() >= 5) {
        return false;
      }
    }

    target->push_back(character);
    state.addHostDraft.validationMessage.clear();
    state.addHostDraft.connectionMessage.clear();
    state.statusMessage = state.addHostDraft.activeField == app::AddHostField::address
      ? "Editing host address"
      : "Editing host port";
    return true;
  }

  bool backspace_active_add_host_field(app::ClientState &state) {
    std::string *target = active_add_host_input_buffer(state);
    if (target->empty()) {
      return false;
    }

    target->pop_back();
    state.addHostDraft.validationMessage.clear();
    state.addHostDraft.connectionMessage.clear();
    return true;
  }

  void clear_active_add_host_field(app::ClientState &state) {
    if (state.addHostDraft.activeField == app::AddHostField::address) {
      state.addHostDraft.addressInput.clear();
    }
    else {
      state.addHostDraft.portInput.clear();
    }
    state.addHostDraft.validationMessage.clear();
    state.addHostDraft.connectionMessage.clear();
  }

  bool normalize_add_host_inputs(const app::ClientState &state, std::string *normalizedAddress, uint16_t *parsedPort, std::string *errorMessage) {
    const std::string address = app::normalize_ipv4_address(state.addHostDraft.addressInput);
    if (address.empty()) {
      if (errorMessage != nullptr) {
        *errorMessage = "Enter a valid IPv4 address";
      }
      return false;
    }

    uint16_t port = 0;
    if (!app::try_parse_host_port(state.addHostDraft.portInput, &port)) {
      if (errorMessage != nullptr) {
        *errorMessage = "Enter a valid TCP port or leave it empty for the default";
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

  void enter_add_host_screen(app::ClientState &state) {
    reset_add_host_draft(state, state.activeScreen);
    set_screen(state, app::ScreenId::add_host, "edit-address");
  }

  void enter_pair_host_screen(app::ClientState &state, const std::string &targetAddress, uint16_t targetPort) {
    state.pairingDraft = app::create_pairing_draft(targetAddress, app::effective_host_port(targetPort), generate_pairing_pin());
    state.pairingDraft.stage = app::PairingStage::in_progress;
    set_screen(state, app::ScreenId::pair_host, "cancel-pairing");
  }

}  // namespace

namespace app {

  ClientState create_initial_state() {
    ClientState state {
      ScreenId::home,
      false,
      false,
      false,
      0U,
      ui::MenuModel(),
      {},
      {default_add_host_address(), {}, AddHostField::address, {false, 0U, {}}, ScreenId::home, {}, {}, false},
      {{}, DEFAULT_HOST_PORT, {}, PairingStage::idle, {}},
      {},
    };
    rebuild_menu(state);
    return state;
  }

  const char *to_string(ScreenId screen) {
    switch (screen) {
      case ScreenId::home:
        return "home";
      case ScreenId::hosts:
        return "hosts";
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

    if (state.activeScreen == ScreenId::hosts || state.activeScreen == ScreenId::home) {
      rebuild_menu(state);
    }
    else if (state.activeScreen == ScreenId::pair_host) {
      if (find_host_by_endpoint(state.hosts, state.pairingDraft.targetAddress, state.pairingDraft.targetPort) == nullptr) {
        set_screen(state, ScreenId::hosts);
      }
      else {
        rebuild_menu(state);
      }
    }
  }

  std::string current_add_host_address(const ClientState &state) {
    return state.addHostDraft.addressInput;
  }

  uint16_t current_add_host_port(const ClientState &state) {
    uint16_t port = 0;
    if (!try_parse_host_port(state.addHostDraft.portInput, &port)) {
      return DEFAULT_HOST_PORT;
    }

    return effective_host_port(port);
  }

  std::string current_pairing_pin(const ClientState &state) {
    return state.pairingDraft.generatedPin;
  }

  void apply_connection_test_result(ClientState &state, bool success, std::string message) {
    if (state.activeScreen == ScreenId::add_host) {
      state.addHostDraft.connectionMessage = message;
      state.addHostDraft.lastConnectionSucceeded = success;
    }
    state.statusMessage = std::move(message);
  }

  bool apply_pairing_result(ClientState &state, const std::string &address, uint16_t port, bool success, std::string message) {
    state.pairingDraft.targetAddress = address;
    state.pairingDraft.targetPort = effective_host_port(port);
    state.pairingDraft.stage = success ? PairingStage::paired : PairingStage::failed;
    state.pairingDraft.statusMessage = message;
    state.statusMessage = std::move(message);

    if (!success) {
      return false;
    }

    HostRecord *host = find_host_by_endpoint(state.hosts, address, port);
    if (host == nullptr || host->pairingState == PairingState::paired) {
      return false;
    }

    host->pairingState = PairingState::paired;
    state.hostsDirty = true;
    return true;
  }

  const HostRecord *selected_host(const ClientState &state) {
    if (const ui::MenuItem *selectedItem = state.menu.selected_item(); selectedItem != nullptr) {
      std::string address;
      uint16_t port = 0;
      if (parse_host_menu_id(selectedItem->id, &address, &port)) {
        return find_host_by_endpoint(state.hosts, address, port);
      }
    }

    return nullptr;
  }

  AppUpdate handle_command(ClientState &state, input::UiCommand command) {
    AppUpdate update {};
    update.connectionTestPort = 0;
    update.pairingPort = 0;

    if (command == input::UiCommand::toggle_overlay) {
      state.overlayVisible = !state.overlayVisible;
      if (!state.overlayVisible) {
        state.overlayScrollOffset = 0;
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
        state.overlayScrollOffset = state.overlayScrollOffset > OVERLAY_SCROLL_STEP
          ? state.overlayScrollOffset - OVERLAY_SCROLL_STEP
          : 0;
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
        case input::UiCommand::activate:
          {
            const std::vector<AddHostKeypadButton> buttons = build_add_host_keypad_buttons(state);
            if (buttons.empty()) {
              return update;
            }

            const AddHostKeypadButton &button = buttons[state.addHostDraft.keypad.selectedButtonIndex % buttons.size()];
            append_to_active_add_host_field(state, button.character);

            rebuild_menu(state, add_host_field_menu_id(state.addHostDraft.activeField));
            return update;
          }
        case input::UiCommand::confirm:
          accept_add_host_keypad(state);
          return update;
        case input::UiCommand::delete_character:
          backspace_active_add_host_field(state);
          rebuild_menu(state, add_host_field_menu_id(state.addHostDraft.activeField));
          return update;
        case input::UiCommand::none:
        case input::UiCommand::previous_page:
        case input::UiCommand::next_page:
        case input::UiCommand::toggle_overlay:
          break;
      }
    }

    const ui::MenuUpdate menuUpdate = state.menu.handle_command(command);
    if (state.activeScreen == ScreenId::hosts && menuUpdate.selectionChanged) {
      rebuild_menu(state);
    }

    if (menuUpdate.overlayToggleRequested) {
      state.overlayVisible = !state.overlayVisible;
      if (!state.overlayVisible) {
        state.overlayScrollOffset = 0;
      }
      update.overlayChanged = true;
      update.overlayVisibilityChanged = true;
    }

    if (menuUpdate.backRequested && state.activeScreen != ScreenId::home) {
      if (state.activeScreen == ScreenId::add_host) {
        state.addHostDraft.validationMessage.clear();
        state.addHostDraft.connectionMessage.clear();
        set_screen(state, state.addHostDraft.returnScreen == ScreenId::add_host ? ScreenId::home : state.addHostDraft.returnScreen, state.addHostDraft.returnScreen == ScreenId::hosts ? "add-host" : std::string {});
      }
      else if (state.activeScreen == ScreenId::pair_host) {
        set_screen(state, ScreenId::hosts, build_host_menu_id(state.pairingDraft.targetAddress, state.pairingDraft.targetPort));
      }
      else {
        set_screen(state, ScreenId::home);
      }
      update.screenChanged = true;
      return update;
    }

    if (!menuUpdate.activationRequested) {
      return update;
    }

    update.activatedItemId = menuUpdate.activatedItemId;

    if (menuUpdate.activatedItemId == "hosts") {
      set_screen(state, ScreenId::hosts);
      update.screenChanged = true;
      return update;
    }

    if (menuUpdate.activatedItemId == "add-host") {
      enter_add_host_screen(state);
      update.screenChanged = true;
      return update;
    }

    if (menuUpdate.activatedItemId == "settings") {
      set_screen(state, ScreenId::settings);
      update.screenChanged = true;
      return update;
    }

    if (menuUpdate.activatedItemId == "back-home") {
      set_screen(state, ScreenId::home);
      update.screenChanged = true;
      return update;
    }

    if (state.activeScreen == ScreenId::add_host) {
      if (menuUpdate.activatedItemId == "edit-address") {
        open_add_host_keypad(state, AddHostField::address);
        return update;
      }

      if (menuUpdate.activatedItemId == "edit-port") {
        open_add_host_keypad(state, AddHostField::port);
        return update;
      }

      if (menuUpdate.activatedItemId == "clear-field") {
        clear_active_add_host_field(state);
        rebuild_menu(state, add_host_field_menu_id(state.addHostDraft.activeField));
        return update;
      }

      if (menuUpdate.activatedItemId == "use-default-port") {
        state.addHostDraft.portInput.clear();
        state.statusMessage = "Using default Moonlight host port 47984";
        rebuild_menu(state, "edit-port");
        return update;
      }

      std::string normalizedAddress;
      uint16_t parsedPort = 0;
      std::string errorMessage;
      const bool draftIsValid = normalize_add_host_inputs(state, &normalizedAddress, &parsedPort, &errorMessage);

      if (menuUpdate.activatedItemId == "test-connection") {
        if (!draftIsValid) {
          state.addHostDraft.validationMessage = errorMessage;
          state.statusMessage = errorMessage;
          rebuild_menu(state, "test-connection");
          return update;
        }

        state.addHostDraft.validationMessage.clear();
        state.addHostDraft.connectionMessage = "Testing connection to " + build_endpoint_label(normalizedAddress, parsedPort) + "...";
        state.statusMessage = state.addHostDraft.connectionMessage;
        update.connectionTestRequested = true;
        update.connectionTestAddress = normalizedAddress;
        update.connectionTestPort = effective_host_port(parsedPort);
        return update;
      }

      if (menuUpdate.activatedItemId == "save-host") {
        if (!draftIsValid) {
          state.addHostDraft.validationMessage = errorMessage;
          state.statusMessage = errorMessage;
          rebuild_menu(state, menuUpdate.activatedItemId);
          return update;
        }

        HostRecord *existingHost = find_host_by_endpoint(state.hosts, normalizedAddress, parsedPort);
        if (existingHost == nullptr) {
          state.hosts.push_back({
            build_default_host_display_name(normalizedAddress),
            normalizedAddress,
            parsedPort,
            PairingState::not_paired,
          });
          state.hostsDirty = true;
          update.hostsChanged = true;
          existingHost = &state.hosts.back();
        }
        else if (menuUpdate.activatedItemId == "save-host") {
          state.addHostDraft.validationMessage = "That host is already saved";
          state.statusMessage = state.addHostDraft.validationMessage;
          rebuild_menu(state, menuUpdate.activatedItemId);
          return update;
        }

        state.addHostDraft.validationMessage.clear();
        state.addHostDraft.connectionMessage.clear();

        if (menuUpdate.activatedItemId == "save-host") {
          state.statusMessage = "Saved host " + build_endpoint_label(existingHost->address, existingHost->port);
          set_screen(state, ScreenId::hosts, build_host_menu_id(existingHost->address, existingHost->port));
          update.screenChanged = true;
          update.activatedItemId = build_host_menu_id(existingHost->address, existingHost->port);
          return update;
        }

      }

      if (menuUpdate.activatedItemId == "cancel-add-host") {
        state.addHostDraft.validationMessage.clear();
        state.addHostDraft.connectionMessage.clear();
        set_screen(state, state.addHostDraft.returnScreen == ScreenId::add_host ? ScreenId::home : state.addHostDraft.returnScreen, state.addHostDraft.returnScreen == ScreenId::hosts ? "add-host" : std::string {});
        update.screenChanged = true;
        return update;
      }
    }

    if (menuUpdate.activatedItemId == "test-connection") {
      if (state.activeScreen == ScreenId::hosts) {
        if (const HostRecord *host = selected_host_for_menu(state); host != nullptr) {
          state.statusMessage = "Testing connection to " + build_endpoint_label(host->address, host->port) + "...";
          update.connectionTestAddress = host->address;
          update.connectionTestPort = effective_host_port(host->port);
          update.connectionTestRequested = true;
        }
      }
      return update;
    }

    if (menuUpdate.activatedItemId == "pair-host") {
      if (const HostRecord *host = selected_host_for_menu(state); host != nullptr) {
        enter_pair_host_screen(state, host->address, host->port);
        update.screenChanged = true;
        update.pairingRequested = true;
        update.pairingAddress = state.pairingDraft.targetAddress;
        update.pairingPort = state.pairingDraft.targetPort;
        update.pairingPin = state.pairingDraft.generatedPin;
      }
      return update;
    }

    if (menuUpdate.activatedItemId == "start-pairing") {
      std::string normalizedAddress;
      uint16_t parsedPort = 0;
      std::string errorMessage;
      if (!normalize_add_host_inputs(state, &normalizedAddress, &parsedPort, &errorMessage)) {
        state.addHostDraft.validationMessage = errorMessage;
        state.statusMessage = errorMessage;
        rebuild_menu(state, menuUpdate.activatedItemId);
        return update;
      }

      state.addHostDraft.validationMessage.clear();
      state.addHostDraft.connectionMessage.clear();

      HostRecord *host = find_host_by_endpoint(state.hosts, normalizedAddress, parsedPort);
      if (host == nullptr) {
        state.hosts.push_back({
          build_default_host_display_name(normalizedAddress),
          normalizedAddress,
          parsedPort,
          PairingState::not_paired,
        });
        state.hostsDirty = true;
        update.hostsChanged = true;
        host = &state.hosts.back();
      }

      enter_pair_host_screen(state, host->address, host->port);
      update.screenChanged = true;
      update.pairingRequested = true;
      update.pairingAddress = state.pairingDraft.targetAddress;
      update.pairingPort = state.pairingDraft.targetPort;
      update.pairingPin = state.pairingDraft.generatedPin;
      return update;
    }

    if (state.activeScreen == ScreenId::pair_host) {

      if (menuUpdate.activatedItemId == "cancel-pairing") {
        set_screen(state, ScreenId::hosts, build_host_menu_id(state.pairingDraft.targetAddress, state.pairingDraft.targetPort));
        update.screenChanged = true;
        return update;
      }
    }

    if (menuUpdate.activatedItemId == "delete-host") {
      if (const HostRecord *host = selected_host_for_menu(state); host != nullptr) {
        const std::string deletedAddress = host->address;
        const uint16_t deletedPort = host->port;
        const std::string deletedName = host->displayName;
        const auto iterator = std::find_if(state.hosts.begin(), state.hosts.end(), [&deletedAddress, deletedPort](const HostRecord &candidate) {
          return candidate.address == deletedAddress && effective_host_port(candidate.port) == effective_host_port(deletedPort);
        });
        const auto removedIndex = static_cast<std::size_t>(std::distance(state.hosts.begin(), iterator));

        if (iterator != state.hosts.end()) {
          state.hosts.erase(iterator);
          state.hostsDirty = true;
          state.statusMessage = "Deleted host " + deletedName;

          std::string preferredItemId = "add-host";
          if (!state.hosts.empty()) {
            const std::size_t preferredIndex = std::min(removedIndex, state.hosts.size() - 1);
            preferredItemId = build_host_menu_id(state.hosts[preferredIndex].address, state.hosts[preferredIndex].port);
          }

          set_screen(state, ScreenId::hosts, preferredItemId);
          update.hostsChanged = true;
        }
      }
      return update;
    }

    if (const HostRecord *host = selected_host(state); host != nullptr) {
      state.statusMessage = "Selected " + host->displayName + " at " + build_endpoint_label(host->address, host->port) + ".";
      rebuild_menu(state, build_host_menu_id(host->address, host->port));
      return update;
    }

    if (menuUpdate.activatedItemId == "exit") {
      state.shouldExit = true;
      update.exitRequested = true;
    }

    return update;
  }

}  // namespace app
