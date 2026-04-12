// class header include
#include "src/ui/shell_view.h"

// standard includes
#include <algorithm>
#include <utility>
#include <vector>

namespace {

  bool starts_with(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0U) == 0U;
  }

  bool screen_supports_notifications(const app::ClientState &state) {
    return state.activeScreen == app::ScreenId::home || state.activeScreen == app::ScreenId::hosts || state.activeScreen == app::ScreenId::apps || state.activeScreen == app::ScreenId::settings;
  }

  std::string format_file_size(std::uint64_t sizeBytes) {
    if (sizeBytes < 1024ULL) {
      return std::to_string(sizeBytes) + " B";
    }
    if (sizeBytes < (1024ULL * 1024ULL)) {
      return std::to_string((sizeBytes + 1023ULL) / 1024ULL) + " KB";
    }
    return std::to_string((sizeBytes + (1024ULL * 1024ULL) - 1ULL) / (1024ULL * 1024ULL)) + " MB";
  }

  bool is_minor_status_message(const app::ClientState &state) {
    if (state.statusMessage.empty()) {
      return true;
    }

    if (starts_with(state.statusMessage, "Loaded recent log file lines") || starts_with(state.statusMessage, "No log file has been written yet") || starts_with(state.statusMessage, "Testing connection to ") || starts_with(state.statusMessage, "Editing host ") || starts_with(state.statusMessage, "Updated host ") || starts_with(state.statusMessage, "Cancelled host ") || starts_with(state.statusMessage, "Using default Moonlight host port") || starts_with(state.statusMessage, "Loading apps for ") || starts_with(state.statusMessage, "Pairing is preparing the client identity")) {
      return true;
    }

    if (state.activeScreen == app::ScreenId::apps) {
      if (const app::HostRecord *host = app::apps_host(state); host != nullptr) {
        return host->appListState == app::HostAppListState::loading && state.statusMessage == host->appListStatusMessage;
      }
    }

    return false;
  }

  std::string page_title(const app::ClientState &state) {
    switch (state.activeScreen) {
      case app::ScreenId::home:
      case app::ScreenId::hosts:
        return {};
      case app::ScreenId::apps:
        if (const app::HostRecord *host = app::apps_host(state); host != nullptr) {
          return host->displayName;
        }
        return "Apps";
      case app::ScreenId::add_host:
        return "Add Host";
      case app::ScreenId::pair_host:
        return "Pair Host";
      case app::ScreenId::settings:
        return "Settings";
    }

    return "Hosts";
  }

  std::string host_tile_status(const app::HostRecord &host) {
    if (host.reachability == app::HostReachability::unknown) {
      return "Checking...";
    }
    if (host.reachability != app::HostReachability::online) {
      return "Offline";
    }
    if (host.pairingState != app::PairingState::paired) {
      return "Needs Pairing";
    }
    return "Online";
  }

  std::string host_tile_icon(const app::HostRecord &host) {
    if (host.reachability == app::HostReachability::online && host.pairingState == app::PairingState::paired) {
      return "icons\\host-monitor-online.svg";
    }
    if (host.reachability == app::HostReachability::online) {
      return "icons\\host-monitor-pairing.svg";
    }
    return "icons\\host-monitor-offline.svg";
  }

  std::vector<ui::ShellToolbarButton> toolbar_buttons(const app::ClientState &state) {
    return {
      {"settings", "Settings", "G", "icons\\gear.svg", state.activeScreen == app::ScreenId::hosts && state.hostsFocusArea == app::HostsFocusArea::toolbar && state.selectedToolbarButtonIndex % 3U == 0U},
      {"support", "Support", "?", "icons\\support.svg", state.activeScreen == app::ScreenId::hosts && state.hostsFocusArea == app::HostsFocusArea::toolbar && state.selectedToolbarButtonIndex % 3U == 1U},
      {"add-host", "Add Host", "+", "icons\\add-host.svg", state.activeScreen == app::ScreenId::hosts && state.hostsFocusArea == app::HostsFocusArea::toolbar && state.selectedToolbarButtonIndex % 3U == 2U},
    };
  }

  std::vector<ui::ShellHostTile> host_tiles(const app::ClientState &state) {
    std::vector<ui::ShellHostTile> tiles;
    tiles.reserve(state.hosts.size());
    for (std::size_t index = 0; index < state.hosts.size(); ++index) {
      const app::HostRecord &host = state.hosts[index];
      tiles.emplace_back(ui::ShellHostTile {
        host.address,
        host.displayName,
        host_tile_status(host),
        host_tile_icon(host),
        host.pairingState,
        host.reachability,
        state.activeScreen == app::ScreenId::hosts && state.hostsFocusArea == app::HostsFocusArea::grid && index == state.selectedHostIndex,
      });
    }
    return tiles;
  }

  std::vector<ui::ShellAppTile> app_tiles(const app::ClientState &state) {
    std::vector<ui::ShellAppTile> tiles;
    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr || host->pairingState != app::PairingState::paired) {
      return tiles;
    }

    for (std::size_t index = 0, visibleIndex = 0; index < host->apps.size(); ++index) {
      const app::HostAppRecord &appRecord = host->apps[index];
      if (!state.showHiddenApps && appRecord.hidden) {
        continue;
      }

      const std::string detail = appRecord.running ? "Running now" : std::string {};

      std::string badgeLabel;
      if (appRecord.favorite) {
        badgeLabel = "Favorite";
      } else if (appRecord.hdrSupported) {
        badgeLabel = "HDR";
      } else if (appRecord.hidden) {
        badgeLabel = "Hidden";
      }

      tiles.emplace_back(ui::ShellAppTile {
        std::to_string(appRecord.id),
        appRecord.name,
        detail,
        badgeLabel,
        appRecord.boxArtCacheKey,
        appRecord.hidden,
        appRecord.favorite,
        appRecord.boxArtCached,
        appRecord.running,
        state.activeScreen == app::ScreenId::apps && visibleIndex == state.selectedAppIndex,
      });
      ++visibleIndex;
    }

    return tiles;
  }

  std::string active_add_host_field_label(const app::ClientState &state) {
    return state.addHostDraft.activeField == app::AddHostField::address ? "Address" : "Port";
  }

  std::string keypad_value(const app::ClientState &state) {
    if (state.addHostDraft.keypad.visible) {
      return state.addHostDraft.keypad.stagedInput.empty() && state.addHostDraft.activeField == app::AddHostField::port ? "default (47989)" : state.addHostDraft.keypad.stagedInput;
    }

    if (state.addHostDraft.activeField == app::AddHostField::address) {
      return state.addHostDraft.addressInput;
    }
    if (state.addHostDraft.portInput.empty()) {
      return "default (47989)";
    }
    return state.addHostDraft.portInput;
  }

  const char *settings_category_label(app::SettingsCategory category) {
    switch (category) {
      case app::SettingsCategory::logging:
        return "Logging";
      case app::SettingsCategory::display:
        return "Display";
      case app::SettingsCategory::input:
        return "Input";
      case app::SettingsCategory::reset:
        return "Reset";
    }

    return "Logging";
  }

  std::vector<ui::ShellModalButton> keypad_buttons(const app::ClientState &state) {
    const std::vector<std::string> labels = state.addHostDraft.activeField == app::AddHostField::address ? std::vector<std::string> {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0"} : std::vector<std::string> {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};

    std::vector<ui::ShellModalButton> buttons;
    buttons.reserve(labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) {
      buttons.emplace_back(ui::ShellModalButton {
        labels[index],
        true,
        state.addHostDraft.keypad.visible && index == state.addHostDraft.keypad.selectedButtonIndex,
      });
    }

    return buttons;
  }

  std::vector<std::string> keypad_modal_lines(const app::ClientState &state) {
    std::vector<std::string> lines = {
      std::string("Editing field: ") + active_add_host_field_label(state),
      std::string("Staged value: ") + keypad_value(state),
      "Use the D-pad or left stick to choose a key. Hold a direction to keep moving.",
    };

    if (state.addHostDraft.activeField == app::AddHostField::address) {
      lines.emplace_back("Enter a dotted IPv4 address such as 192.168.0.10.");
    } else {
      lines.emplace_back("Enter digits for a custom TCP port, or leave it empty to keep the default of 47989.");
    }

    return lines;
  }

  std::vector<std::string> hosts_body_lines(const app::ClientState &state) {
    if (state.hosts.empty()) {
      return {
        "No PCs have been added yet.",
        "Use Add Host to save a host manually.",
        "A Moonlight-style discovery grid now owns the home screen.",
      };
    }

    return {
      "Select a PC to pair or browse its apps.",
      "Press Y on a controller, or I on a keyboard, for host actions.",
    };
  }

  std::vector<std::string> apps_body_lines(const app::ClientState &state) {
    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr) {
      return {"No host is selected."};
    }
    if (host->pairingState != app::PairingState::paired) {
      return {"This host is not paired yet. Return and select it to begin pairing."};
    }
    if (host->appListState == app::HostAppListState::loading) {
      return {};
    }
    if (host->appListState == app::HostAppListState::failed) {
      return {
        host->appListStatusMessage.empty() ? "The app list could not be refreshed." : host->appListStatusMessage,
      };
    }
    if (host->apps.empty()) {
      return {"Host did not return any launchable apps for this host."};
    }

    return {};
  }

  std::vector<std::string> add_host_body_lines(const app::ClientState &state) {
    std::vector<std::string> lines = {
      "Manual host entry with a keypad modal.",
      std::string("Address: ") + app::current_add_host_address(state),
      std::string("Port: ") + (state.addHostDraft.portInput.empty() ? std::string("default (47989)") : state.addHostDraft.portInput),
      "Press A to edit either field with the keypad modal.",
    };

    if (!state.addHostDraft.validationMessage.empty()) {
      lines.emplace_back("Validation: " + state.addHostDraft.validationMessage);
    }
    if (!state.addHostDraft.connectionMessage.empty()) {
      lines.emplace_back("Connection: " + state.addHostDraft.connectionMessage);
    }
    return lines;
  }

  std::vector<std::string> pair_host_body_lines(const app::ClientState &state) {
    std::vector<std::string> lines = {
      std::string("Target host: ") + state.pairingDraft.targetAddress,
    };
    if (state.pairingDraft.stage == app::PairingStage::idle) {
      lines.emplace_back("Checking whether the host is reachable before showing a PIN.");
    } else {
      lines.emplace_back(std::string("Target port: ") + std::to_string(state.pairingDraft.targetPort));
      if (!state.pairingDraft.generatedPin.empty()) {
        lines.emplace_back(std::string("PIN: ") + app::current_pairing_pin(state));
        lines.emplace_back("Enter the PIN on the host.");
      }
    }
    if (!state.pairingDraft.statusMessage.empty()) {
      lines.emplace_back("Status: " + state.pairingDraft.statusMessage);
    }
    return lines;
  }

  std::vector<std::string> settings_body_lines(const app::ClientState &state) {
    std::vector<std::string> lines = {
      std::string("Category: ") + settings_category_label(state.selectedSettingsCategory),
    };
    if (state.selectedSettingsCategory == app::SettingsCategory::logging) {
      lines.emplace_back("Runtime log file: reset on every startup");
      lines.push_back(std::string("Log file path: ") + (state.logFilePath.empty() ? "not configured" : state.logFilePath));
      lines.push_back(std::string("File logging level: ") + logging::to_string(state.loggingLevel));
      lines.push_back(std::string("xemu console logging level: ") + logging::to_string(state.xemuConsoleLoggingLevel));
      lines.emplace_back("File logging writes to disk and should usually stay at NONE unless you are debugging an issue.");
      lines.emplace_back("xemu console logging writes through DbgPrint(). Start xemu with -device lpc47m157 -serial stdio.");
      lines.emplace_back("Startup console messages are always shown before the splash screen.");
      return lines;
    }
    if (state.selectedSettingsCategory == app::SettingsCategory::reset) {
      if (state.savedFiles.empty()) {
        lines.emplace_back("Saved files: none found.");
        return lines;
      }
      lines.emplace_back("Saved files on disk:");
      for (const startup::SavedFileEntry &savedFile : state.savedFiles) {
        lines.push_back("- " + savedFile.displayName + " (" + format_file_size(savedFile.sizeBytes) + ")");
      }
      return lines;
    }
    if (state.selectedSettingsCategory == app::SettingsCategory::display) {
      lines.emplace_back("Display options will be added here.");
      return lines;
    }

    lines.emplace_back("Input options will be added here.");
    return lines;
  }

  std::vector<std::string> body_lines(const app::ClientState &state) {
    switch (state.activeScreen) {
      case app::ScreenId::home:
      case app::ScreenId::hosts:
        return hosts_body_lines(state);
      case app::ScreenId::apps:
        return apps_body_lines(state);
      case app::ScreenId::add_host:
        return add_host_body_lines(state);
      case app::ScreenId::pair_host:
        return pair_host_body_lines(state);
      case app::ScreenId::settings:
        return settings_body_lines(state);
    }

    return {};
  }

  std::vector<ui::ShellActionRow> menu_rows(const app::ClientState &state) {
    std::vector<ui::ShellActionRow> rows;
    const ui::MenuItem *selectedItem = state.menu.selected_item();
    for (const ui::MenuItem &item : state.menu.items()) {
      rows.emplace_back(ui::ShellActionRow {
        item.id,
        item.label,
        item.enabled,
        selectedItem != nullptr && selectedItem->id == item.id,
        false,
      });
    }
    return rows;
  }

  std::vector<ui::ShellActionRow> detail_menu_rows(const app::ClientState &state) {
    std::vector<ui::ShellActionRow> rows;
    const ui::MenuItem *selectedItem = state.detailMenu.selected_item();
    for (const ui::MenuItem &item : state.detailMenu.items()) {
      rows.emplace_back(ui::ShellActionRow {
        item.id,
        item.label,
        item.enabled,
        state.settingsFocusArea == app::SettingsFocusArea::options && selectedItem != nullptr && selectedItem->id == item.id,
        false,
      });
    }
    return rows;
  }

  ui::ShellNotification notification(const app::ClientState &state) {
    return {
      "Notification",
      state.statusMessage,
      {
        {"dismiss-notification", "Dismiss", "icons\\button-x.svg", {}, false},
      },
    };
  }

  void fill_support_modal_view(ui::ShellViewModel *viewModel) {
    viewModel->modalTitle = "Support";
    viewModel->modalLines = {
      "Moonlight Xbox OG prototype UI",
      "Use the footer actions below to close this dialog.",
      "Open host and app context menus from the Y action on the main screens.",
    };
    viewModel->modalFooterActions = {
      {"close", "Close", "icons\\button-a.svg", "icons\\button-start.svg", true},
      {"back", "Back", "icons\\button-b.svg", {}, false},
    };
  }

  void fill_host_actions_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    viewModel->modalTitle = "Host Actions";
    viewModel->modalActions = {
      {"view-all-apps", "View all apps", true, state.modal.selectedActionIndex == 0U, false},
      {"test-connection", "Test Network Connection", true, state.modal.selectedActionIndex == 1U, false},
      {"delete-host", "Delete PC", true, state.modal.selectedActionIndex == 2U, false},
      {"view-host-details", "View Details", true, state.modal.selectedActionIndex == 3U, false},
    };
  }

  void fill_host_details_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    viewModel->modalTitle = "Host Details";
    const app::HostRecord *host = app::selected_host(state);
    if (host == nullptr) {
      return;
    }

    const char *reachabilityLabel = "UNKNOWN";
    if (host->reachability == app::HostReachability::online) {
      reachabilityLabel = "ONLINE";
    } else if (host->reachability == app::HostReachability::offline) {
      reachabilityLabel = "OFFLINE";
    }

    viewModel->modalLines = {
      "Name: " + host->displayName,
      std::string("State: ") + reachabilityLabel,
      std::string("Active Address: ") + (host->activeAddress.empty() ? "NULL" : host->activeAddress),
      std::string("UUID: ") + (host->uuid.empty() ? "NULL" : host->uuid),
      std::string("Local Address: ") + (host->localAddress.empty() ? "NULL" : host->localAddress),
      std::string("Remote Address: ") + (host->remoteAddress.empty() ? "NULL" : host->remoteAddress),
      std::string("IPv6 Address: ") + (host->ipv6Address.empty() ? "NULL" : host->ipv6Address),
      std::string("Manual Address: ") + (host->manualAddress.empty() ? host->address : host->manualAddress),
      std::string("MAC Address: ") + (host->macAddress.empty() ? "NULL" : host->macAddress),
      std::string("Pair State: ") + (host->pairingState == app::PairingState::paired ? "PAIRED" : "NOT_PAIRED"),
      "Running Game ID: " + std::to_string(host->runningGameId),
      "HTTPS Port: " + std::to_string(app::effective_host_port(host->httpsPort)),
    };
  }

  void fill_app_actions_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    if (const app::HostAppRecord *appRecord = app::selected_app(state); appRecord != nullptr) {
      viewModel->modalTitle = appRecord->name;
      viewModel->modalActions = {
        {"toggle-hidden-app", "Hide app", true, state.modal.selectedActionIndex == 0U, appRecord->hidden},
        {"view-app-details", "View details", true, state.modal.selectedActionIndex == 1U, false},
        {"create-shortcut", "Create shortcut", true, state.modal.selectedActionIndex == 2U, appRecord->favorite},
      };
    }
  }

  void fill_app_details_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    if (const app::HostAppRecord *appRecord = app::selected_app(state); appRecord != nullptr) {
      viewModel->modalTitle = "App Details";
      viewModel->modalLines = {
        "Name: " + appRecord->name,
        std::string("HDR Supported: ") + (appRecord->hdrSupported ? "YES" : "NO"),
        "ID: " + std::to_string(appRecord->id),
      };
    }
  }

  void fill_log_viewer_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    viewModel->modalTitle = "Log File";
    viewModel->logViewerVisible = true;
    viewModel->logViewerPath = state.logFilePath.empty() ? "not configured" : state.logFilePath;
    viewModel->logViewerLines = state.logViewerLines;
    viewModel->logViewerScrollOffset = state.logViewerScrollOffset;
    viewModel->logViewerPlacement = state.logViewerPlacement;
    viewModel->modalFooterActions = {
      {"older", "Older", "icons\\button-lb.svg", {}, false},
      {"newer", "Newer", "icons\\button-rb.svg", {}, false},
      {"fast-older", "Fast Older", "icons\\button-lt.svg", {}, false},
      {"fast-newer", "Fast Newer", "icons\\button-rt.svg", {}, false},
      {"move-pane", "Move Pane", "icons\\button-x.svg", {}, false},
      {"close", "Close", "icons\\button-b.svg", {}, true},
    };
  }

  void fill_confirmation_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    viewModel->modalTitle = state.confirmation.title;
    viewModel->modalLines = state.confirmation.lines;
    viewModel->modalFooterActions = {
      {"confirm", "OK", "icons\\button-a.svg", {}, state.modal.selectedActionIndex == 0U},
      {"cancel", "Cancel", "icons\\button-b.svg", {}, state.modal.selectedActionIndex != 0U},
    };
  }

  void fill_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    if (!state.modal.active()) {
      return;
    }

    viewModel->modalVisible = true;
    switch (state.modal.id) {
      case app::ModalId::support:
        fill_support_modal_view(viewModel);
        return;
      case app::ModalId::host_actions:
        fill_host_actions_modal_view(state, viewModel);
        return;
      case app::ModalId::host_details:
        fill_host_details_modal_view(state, viewModel);
        return;
      case app::ModalId::app_actions:
        fill_app_actions_modal_view(state, viewModel);
        return;
      case app::ModalId::app_details:
        fill_app_details_modal_view(state, viewModel);
        return;
      case app::ModalId::log_viewer:
        fill_log_viewer_modal_view(state, viewModel);
        return;
      case app::ModalId::confirmation:
        fill_confirmation_modal_view(state, viewModel);
        return;
      case app::ModalId::none:
        return;
    }
  }

  std::vector<ui::ShellFooterAction> footer_actions(const app::ClientState &state) {
    switch (state.activeScreen) {
      case app::ScreenId::home:
      case app::ScreenId::hosts:
        {
          std::string openLabel = "Pair";
          if (state.hostsFocusArea == app::HostsFocusArea::toolbar) {
            openLabel = "Select";
          } else if (const app::HostRecord *selectedHost = app::selected_host(state); selectedHost != nullptr && selectedHost->pairingState == app::PairingState::paired) {
            openLabel = "Open";
          }
          std::vector<ui::ShellFooterAction> actions = {
            {"open", std::move(openLabel), "icons\\button-a.svg", {}, true},
          };
          if (state.hostsFocusArea == app::HostsFocusArea::grid && app::selected_host(state) != nullptr) {
            actions.emplace_back(ui::ShellFooterAction {"host-menu", "Host Menu", "icons\\button-y.svg", {}, false});
          }
          actions.emplace_back(ui::ShellFooterAction {"exit", "Exit", "icons\\button-select.svg", "icons\\button-start.svg", false});
          return actions;
        }
      case app::ScreenId::apps:
        {
          std::vector<ui::ShellFooterAction> actions;
          if (app::selected_app(state) != nullptr) {
            actions.emplace_back(ui::ShellFooterAction {"launch", "Launch", "icons\\button-a.svg", {}, true});
            actions.emplace_back(ui::ShellFooterAction {"app-menu", "App Menu", "icons\\button-y.svg", {}, false});
          }
          actions.emplace_back(ui::ShellFooterAction {"back", "Back", "icons\\button-b.svg", {}, false});
          return actions;
        }
      case app::ScreenId::add_host:
        if (state.addHostDraft.keypad.visible) {
          return {
            {"enter", "Enter Key", "icons\\button-a.svg", {}, true},
            {"delete", "Delete", "icons\\button-x.svg", {}, false},
            {"cancel", "Cancel", "icons\\button-b.svg", {}, false},
            {"accept", "Accept", "icons\\button-start.svg", {}, false},
          };
        }
        return {
          {"select", "Select", "icons\\button-a.svg", {}, true},
          {"back", "Back", "icons\\button-b.svg", {}, false},
        };
      case app::ScreenId::pair_host:
        return {
          {"back", "Cancel", "icons\\button-b.svg", {}, false},
        };
      case app::ScreenId::settings:
        return {
          {"select", "Select", "icons\\button-a.svg", {}, true},
          {"back", state.settingsFocusArea == app::SettingsFocusArea::options ? "Categories" : "Back", "icons\\button-b.svg", {}, false},
        };
    }

    return {};
  }

}  // namespace

