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
   * @brief Render-ready menu row for the SDL shell.
   */
  struct ShellMenuRow {
    std::string id;
    std::string label;
    bool enabled;
    bool selected;
  };

  /**
   * @brief Render-ready button in the add-host keypad modal.
   */
  struct ShellModalButton {
    std::string label;
    bool enabled;
    bool selected;
  };

  /**
   * @brief Render-ready shell state derived from the app model.
   */
  struct ShellViewModel {
    std::string title;
    std::vector<std::string> bodyLines;
    std::vector<ShellMenuRow> menuRows;
    std::vector<std::string> footerLines;
    bool overlayVisible;
    std::string overlayTitle;
    std::vector<std::string> overlayLines;
    bool keypadModalVisible;
    std::string keypadModalTitle;
    std::vector<std::string> keypadModalLines;
    std::vector<ShellModalButton> keypadModalButtons;
    std::size_t keypadModalColumnCount;
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
