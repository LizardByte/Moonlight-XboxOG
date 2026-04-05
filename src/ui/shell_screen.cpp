// class header include
#include "src/ui/shell_screen.h"

// standard includes
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

// nxdk includes
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <hal/debug.h>
#include <windows.h>

// local includes
#include "src/input/navigation_input.h"
#include "src/network/host_pairing.h"
#include "src/network/runtime_network.h"
#include "src/os.h"
#include "src/startup/client_identity_storage.h"
#include "src/startup/host_storage.h"
#include "src/ui/shell_view.h"

namespace {

  constexpr std::size_t PAIRING_THREAD_STACK_SIZE = 1024U * 1024U;

  constexpr Uint8 BACKGROUND_RED = 0x10;
  constexpr Uint8 BACKGROUND_GREEN = 0x12;
  constexpr Uint8 BACKGROUND_BLUE = 0x16;
  constexpr Uint8 PANEL_RED = 0x1A;
  constexpr Uint8 PANEL_GREEN = 0x1F;
  constexpr Uint8 PANEL_BLUE = 0x25;
  constexpr Uint8 ACCENT_RED = 0x76;
  constexpr Uint8 ACCENT_GREEN = 0xB9;
  constexpr Uint8 ACCENT_BLUE = 0xFF;
  constexpr Uint8 TEXT_RED = 0xF2;
  constexpr Uint8 TEXT_GREEN = 0xF5;
  constexpr Uint8 TEXT_BLUE = 0xF8;
  constexpr Uint8 MUTED_RED = 0xA3;
  constexpr Uint8 MUTED_GREEN = 0xAB;
  constexpr Uint8 MUTED_BLUE = 0xB5;
  constexpr Sint16 TRIGGER_PAGE_SCROLL_THRESHOLD = 16000;

  std::string build_asset_path(const char *relativePath) {
    return std::string(DATA_PATH) + "assets" + PATH_SEP + relativePath;
  }

  int report_shell_failure(logging::Logger &logger, const char *category, const std::string &message) {
    logger.log(logging::LogLevel::error, category, message);
    debugPrint("%s\n", message.c_str());
    debugPrint("Holding failure screen for 5 seconds before exit.\n");
    Sleep(5000);
    return 1;
  }

  void destroy_texture(SDL_Texture *texture) {
    if (texture != nullptr) {
      SDL_DestroyTexture(texture);
    }
  }

  SDL_Texture *load_texture_from_asset(SDL_Renderer *renderer, const char *relativePath) {
    if (renderer == nullptr || relativePath == nullptr) {
      return nullptr;
    }

    const std::string assetPath = build_asset_path(relativePath);
    SDL_Surface *surface = IMG_Load(assetPath.c_str());
    if (surface == nullptr) {
      return nullptr;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
  }

  void fill_rect(SDL_Renderer *renderer, const SDL_Rect &rect, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 0xFF) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderFillRect(renderer, &rect);
  }