namespace ui {

  bool screen_uses_split_menu_layout(app::ScreenId screen) {
    return screen == app::ScreenId::settings || screen == app::ScreenId::add_host || screen == app::ScreenId::pair_host;
  }

  void fill_view_model_panel_state(const app::ClientState &state, ShellViewModel *viewModel) {
    if (!screen_uses_split_menu_layout(state.activeScreen)) {
      return;
    }

    viewModel->leftPanelActive = state.activeScreen != app::ScreenId::settings || state.settingsFocusArea == app::SettingsFocusArea::categories;
    viewModel->rightPanelActive = state.activeScreen == app::ScreenId::settings && state.settingsFocusArea == app::SettingsFocusArea::options;
  }

  void fill_view_model_selected_menu_details(const app::ClientState &state, ShellViewModel *viewModel) {
    if (!screen_uses_split_menu_layout(state.activeScreen)) {
      return;
    }

    if (state.activeScreen == app::ScreenId::settings && state.settingsFocusArea == app::SettingsFocusArea::options && state.detailMenu.selected_item() != nullptr) {
      viewModel->selectedMenuRowLabel = state.detailMenu.selected_item()->label;
      viewModel->selectedMenuRowDescription = state.detailMenu.selected_item()->description;
      return;
    }
    if (state.menu.selected_item() != nullptr) {
      viewModel->selectedMenuRowLabel = state.menu.selected_item()->label;
      viewModel->selectedMenuRowDescription = state.menu.selected_item()->description;
    }
  }

