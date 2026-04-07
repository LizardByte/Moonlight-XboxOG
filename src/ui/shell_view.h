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
    std::string id;  ///< Stable button identifier.
    std::string label;  ///< User-facing toolbar label.
    std::string glyph;  ///< Optional text glyph used when no icon asset is available.
    std::string iconAssetPath;  ///< Optional asset path for the button icon.
    bool selected = false;  ///< True when the button currently has focus.
  };

  /**
   * @brief Render-ready host tile for the Moonlight-style hosts page.
   */
  struct ShellHostTile {
    std::string id;  ///< Stable host tile identifier.
    std::string displayName;  ///< Primary host label.
    std::string statusLabel;  ///< Secondary host status text.
    std::string iconAssetPath;  ///< Asset path for the tile icon.
    app::PairingState pairingState = app::PairingState::not_paired;  ///< Pairing state displayed on the tile.
    app::HostReachability reachability = app::HostReachability::unknown;  ///< Reachability state displayed on the tile.
    bool selected = false;  ///< True when the tile currently has focus.
  };

  /**
   * @brief Render-ready app tile for the per-host apps page.
   */
  struct ShellAppTile {
    std::string id;  ///< Stable app tile identifier.
    std::string name;  ///< Primary app label.
    std::string detail;  ///< Secondary app detail text.
    std::string badgeLabel;  ///< Optional badge label such as HDR or Running.
    std::string boxArtCacheKey;  ///< Cache key used to resolve box art.
    bool hidden = false;  ///< True when the app is hidden by default.
    bool favorite = false;  ///< True when the app is flagged as a favorite.
    bool boxArtCached = false;  ///< True when box art is already available locally.
    bool running = false;  ///< True when the app is currently running on the host.
    bool selected = false;  ///< True when the tile currently has focus.
  };

  /**
   * @brief Render-ready vertical action row used by menus and modals.
   */
  struct ShellActionRow {
    std::string id;  ///< Stable row identifier.
    std::string label;  ///< User-facing row label.
    bool enabled = true;  ///< True when the row can be activated.
    bool selected = false;  ///< True when the row currently has focus.
    bool checked = false;  ///< True when the row represents an enabled option.
  };

  /**
   * @brief Render-ready footer chip pairing a button icon with an action label.
   */
  struct ShellFooterAction {
    std::string id;  ///< Stable footer action identifier.
    std::string label;  ///< User-facing footer label.
    std::string iconAssetPath;  ///< Primary icon asset path.
    std::string secondaryIconAssetPath;  ///< Optional secondary icon asset path.
    bool emphasized = false;  ///< True when the action should receive visual emphasis.
  };

  /**
   * @brief Render-ready bottom-right notification shown above the shell content.
   */
  struct ShellNotification {
    std::string title;  ///< Notification title.
    std::string message;  ///< Notification body message.
    std::vector<ShellFooterAction> actions;  ///< Footer actions shown with the notification.
  };

  /**
   * @brief Render-ready button in the keypad modal.
   */
  struct ShellModalButton {
    std::string label;  ///< User-facing button label.
    bool enabled = true;  ///< True when the button can be activated.
    bool selected = false;  ///< True when the button currently has focus.
  };

  /**
   * @brief Render-ready shell state derived from the app model.
   */
  struct ShellViewModel {
    app::ScreenId screen = app::ScreenId::hosts;  ///< Active screen being rendered.
    std::string title;  ///< Shell-wide title.
    std::string pageTitle;  ///< Primary page heading.
    std::string statusMessage;  ///< Status line shown near the footer.
    bool notificationVisible = false;  ///< True when a transient notification should be rendered.
    ShellNotification notification;  ///< Notification content when visible.
    std::vector<ShellToolbarButton> toolbarButtons;  ///< Toolbar buttons for the hosts page.
    std::vector<ShellHostTile> hostTiles;  ///< Host tiles shown on the hosts page.
    std::size_t hostColumnCount = 3;  ///< Number of columns used to lay out host tiles.
    std::vector<ShellAppTile> appTiles;  ///< App tiles shown on the apps page.
    std::size_t appColumnCount = 4;  ///< Number of columns used to lay out app tiles.
    std::vector<std::string> bodyLines;  ///< Generic body lines for text-driven screens.
    std::vector<ShellActionRow> menuRows;  ///< Primary action rows for the active screen.
    std::vector<ShellActionRow> detailMenuRows;  ///< Secondary action rows for details or settings.
    std::string selectedMenuRowLabel;  ///< Label of the currently selected primary menu row.
    std::vector<ShellFooterAction> footerActions;  ///< Footer actions shown for the current screen.
    bool overlayVisible = false;  ///< True when the diagnostics overlay should be rendered.
    std::string overlayTitle;  ///< Diagnostics overlay title.
    std::vector<std::string> overlayLines;  ///< Diagnostics overlay body lines.
    bool modalVisible = false;  ///< True when a modal dialog should be rendered.
    std::string modalTitle;  ///< Modal dialog title.
    std::vector<std::string> modalLines;  ///< Modal dialog body lines.
    std::vector<ShellActionRow> modalActions;  ///< Modal action rows.
    std::vector<ShellFooterAction> modalFooterActions;  ///< Footer actions displayed while a modal is open.
    bool logViewerVisible = false;  ///< True when the log viewer should be rendered.
    std::string logViewerPath;  ///< Path of the currently loaded log file.
    std::vector<std::string> logViewerLines;  ///< Loaded log lines shown in the viewer.
    std::size_t logViewerScrollOffset = 0U;  ///< Vertical scroll offset inside the log viewer.
    app::LogViewerPlacement logViewerPlacement = app::LogViewerPlacement::full;  ///< Placement of the log viewer pane.
    bool keypadModalVisible = false;  ///< True when the keypad modal should be rendered.
    std::string keypadModalTitle;  ///< Title shown at the top of the keypad modal.
    std::vector<std::string> keypadModalLines;  ///< Instruction and draft lines shown in the keypad modal.
    std::vector<ShellModalButton> keypadModalButtons;  ///< Buttons rendered inside the keypad modal.
    std::size_t keypadModalColumnCount = 0;  ///< Number of columns used to lay out keypad buttons.
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