  void draw_rect(SDL_Renderer *renderer, const SDL_Rect &rect, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 0xFF) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderDrawRect(renderer, &rect);
  }

  bool render_text_line(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    int x,
    int y,
    int maxWidth,
    int *drawnHeight = nullptr
  ) {
    if (text.empty()) {
      if (drawnHeight != nullptr) {
        *drawnHeight = TTF_FontLineSkip(font);
      }
      return true;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, static_cast<Uint32>(maxWidth));
    if (surface == nullptr) {
      return false;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
      SDL_FreeSurface(surface);
      return false;
    }

    SDL_Rect destination {x, y, surface->w, surface->h};
    SDL_FreeSurface(surface);
    const int renderResult = SDL_RenderCopy(renderer, texture, nullptr, &destination);
    destroy_texture(texture);

    if (drawnHeight != nullptr) {
      *drawnHeight = destination.h;
    }

    return renderResult == 0;
  }

  int measure_wrapped_text_height(TTF_Font *font, const std::string &text, int maxWidth) {
    if (text.empty()) {
      return TTF_FontLineSkip(font);
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, static_cast<Uint32>(maxWidth));
    if (surface == nullptr) {
      return TTF_FontLineSkip(font);
    }

    const int height = surface->h;
    SDL_FreeSurface(surface);
    return height;
  }

  bool render_text_centered(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    const SDL_Rect &rect,
    int *drawnHeight = nullptr
  ) {
    int textWidth = 0;
    int textHeight = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &textWidth, &textHeight) != 0) {
      return render_text_line(renderer, font, text, color, rect.x + 8, rect.y + 8, rect.w - 16, drawnHeight);
    }

    const int x = rect.x + std::max(0, (rect.w - textWidth) / 2);
    const int y = rect.y + std::max(0, (rect.h - textHeight) / 2);
    return render_text_line(renderer, font, text, color, x, y, rect.w, drawnHeight);
  }

  input::UiCommand translate_controller_button(Uint8 button) {
    switch (button) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_up);
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_down);
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_left);
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_right);
      case SDL_CONTROLLER_BUTTON_A:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::a);
      case SDL_CONTROLLER_BUTTON_B:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::b);
      case SDL_CONTROLLER_BUTTON_X:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::x);
      case SDL_CONTROLLER_BUTTON_Y:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::y);
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::left_shoulder);
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::right_shoulder);
      case SDL_CONTROLLER_BUTTON_START:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::start);
      case SDL_CONTROLLER_BUTTON_BACK:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::back);
      default:
        return input::UiCommand::none;
    }
  }

  input::UiCommand translate_keyboard_key(SDL_Keycode key, Uint16 modifiers) {
    const bool shiftPressed = (modifiers & KMOD_SHIFT) != 0;

    switch (key) {
      case SDLK_UP:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::up, shiftPressed);
      case SDLK_DOWN:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::down, shiftPressed);
      case SDLK_LEFT:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::left, shiftPressed);
      case SDLK_RIGHT:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::right, shiftPressed);
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::enter, shiftPressed);
      case SDLK_ESCAPE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::escape, shiftPressed);
      case SDLK_BACKSPACE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::backspace, shiftPressed);
      case SDLK_DELETE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::delete_key, shiftPressed);
      case SDLK_SPACE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::space, shiftPressed);
      case SDLK_TAB:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::tab, shiftPressed);
      case SDLK_PAGEUP:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::page_up, shiftPressed);
      case SDLK_PAGEDOWN:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::page_down, shiftPressed);
      case SDLK_i:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::i, shiftPressed);
      case SDLK_F3:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::f3, shiftPressed);
      default:
        return input::UiCommand::none;
    }
  }

  input::UiCommand translate_trigger_axis(const SDL_ControllerAxisEvent &event, bool *leftTriggerPressed, bool *rightTriggerPressed) {
    if (leftTriggerPressed == nullptr || rightTriggerPressed == nullptr) {
      return input::UiCommand::none;
    }

    const bool thresholdCrossed = event.value >= TRIGGER_PAGE_SCROLL_THRESHOLD;

    switch (event.axis) {
      case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        if (thresholdCrossed && !*leftTriggerPressed) {
          *leftTriggerPressed = true;
          return input::UiCommand::previous_page;
        }
        if (!thresholdCrossed) {
          *leftTriggerPressed = false;
        }
        break;
      case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        if (thresholdCrossed && !*rightTriggerPressed) {
          *rightTriggerPressed = true;
          return input::UiCommand::next_page;
        }
        if (!thresholdCrossed) {
          *rightTriggerPressed = false;
        }
        break;
      default:
        break;
    }

    return input::UiCommand::none;
  }

  void log_app_update(logging::Logger &logger, const app::ClientState &state, const app::AppUpdate &update) {
    if (!update.activatedItemId.empty()) {
      logger.log(logging::LogLevel::info, "ui", "Activated menu item: " + update.activatedItemId);
    }
    if (update.screenChanged) {
      logger.log(logging::LogLevel::info, "ui", std::string("Switched screen to ") + app::to_string(state.activeScreen));
    }
    if (update.overlayVisibilityChanged) {
      logger.log(logging::LogLevel::info, "overlay", state.overlayVisible ? "Overlay enabled" : "Overlay disabled");
    }
    if (update.exitRequested) {
      logger.log(logging::LogLevel::info, "app", "Exit requested from shell");
    }
  }

  bool persist_hosts(logging::Logger &logger, app::ClientState &state) {
    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(state.hosts);
    if (saveResult.success) {
      state.hostsDirty = false;
      logger.log(logging::LogLevel::info, "hosts", "Saved host records");
      return true;
    }

    logger.log(logging::LogLevel::error, "hosts", saveResult.errorMessage);
    return false;
  }

  void persist_hosts_if_needed(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update) {
    if (!update.hostsChanged) {
      return;
    }

    persist_hosts(logger, state);
  }

  bool test_tcp_host_connection(const std::string &address, uint16_t port, std::string *message) {
    if (!network::runtime_network_ready()) {
      if (message != nullptr) {
        *message = network::runtime_network_status().summary;
      }
      return false;
    }

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;
    if (!network::query_server_info(address, port, &serverInfo, &errorMessage)) {
      if (message != nullptr) {
        *message = std::move(errorMessage);
      }
      return false;
    }

    if (message != nullptr) {
      *message = "Received /serverinfo from " + address + ":" + std::to_string(serverInfo.httpPort)
        + " and discovered HTTPS pairing on port " + std::to_string(serverInfo.httpsPort);
    }
    return true;
  }

  struct PairingTaskState {
    SDL_Thread *thread;
    std::atomic<bool> completed;
    std::atomic<bool> discardResult;
    network::HostPairingRequest request;
    network::HostPairingResult result;
    struct DeferredLogEntry {
      logging::LogLevel level;
      std::string message;
    };
    std::vector<DeferredLogEntry> deferredLogs;
  };

  void reset_pairing_task(PairingTaskState *task) {
    if (task == nullptr) {
      return;
    }

    task->thread = nullptr;
    task->completed.store(false);
    task->discardResult.store(false);
    task->request = {};
    task->result = {false, false, {}};
    task->deferredLogs.clear();
  }

  bool pairing_task_is_active(const PairingTaskState &task) {
    return task.thread != nullptr && !task.completed.load();
  }

  int run_pairing_task(void *context) {
    PairingTaskState *task = static_cast<PairingTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    debugPrint("[PAIRING] worker entered\n");

    const startup::LoadClientIdentityResult loadedIdentity = startup::load_client_identity();
    debugPrint("[PAIRING] client identity load completed\n");
    for (const std::string &warning : loadedIdentity.warnings) {
      task->deferredLogs.push_back({logging::LogLevel::warning, warning});
    }

    network::PairingIdentity identity = loadedIdentity.identity;
    if (!loadedIdentity.fileFound || !network::is_valid_pairing_identity(identity)) {
      if (loadedIdentity.fileFound) {
        task->deferredLogs.push_back({logging::LogLevel::warning, "Stored pairing identity was invalid. Generating a new one."});
      }

      std::string identityError;
      debugPrint("[PAIRING] generating client identity\n");
      identity = network::create_pairing_identity(&identityError);
      if (!network::is_valid_pairing_identity(identity)) {
        task->result = {
          false,
          false,
          identityError.empty()
            ? "Failed to generate a valid client pairing identity"
            : "Failed to generate a valid client pairing identity: " + identityError,
        };
        task->completed.store(true, std::memory_order_release);
        return 0;
      }

      const startup::SaveClientIdentityResult saveResult = startup::save_client_identity(identity);
      debugPrint("[PAIRING] client identity save completed\n");
      if (!saveResult.success) {
        task->result = {false, false, saveResult.errorMessage};
        task->completed.store(true, std::memory_order_release);
        return 0;
      }

      task->deferredLogs.push_back({logging::LogLevel::info, "Saved pairing identity"});
    }

    task->request.identity = std::move(identity);
    debugPrint("[PAIRING] invoking network::pair_host\n");
    task->result = network::pair_host(task->request);
    debugPrint("[PAIRING] network::pair_host returned\n");
    task->completed.store(true, std::memory_order_release);
    return 0;
  }

  void finish_pairing_task_if_ready(logging::Logger &logger, app::ClientState &state, PairingTaskState *task) {
    if (task == nullptr || task->thread == nullptr || !task->completed.load(std::memory_order_acquire)) {
      return;
    }

    SDL_Thread *thread = task->thread;
    task->thread = nullptr;
    int threadResult = 0;
    SDL_WaitThread(thread, &threadResult);
    (void) threadResult;

    const network::HostPairingRequest request = task->request;
    const network::HostPairingResult result = task->result;
    const bool discardResult = task->discardResult.load();
    const std::vector<PairingTaskState::DeferredLogEntry> deferredLogs = task->deferredLogs;
    reset_pairing_task(task);

    for (const PairingTaskState::DeferredLogEntry &entry : deferredLogs) {
      logger.log(entry.level, "pairing", entry.message);
    }

    if (discardResult) {
      logger.log(logging::LogLevel::info, "pairing", "Ignored a completed pairing result after leaving the pairing screen");
      return;
    }

    const bool hostsChanged = app::apply_pairing_result(
      state,
      request.address,
      request.httpPort,
      result.success || result.alreadyPaired,
      result.message
    );

    logger.log(result.success || result.alreadyPaired ? logging::LogLevel::info : logging::LogLevel::warning, "pairing", result.message);
    if (hostsChanged) {
      persist_hosts(logger, state);
    }
  }

  void ignore_pairing_result_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update, PairingTaskState *task) {
    if (task == nullptr || update.activatedItemId != "cancel-pairing" || task->thread == nullptr) {
      return;
    }

    task->discardResult.store(true);
    state.statusMessage = "Left the pairing screen. The active network attempt will finish in the background and its result will be ignored.";
    logger.log(logging::LogLevel::info, "pairing", state.statusMessage);
  }

  void test_host_connection_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update) {
    if (!update.connectionTestRequested) {
      return;
    }

    const std::string address = update.connectionTestAddress;
    const uint16_t port = update.connectionTestPort == 0 ? app::DEFAULT_HOST_PORT : update.connectionTestPort;

    if (address.empty()) {
      app::apply_connection_test_result(state, false, "Connection test failed because the host address is invalid");
      logger.log(logging::LogLevel::warning, "hosts", state.statusMessage);
      return;
    }

    std::string resultMessage;
    const bool success = test_tcp_host_connection(address, port, &resultMessage);
    app::apply_connection_test_result(state, success, resultMessage);
    logger.log(success ? logging::LogLevel::info : logging::LogLevel::warning, "hosts", resultMessage);
  }

  void pair_host_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update, PairingTaskState *task) {
    if (!update.pairingRequested || task == nullptr) {
      return;
    }

    if (pairing_task_is_active(*task)) {
      const std::string busyMessage = "A pairing attempt is already running in the background";
      app::apply_pairing_result(state, update.pairingAddress, update.pairingPort, false, busyMessage);
      logger.log(logging::LogLevel::warning, "pairing", busyMessage);
      return;
    }

    reset_pairing_task(task);
    task->request = {
      update.pairingAddress,
      update.pairingPort,
      update.pairingPin,
      "MoonlightXboxOG",
      {},
    };

    task->thread = SDL_CreateThreadWithStackSize(run_pairing_task, "pair-host", PAIRING_THREAD_STACK_SIZE, task);
    if (task->thread == nullptr) {
      reset_pairing_task(task);
      const std::string createThreadError = std::string("Failed to start the background pairing task: ") + SDL_GetError();
      app::apply_pairing_result(state, update.pairingAddress, update.pairingPort, false, createThreadError);
      logger.log(logging::LogLevel::error, "pairing", createThreadError);
      return;
    }

    state.pairingDraft.statusMessage = "Pairing is preparing the client identity and contacting the host in the background. Enter the PIN on the host if prompted and keep this screen open for the result.";
    state.statusMessage = state.pairingDraft.statusMessage;
    logger.log(logging::LogLevel::info, "pairing", "Started background pairing with " + update.pairingAddress + ":" + std::to_string(update.pairingPort));
  }

  bool draw_shell(SDL_Renderer *renderer, SDL_Texture *titleLogoTexture, TTF_Font *titleFont, TTF_Font *bodyFont, TTF_Font *smallFont, const ui::ShellViewModel &viewModel) {
    int screenWidth = 0;
    int screenHeight = 0;
    if (SDL_GetRendererOutputSize(renderer, &screenWidth, &screenHeight) != 0 || screenWidth <= 0 || screenHeight <= 0) {
      return false;
    }

    const int outerMargin = std::max(18, screenHeight / 24);
    const int panelGap = std::max(14, screenWidth / 48);
    const int menuPanelWidth = std::max(228, (screenWidth * 34) / 100);
    const SDL_Rect menuPanel {outerMargin, outerMargin, menuPanelWidth, screenHeight - (outerMargin * 2)};
    const SDL_Rect contentPanel {
      outerMargin + menuPanelWidth + panelGap,
      outerMargin,
      screenWidth - ((outerMargin * 2) + menuPanelWidth + panelGap),
      screenHeight - (outerMargin * 2)
    };

    SDL_SetRenderDrawColor(renderer, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xFF);
    if (SDL_RenderClear(renderer) != 0) {
      return false;
    }

    fill_rect(renderer, menuPanel, PANEL_RED, PANEL_GREEN, PANEL_BLUE);
    fill_rect(renderer, contentPanel, PANEL_RED, PANEL_GREEN, PANEL_BLUE);
    draw_rect(renderer, menuPanel, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
    draw_rect(renderer, contentPanel, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);

    int titleTextX = menuPanel.x + 16;
    const int titleTextY = menuPanel.y + 12;
    int titleTextWidth = menuPanel.w - 32;

    if (titleLogoTexture != nullptr) {
      int logoWidth = 0;
      int logoHeight = 0;
      if (SDL_QueryTexture(titleLogoTexture, nullptr, nullptr, &logoWidth, &logoHeight) == 0 && logoWidth > 0 && logoHeight > 0) {
        const int targetLogoHeight = std::max(32, TTF_FontLineSkip(titleFont));
        const int targetLogoWidth = std::max(32, (logoWidth * targetLogoHeight) / logoHeight);
        const SDL_Rect logoRect {
          menuPanel.x + 16,
          menuPanel.y + 10,
          targetLogoWidth,
          targetLogoHeight,
        };
        if (SDL_RenderCopy(renderer, titleLogoTexture, nullptr, &logoRect) != 0) {
          return false;
        }

        titleTextX = logoRect.x + logoRect.w + 12;
        titleTextWidth = menuPanel.x + menuPanel.w - 16 - titleTextX;
      }
    }

    if (!render_text_line(renderer, titleFont, viewModel.title, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, titleTextX, titleTextY, titleTextWidth)) {
      return false;
    }

    int menuY = menuPanel.y + std::max(70, screenHeight / 6);
    for (const ui::ShellMenuRow &row : viewModel.menuRows) {
      const SDL_Rect rowRect {menuPanel.x + 12, menuY - 6, menuPanel.w - 24, std::max(36, screenHeight / 13)};
      if (row.selected) {
        fill_rect(renderer, rowRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0x55);
        draw_rect(renderer, rowRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
      }

      const SDL_Color rowColor = row.enabled
        ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}
        : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF};
      if (!render_text_line(renderer, bodyFont, row.label, rowColor, menuPanel.x + 24, menuY, menuPanel.w - 48)) {
        return false;
      }

      menuY += rowRect.h + 4;
    }

    int bodyY = contentPanel.y + 16;
    for (const std::string &line : viewModel.bodyLines) {
      int drawnHeight = 0;
      if (!render_text_line(renderer, bodyFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, contentPanel.x + 20, bodyY, contentPanel.w - 40, &drawnHeight)) {
        return false;
      }
      bodyY += drawnHeight + 10;
    }

    int footerY = contentPanel.y + contentPanel.h - std::max(104, screenHeight / 5);
    for (const std::string &line : viewModel.footerLines) {
      int drawnHeight = 0;
      if (!render_text_line(renderer, smallFont, line, {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, contentPanel.x + 20, footerY, contentPanel.w - 40, &drawnHeight)) {
        return false;
      }
      footerY += drawnHeight + 4;
    }

    if (viewModel.overlayVisible) {
      const int overlayX = (screenWidth / 2) + (panelGap / 2);
      const SDL_Rect overlayRect {
        overlayX,
        outerMargin + 28,
        screenWidth - overlayX - outerMargin - 8,
        screenHeight - ((outerMargin + 28) * 2)
      };
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fill_rect(renderer, overlayRect, 0x00, 0x00, 0x00, 0xD8);
      draw_rect(renderer, overlayRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF);

      if (!render_text_line(renderer, bodyFont, viewModel.overlayTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, overlayRect.x + 16, overlayRect.y + 16, overlayRect.w - 32)) {
        return false;
      }

      int overlayY = overlayRect.y + 54;
      for (const std::string &line : viewModel.overlayLines) {
        int drawnHeight = 0;
        if (!render_text_line(renderer, smallFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, overlayRect.x + 16, overlayY, overlayRect.w - 32, &drawnHeight)) {
          return false;
        }
        overlayY += drawnHeight + 6;
      }
    }

    if (viewModel.keypadModalVisible) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      const SDL_Rect scrimRect {0, 0, screenWidth, screenHeight};
      fill_rect(renderer, scrimRect, 0x00, 0x00, 0x00, 0x9C);

      const int modalWidth = std::min(screenWidth - (outerMargin * 2), std::max(360, screenWidth / 2));
      const int buttonGap = 10;
      const int buttonColumnCount = std::max(1, static_cast<int>(viewModel.keypadModalColumnCount));
      const int buttonRowCount = std::max(1, static_cast<int>((viewModel.keypadModalButtons.size() + viewModel.keypadModalColumnCount - 1) / viewModel.keypadModalColumnCount));
      const int preferredButtonHeight = std::max(40, TTF_FontLineSkip(bodyFont) + 16);
      const int modalInnerWidth = modalWidth - 32;
      int modalTextHeight = 0;
      for (const std::string &line : viewModel.keypadModalLines) {
        modalTextHeight += measure_wrapped_text_height(smallFont, line, modalInnerWidth) + 6;
      }
      const int desiredButtonAreaHeight = (buttonRowCount * preferredButtonHeight) + (buttonGap * std::max(0, buttonRowCount - 1));
      const int desiredModalHeight = 52 + modalTextHeight + 16 + desiredButtonAreaHeight + 28;
      const int modalHeight = std::min(screenHeight - (outerMargin * 2), std::max(300, desiredModalHeight));
      const SDL_Rect modalRect {
        (screenWidth - modalWidth) / 2,
        (screenHeight - modalHeight) / 2,
        modalWidth,
        modalHeight,
      };

      fill_rect(renderer, modalRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE, 0xF0);
      draw_rect(renderer, modalRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);

      if (!render_text_line(renderer, bodyFont, viewModel.keypadModalTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, modalRect.x + 16, modalRect.y + 16, modalRect.w - 32)) {
        return false;
      }

      int modalY = modalRect.y + 52;
      for (const std::string &line : viewModel.keypadModalLines) {
        int drawnHeight = 0;
        if (!render_text_line(renderer, smallFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, modalRect.x + 16, modalY, modalRect.w - 32, &drawnHeight)) {
          return false;
        }
        modalY += drawnHeight + 6;
      }

      const int buttonAreaTop = modalY + 16;
      const int buttonAreaHeight = (modalRect.y + modalRect.h) - buttonAreaTop - 24;
      const int buttonWidth = (modalRect.w - 32 - (buttonGap * (buttonColumnCount - 1))) / buttonColumnCount;
      const int buttonHeight = std::max(34, (buttonAreaHeight - (buttonGap * std::max(0, buttonRowCount - 1))) / buttonRowCount);

      for (std::size_t index = 0; index < viewModel.keypadModalButtons.size(); ++index) {
        const int row = static_cast<int>(index / viewModel.keypadModalColumnCount);
        const int column = static_cast<int>(index % viewModel.keypadModalColumnCount);
        const SDL_Rect buttonRect {
          modalRect.x + 16 + (column * (buttonWidth + buttonGap)),
          buttonAreaTop + (row * (buttonHeight + buttonGap)),
          buttonWidth,
          buttonHeight,
        };
        const ui::ShellModalButton &button = viewModel.keypadModalButtons[index];

        if (button.selected) {
          fill_rect(renderer, buttonRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0x55);
        }
        else {
          fill_rect(renderer, buttonRect, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xE0);
        }
        draw_rect(renderer, buttonRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);

        const SDL_Color buttonColor = button.enabled
          ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}
          : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF};
        if (!render_text_centered(renderer, bodyFont, button.label, buttonColor, buttonRect)) {
          return false;
        }
      }
    }

    SDL_RenderPresent(renderer);
    return true;
  }

  void close_controller(SDL_GameController *controller) {
    if (controller != nullptr) {
      SDL_GameControllerClose(controller);
    }
  }

}  // namespace