  void fill_view_model_overlay(const app::ClientState &state, const std::vector<logging::LogEntry> &logEntries, const std::vector<std::string> &statsLines, ShellViewModel *viewModel) {
    if (!viewModel->overlayVisible) {
      return;
    }

    if (!statsLines.empty()) {
      viewModel->overlayLines.insert(viewModel->overlayLines.end(), statsLines.begin(), statsLines.end());
    } else {
      viewModel->overlayLines.emplace_back("No active stream");
    }

    const std::size_t logLineLimit = 10;
    const std::size_t availableLogCount = logEntries.size();
    const std::size_t maxOffset = availableLogCount > logLineLimit ? availableLogCount - logLineLimit : 0;
    const std::size_t clampedOffset = std::min(state.overlayScrollOffset, maxOffset);
    const std::size_t startIndex = availableLogCount > logLineLimit ? availableLogCount - logLineLimit - clampedOffset : 0;
    const std::size_t endIndex = std::min(availableLogCount, startIndex + logLineLimit);

    for (std::size_t index = startIndex; index < endIndex; ++index) {
      viewModel->overlayLines.push_back(logging::format_entry(logEntries[index]));
    }

    if (clampedOffset > 0U) {
      viewModel->overlayLines.emplace(viewModel->overlayLines.begin(), "Showing earlier log entries");
    }
  }

