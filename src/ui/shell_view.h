#pragma once

// standard includes
#include <cstddef>
#include <string>
#include <vector>

// local includes
#include "src/app/client_state.h"
#include "src/logging/logger.h"

namespace ui {

  /**
   * @brief Render-ready button shown in the hosts toolbar.
   */
  struct ShellToolbarButton {
    std::string id;
    std::string label;
    std::string glyph;
    std::string iconAssetPath;
    bool selected = false;
  };

  /**
   * @brief Render-ready host tile for the Moonlight-style hosts page.
   */
  struct ShellHostTile {
    std::string id;
    std::string displayName;
    std::string statusLabel;
    std::string iconAssetPath;
    app::PairingState pairingState = app::PairingState::not_paired;
    app::HostReachability reachability = app::HostReachability::unknown;
    bool selected = false;
  };

  /**
   * @brief Render-ready app tile for the per-host apps page.
   */
  struct ShellAppTile {
    std::string id;
    std::string name;
    std::string detail;
    std::string badgeLabel;
    std::string boxArtCacheKey;
    bool hidden = false;
    bool favorite = false;
    bool boxArtCached = false;
    bool running = false;
    bool selected = false;
  };

  /**
   * @brief Render-ready vertical action row used by menus and modals.
   */
  struct ShellActionRow {
    std::string id;
    std::string label;
    bool enabled = true;
    bool selected = false;
    bool checked = false;
  };

  /**
   * @brief Render-ready footer chip pairing a button icon with an action label.
   */
  struct ShellFooterAction {
    std::string id;
    std::string label;
    std::string iconAssetPath;
    std::string secondaryIconAssetPath;
    bool emphasized = false;
  };

  /**
   * @brief Render-ready bottom-right notification shown above the shell content.
   */
  struct ShellNotification {
    std::string title;
    std::string message;
    std::vector<ShellFooterAction> actions;
  };

  /**
   * @brief Render-ready button in the keypad modal.
   */
  struct ShellModalButton {
    std::string label;
    bool enabled = true;
    bool selected = false;
  };

  /**
   * @brief Render-ready shell state derived from the app model.
   */
  struct ShellViewModel {
    app::ScreenId screen = app::ScreenId::hosts;
    std::string title;
    std::string pageTitle;
    std::string statusMessage;
    bool notificationVisible = false;
    ShellNotification notification;
    std::vector<ShellToolbarButton> toolbarButtons;
    std::vector<ShellHostTile> hostTiles;
    std::size_t hostColumnCount = 3;
    std::vector<ShellAppTile> appTiles;
    std::size_t appColumnCount = 4;
    std::vector<std::string> bodyLines;
    std::vector<ShellActionRow> menuRows;
    std::vector<ShellActionRow> detailMenuRows;
    std::string selectedMenuRowLabel;
    std::vector<ShellFooterAction> footerActions;
    bool overlayVisible = false;
    std::string overlayTitle;
    std::vector<std::string> overlayLines;
    bool modalVisible = false;
    std::string modalTitle;
    std::vector<std::string> modalLines;
    std::vector<ShellActionRow> modalActions;
    std::vector<ShellFooterAction> modalFooterActions;
    bool logViewerVisible = false;
    std::string logViewerPath;
    std::vector<std::string> logViewerLines;
    std::size_t logViewerScrollOffset = 0U;
    app::LogViewerPlacement logViewerPlacement = app::LogViewerPlacement::full;
    bool keypadModalVisible = false;
    std::string keypadModalTitle;
    std::vector<std::string> keypadModalLines;
    std::vector<ShellModalButton> keypadModalButtons;
    std::size_t keypadModalColumnCount = 0;
  };

  /**
   * @brief Build a render-ready shell view from app state and diagnostics.
   *
   * @param state Current app shell state.
   * @param logEntries Recent log entries for the optional overlay.
   * @param statsLines Optional streaming statistics overlay lines.
   * @return A render-ready shell view model.
   */
  ShellViewModel build_shell_view_model(
    const app::ClientState &state,
    const std::vector<logging::LogEntry> &logEntries,
    const std::vector<std::string> &statsLines = {}
  );

}  // namespace ui