namespace ui {

  int run_shell(SDL_Window *window, const VIDEO_MODE &videoMode, app::ClientState &state, logging::Logger &logger) {
    if (window == nullptr) {
      return report_shell_failure(logger, "sdl", "Shell requires a valid SDL window");
    }

    if (TTF_Init() != 0) {
      return report_shell_failure(logger, "ttf", std::string("TTF_Init failed: ") + TTF_GetError());
    }

    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == nullptr) {
      IMG_Quit();
      TTF_Quit();
      return report_shell_failure(logger, "sdl", std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }

    const std::string fontPath = build_asset_path("fonts\\vegur-regular.ttf");
    TTF_Font *titleFont = TTF_OpenFont(fontPath.c_str(), std::max(24, videoMode.height / 16));
    TTF_Font *bodyFont = TTF_OpenFont(fontPath.c_str(), std::max(18, videoMode.height / 24));
    TTF_Font *smallFont = TTF_OpenFont(fontPath.c_str(), std::max(14, videoMode.height / 34));
    if (titleFont == nullptr || bodyFont == nullptr || smallFont == nullptr) {
      if (titleFont != nullptr) {
        TTF_CloseFont(titleFont);
      }
      if (bodyFont != nullptr) {
        TTF_CloseFont(bodyFont);
      }
      if (smallFont != nullptr) {
        TTF_CloseFont(smallFont);
      }
      SDL_DestroyRenderer(renderer);
      IMG_Quit();
      TTF_Quit();
      return report_shell_failure(logger, "ttf", std::string("Failed to load shell font from ") + fontPath + ": " + TTF_GetError());
    }

    SDL_Texture *titleLogoTexture = load_texture_from_asset(renderer, "moonlight-logo.svg");

    SDL_GameController *controller = nullptr;
    for (int joystickIndex = 0; joystickIndex < SDL_NumJoysticks(); ++joystickIndex) {
      if (SDL_IsGameController(joystickIndex)) {
        controller = SDL_GameControllerOpen(joystickIndex);
        if (controller != nullptr) {
          logger.log(logging::LogLevel::info, "input", "Opened primary controller");
          break;
        }
      }
    }

    bool running = true;
    bool leftTriggerPressed = false;
    bool rightTriggerPressed = false;
    PairingTaskState pairingTask {};
    reset_pairing_task(&pairingTask);
    logger.log(logging::LogLevel::info, "app", "Entered interactive shell");

    while (running && !state.shouldExit) {
      finish_pairing_task_if_ready(logger, state, &pairingTask);

      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        input::UiCommand command = input::UiCommand::none;

        switch (event.type) {
          case SDL_QUIT:
            state.shouldExit = true;
            break;
          case SDL_CONTROLLERDEVICEADDED:
            if (controller == nullptr && SDL_IsGameController(event.cdevice.which)) {
              controller = SDL_GameControllerOpen(event.cdevice.which);
              if (controller != nullptr) {
                logger.log(logging::LogLevel::info, "input", "Controller connected");
              }
            }
            break;
          case SDL_CONTROLLERDEVICEREMOVED:
            if (controller != nullptr && controller == SDL_GameControllerFromInstanceID(event.cdevice.which)) {
              close_controller(controller);
              controller = nullptr;
              leftTriggerPressed = false;
              rightTriggerPressed = false;
              logger.log(logging::LogLevel::warning, "input", "Controller disconnected");
            }
            break;
          case SDL_CONTROLLERBUTTONDOWN:
            command = translate_controller_button(event.cbutton.button);
            break;
          case SDL_CONTROLLERAXISMOTION:
            command = translate_trigger_axis(event.caxis, &leftTriggerPressed, &rightTriggerPressed);
            break;
          case SDL_KEYDOWN:
            if (event.key.repeat == 0) {
              command = translate_keyboard_key(event.key.keysym.sym, event.key.keysym.mod);
            }
            break;
          default:
            break;
        }

        if (command != input::UiCommand::none) {
          const app::AppUpdate update = app::handle_command(state, command);
          log_app_update(logger, state, update);
          ignore_pairing_result_if_requested(logger, state, update, &pairingTask);
          test_host_connection_if_requested(logger, state, update);
          pair_host_if_requested(logger, state, update, &pairingTask);
          persist_hosts_if_needed(logger, state, update);
        }
      }

      finish_pairing_task_if_ready(logger, state, &pairingTask);

      const ui::ShellViewModel viewModel = build_shell_view_model(state, logger.snapshot(logging::LogLevel::info));
      if (!draw_shell(renderer, titleLogoTexture, titleFont, bodyFont, smallFont, viewModel)) {
        report_shell_failure(logger, "render", std::string("Shell render failed: ") + SDL_GetError());
        running = false;
        break;
      }

      SDL_Delay(16);
    }

    if (pairingTask.thread != nullptr) {
      pairingTask.discardResult.store(true);
      int threadResult = 0;
      SDL_WaitThread(pairingTask.thread, &threadResult);
      (void) threadResult;
    }

    close_controller(controller);
    destroy_texture(titleLogoTexture);
    TTF_CloseFont(smallFont);
    TTF_CloseFont(bodyFont);
    TTF_CloseFont(titleFont);
    SDL_DestroyRenderer(renderer);
    IMG_Quit();
    TTF_Quit();
    return 0;
  }

}  // namespace ui
