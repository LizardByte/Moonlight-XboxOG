// class header include
#include "src/ui/shell_view.h"

// standard includes
#include <algorithm>
#include <cstddef>
#include <vector>

namespace {

  bool starts_with(const std::string &value, const char *prefix) {
    return value.rfind(prefix, 0U) == 0U;
  }

  bool screen_supports_notifications(const app::ClientState &state) {
    return state.activeScreen == app::ScreenId::home
      || state.activeScreen == app::ScreenId::hosts
      || state.activeScreen == app::ScreenId::apps
      || state.activeScreen == app::ScreenId::settings;
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

    if (starts_with(state.statusMessage, "Loaded recent log file lines")
      || starts_with(state.statusMessage, "No log file has been written yet")
      || starts_with(state.statusMessage, "Testing connection to ")
      || starts_with(state.statusMessage, "Editing host ")
      || starts_with(state.statusMessage, "Updated host ")
      || starts_with(state.statusMessage, "Cancelled host ")
      || starts_with(state.statusMessage, "Using default Moonlight host port")
      || starts_with(state.statusMessage, "Loading apps for ")
      || starts_with(state.statusMessage, "Pairing is preparing the client identity")) {
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
      tiles.push_back({
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
      }
      else if (appRecord.hdrSupported) {
        badgeLabel = "HDR";
      }
      else if (appRecord.hidden) {
        badgeLabel = "Hidden";
      }

      tiles.push_back({
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
      return state.addHostDraft.keypad.stagedInput.empty() && state.addHostDraft.activeField == app::AddHostField::port
        ? "default (47989)"
        : state.addHostDraft.keypad.stagedInput;
    }

    return state.addHostDraft.activeField == app::AddHostField::address
      ? state.addHostDraft.addressInput
      : (state.addHostDraft.portInput.empty() ? "default (47989)" : state.addHostDraft.portInput);
  }

  std::vector<ui::ShellModalButton> keypad_buttons(const app::ClientState &state) {
    const std::vector<std::string> labels = state.addHostDraft.activeField == app::AddHostField::address
      ? std::vector<std::string> {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0"}
      : std::vector<std::string> {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};

    std::vector<ui::ShellModalButton> buttons;
    buttons.reserve(labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) {
      buttons.push_back({
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
      "Use the D-pad to choose a key, A to enter it, X to delete, Start to accept, and B to cancel.",
    };

    if (state.addHostDraft.activeField == app::AddHostField::address) {
      lines.emplace_back("Enter a dotted IPv4 address such as 192.168.0.10.");
    }
    else {
      lines.emplace_back("Enter digits for a custom TCP port, or leave it empty to keep the default of 47989.");
    }

    return lines;
  }

  std::vector<std::string> body_lines(const app::ClientState &state) {
    switch (state.activeScreen) {
      case app::ScreenId::home:
      case app::ScreenId::hosts:
        if (state.hosts.empty()) {
          return {
            "No PCs have been added yet.",
            "Use Add Host to save a Sunshine host manually.",
            "A Moonlight-style discovery grid now owns the home screen.",
          };
        }
        return {
          "Select a PC to pair or browse its apps.",
          "Long-press A on a controller, or press Y/I, for host actions.",
        };
      case app::ScreenId::apps:
        {
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
              host->appListStatusMessage.empty()
                ? "The app list could not be refreshed."
                : host->appListStatusMessage,
            };
          }
          if (host->apps.empty()) {
            return {"Host did not return any launchable apps for this host."};
          }

          return {};
        }
      case app::ScreenId::add_host:
        {
          std::vector<std::string> lines = {
            "Manual host entry with a keypad modal.",
            std::string("Address: ") + app::current_add_host_address(state),
            std::string("Port: ") + (state.addHostDraft.portInput.empty() ? std::string("default (47989)") : state.addHostDraft.portInput),
            "Press A to edit either field with the keypad modal.",
          };

          if (!state.addHostDraft.validationMessage.empty()) {
            lines.push_back("Validation: " + state.addHostDraft.validationMessage);
          }
          if (!state.addHostDraft.connectionMessage.empty()) {
            lines.push_back("Connection: " + state.addHostDraft.connectionMessage);
          }
          return lines;
        }
      case app::ScreenId::pair_host:
        {
          std::vector<std::string> lines = {
            std::string("Target host: ") + state.pairingDraft.targetAddress,
          };
          if (state.pairingDraft.stage == app::PairingStage::idle) {
            lines.push_back("Checking whether the host is reachable before showing a PIN.");
          }
          else {
            lines.push_back(std::string("Target port: ") + std::to_string(state.pairingDraft.targetPort));
            if (!state.pairingDraft.generatedPin.empty()) {
              lines.push_back(std::string("PIN: ") + app::current_pairing_pin(state));
              lines.push_back("Enter the PIN on the host only if Sunshine prompts for it.");
            }
          }
          if (!state.pairingDraft.statusMessage.empty()) {
            lines.push_back("Status: " + state.pairingDraft.statusMessage);
          }
          return lines;
        }
      case app::ScreenId::settings:
        {
          std::vector<std::string> lines = {
          std::string("Category: ") + (state.selectedSettingsCategory == app::SettingsCategory::logging
            ? "Logging"
            : state.selectedSettingsCategory == app::SettingsCategory::display
              ? "Display"
              : state.selectedSettingsCategory == app::SettingsCategory::input ? "Input" : "Reset"),
          };
          if (state.selectedSettingsCategory == app::SettingsCategory::logging) {
            lines.push_back(std::string("Log file: ") + (state.logFilePath.empty() ? "not configured" : state.logFilePath));
            lines.push_back(std::string("Current logging level: ") + logging::to_string(state.loggingLevel));
            lines.push_back("Use View Log File to inspect persisted startup and applist diagnostics.");
          }
          else if (state.selectedSettingsCategory == app::SettingsCategory::reset) {
            if (state.savedFiles.empty()) {
              lines.push_back("Saved files: none found.");
              return lines;
            }
            lines.push_back("Saved files on disk:");
            for (const startup::SavedFileEntry &savedFile : state.savedFiles) {
              lines.push_back("- " + savedFile.displayName + " (" + format_file_size(savedFile.sizeBytes) + ")");
            }
          }
          else if (state.selectedSettingsCategory == app::SettingsCategory::display) {
            lines.push_back("Display options will be added here.");
          }
          else {
            lines.push_back("Input options will be added here.");
          }
          return lines;
        }
    }

    return {};
  }

  std::vector<ui::ShellActionRow> menu_rows(const app::ClientState &state) {
    std::vector<ui::ShellActionRow> rows;
    const ui::MenuItem *selectedItem = state.menu.selected_item();
    for (const ui::MenuItem &item : state.menu.items()) {
      rows.push_back({
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
      rows.push_back({
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

  void fill_modal_view(const app::ClientState &state, ui::ShellViewModel *viewModel) {
    if (!state.modal.active()) {
      return;
    }

    viewModel->modalVisible = true;
    switch (state.modal.id) {
      case app::ModalId::support:
        viewModel->modalTitle = "Support";
        viewModel->modalLines = {
          "Moonlight Xbox OG prototype UI",
          "A / Start: close",
          "B: close",
          "Y or I: open context menus on hosts and apps",
        };
        return;
      case app::ModalId::host_actions:
        viewModel->modalTitle = "Host Actions";
        viewModel->modalActions = {
          {"view-all-apps", "View all apps", true, state.modal.selectedActionIndex == 0U, false},
          {"test-connection", "Test Network Connection", true, state.modal.selectedActionIndex == 1U, false},
          {"delete-host", "Delete PC", true, state.modal.selectedActionIndex == 2U, false},
          {"view-host-details", "View Details", true, state.modal.selectedActionIndex == 3U, false},
        };
        return;
      case app::ModalId::host_details:
        {
          viewModel->modalTitle = "Host Details";
          if (const app::HostRecord *host = app::selected_host(state); host != nullptr) {
            viewModel->modalLines = {
              "Name: " + host->displayName,
              std::string("State: ") + (host->reachability == app::HostReachability::online ? "ONLINE" : host->reachability == app::HostReachability::offline ? "OFFLINE" : "UNKNOWN"),
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
          return;
        }
      case app::ModalId::app_actions:
        if (const app::HostAppRecord *appRecord = app::selected_app(state); appRecord != nullptr) {
          viewModel->modalTitle = appRecord->name;
          viewModel->modalActions = {
            {"toggle-hidden-app", "Hide app", true, state.modal.selectedActionIndex == 0U, appRecord->hidden},
            {"view-app-details", "View details", true, state.modal.selectedActionIndex == 1U, false},
            {"create-shortcut", "Create shortcut", true, state.modal.selectedActionIndex == 2U, appRecord->favorite},
          };
        }
        return;
      case app::ModalId::app_details:
        if (const app::HostAppRecord *appRecord = app::selected_app(state); appRecord != nullptr) {
          viewModel->modalTitle = "App Details";
          viewModel->modalLines = {
            "Name: " + appRecord->name,
            std::string("HDR Supported: ") + (appRecord->hdrSupported ? "YES" : "NO"),
            "ID: " + std::to_string(appRecord->id),
          };
        }
        return;
      case app::ModalId::log_viewer:
        viewModel->modalTitle = "Log File";
        viewModel->logViewerVisible = true;
        viewModel->logViewerPath = state.logFilePath.empty() ? "not configured" : state.logFilePath;
        viewModel->logViewerLines = state.logViewerLines;
        viewModel->logViewerScrollOffset = state.logViewerScrollOffset;
        viewModel->logViewerPlacement = state.logViewerPlacement;
        return;
      case app::ModalId::confirmation:
        viewModel->modalTitle = state.confirmation.title;
        viewModel->modalLines = state.confirmation.lines;
        viewModel->modalFooterActions = {
          {"confirm", "OK", "icons\\button-a.svg", {}, state.modal.selectedActionIndex == 0U},
          {"cancel", "Cancel", "icons\\button-b.svg", {}, state.modal.selectedActionIndex != 0U},
        };
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
          std::vector<ui::ShellFooterAction> actions = {
            {"open", state.hostsFocusArea == app::HostsFocusArea::toolbar ? "Select" : (app::selected_host(state) != nullptr && app::selected_host(state)->pairingState == app::PairingState::paired ? "Open" : "Pair"), "icons\\button-a.svg", {}, true},
          };
          if (state.hostsFocusArea == app::HostsFocusArea::grid && app::selected_host(state) != nullptr) {
            actions.push_back({"host-menu", "Host Menu", "icons\\button-y.svg", {}, false});
          }
          actions.push_back({"exit", "Exit", "icons\\button-start.svg", "icons\\button-select.svg", false});
          return actions;
        }
      case app::ScreenId::apps:
        {
          std::vector<ui::ShellFooterAction> actions;
          if (app::selected_app(state) != nullptr) {
            actions.push_back({"launch", "Launch", "icons\\button-a.svg", {}, true});
            actions.push_back({"app-menu", "App Menu", "icons\\button-y.svg", {}, false});
          }
          actions.push_back({"back", "Back", "icons\\button-b.svg", {}, false});
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
    viewModel.notificationVisible = screen_supports_notifications(state)
      && !state.statusMessage.empty()
      && !is_minor_status_message(state)
      && !state.modal.active()
      && !(state.activeScreen == app::ScreenId::add_host && state.addHostDraft.keypad.visible);
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
    if (state.activeScreen == app::ScreenId::settings) {
      if (state.settingsFocusArea == app::SettingsFocusArea::options && state.detailMenu.selected_item() != nullptr) {
        viewModel.selectedMenuRowLabel = state.detailMenu.selected_item()->label;
      }
      else if (state.menu.selected_item() != nullptr) {
        viewModel.selectedMenuRowLabel = state.menu.selected_item()->label;
      }
    }
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

    if (viewModel.overlayVisible) {
      if (!statsLines.empty()) {
        viewModel.overlayLines.insert(viewModel.overlayLines.end(), statsLines.begin(), statsLines.end());
      }
      else {
        viewModel.overlayLines.emplace_back("No active stream");
      }

      const std::size_t logLineLimit = 10;
      const std::size_t availableLogCount = logEntries.size();
      const std::size_t maxOffset = availableLogCount > logLineLimit ? availableLogCount - logLineLimit : 0;
      const std::size_t clampedOffset = std::min(state.overlayScrollOffset, maxOffset);
      const std::size_t startIndex = availableLogCount > logLineLimit ? availableLogCount - logLineLimit - clampedOffset : 0;
      const std::size_t endIndex = std::min(availableLogCount, startIndex + logLineLimit);
      for (std::size_t index = startIndex; index < endIndex; ++index) {
        viewModel.overlayLines.push_back(logging::format_entry(logEntries[index]));
      }

      if (clampedOffset > 0) {
        viewModel.overlayLines.insert(viewModel.overlayLines.begin(), "Showing earlier log entries");
      }
    }

    return viewModel;
  }

}  // namespace ui