  ShellViewModel build_shell_view_model(
    const app::ClientState &state,
    const std::vector<logging::LogEntry> &logEntries,
    const std::vector<std::string> &statsLines
  ) {
    ShellViewModel viewModel {};
    viewModel.screen = state.activeScreen;
    viewModel.title = "Moonlight";
    viewModel.pageTitle = page_title(state);
    viewModel.statusMessage = state.statusMessage;
    viewModel.notificationVisible = screen_supports_notifications(state) && !state.statusMessage.empty() && !is_minor_status_message(state) && !state.modal.active() && !(state.activeScreen == app::ScreenId::add_host && state.addHostDraft.keypad.visible);
    if (viewModel.notificationVisible) {
      viewModel.notification = notification(state);
    }
    viewModel.hostColumnCount = 3U;
    viewModel.appColumnCount = 4U;
    viewModel.toolbarButtons = toolbar_buttons(state);
    viewModel.hostTiles = host_tiles(state);
    viewModel.appTiles = app_tiles(state);
    viewModel.bodyLines = body_lines(state);
    viewModel.menuRows = menu_rows(state);
    viewModel.detailMenuRows = detail_menu_rows(state);
    fill_view_model_panel_state(state, &viewModel);
    fill_view_model_selected_menu_details(state, &viewModel);
    viewModel.footerActions = footer_actions(state);
    viewModel.overlayVisible = state.overlayVisible;
    viewModel.overlayTitle = "Diagnostics";
    viewModel.keypadModalVisible = state.activeScreen == app::ScreenId::add_host && state.addHostDraft.keypad.visible;
    viewModel.keypadModalTitle = state.addHostDraft.activeField == app::AddHostField::address ? "Address Keypad" : "Port Keypad";
    viewModel.keypadModalColumnCount = 3;

    if (viewModel.keypadModalVisible) {
      viewModel.keypadModalLines = keypad_modal_lines(state);
      viewModel.keypadModalButtons = keypad_buttons(state);
    }

    fill_modal_view(state, &viewModel);

    fill_view_model_overlay(state, logEntries, statsLines, &viewModel);

    return viewModel;
  }

}  // namespace ui
