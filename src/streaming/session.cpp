/**
 * @file src/streaming/session.cpp
 * @brief Implements the Xbox streaming session runtime.
 */
#include "src/streaming/session.h"

#include "src/logging/logger.h"
#include "src/os.h"
#include "src/startup/memory_stats.h"
#include "src/streaming/ffmpeg_stream_backend.h"
#include "src/streaming/stats_overlay.h"
#include "third-party/moonlight-common-c/src/Limelight.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names

#ifdef NXDK
/**
 * @brief Fill an integer with cryptographically strong random bytes on nxdk.
 *
 * @param randomValue Output integer populated with secure random bits.
 * @return Zero on success, or a non-zero platform error code on failure.
 */
extern "C" int rand_s(unsigned int *randomValue);
#endif

namespace {

  constexpr Uint8 BACKGROUND_RED = 0x08;
  constexpr Uint8 BACKGROUND_GREEN = 0x0A;
  constexpr Uint8 BACKGROUND_BLUE = 0x10;
  constexpr Uint8 TEXT_RED = 0xF2;
  constexpr Uint8 TEXT_GREEN = 0xF5;
  constexpr Uint8 TEXT_BLUE = 0xF8;
  constexpr Uint8 ACCENT_RED = 0x00;
  constexpr Uint8 ACCENT_GREEN = 0xF3;
  constexpr Uint8 ACCENT_BLUE = 0xD4;
  constexpr Uint32 STREAM_EXIT_COMBO_HOLD_MILLISECONDS = 900U;
  constexpr Uint32 STREAM_FRAME_DELAY_MILLISECONDS = 16U;
  constexpr int DEFAULT_STREAM_FPS = 20;
  constexpr int DEFAULT_STREAM_BITRATE_KBPS = 1500;
  constexpr int MIN_STREAM_FPS = 15;
  constexpr int MAX_STREAM_FPS = 30;
  constexpr int MIN_STREAM_BITRATE_KBPS = 250;
  constexpr int MAX_STREAM_BITRATE_KBPS = 50000;
  constexpr int DEFAULT_PACKET_SIZE = 1024;
  constexpr std::size_t MAX_CONNECTION_PROTOCOL_MESSAGES = 24U;
  constexpr uint16_t PRESENT_GAMEPAD_MASK = 0x0001;
  constexpr uint32_t CONTROLLER_BUTTON_CAPABILITIES =
    static_cast<uint32_t>(A_FLAG | B_FLAG | X_FLAG | Y_FLAG | UP_FLAG | DOWN_FLAG | LEFT_FLAG | RIGHT_FLAG | LB_FLAG | RB_FLAG | PLAY_FLAG | BACK_FLAG | LS_CLK_FLAG | RS_CLK_FLAG | SPECIAL_FLAG);
  constexpr uint16_t CONTROLLER_CAPABILITIES = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE;

  struct StreamUiResources {
    SDL_Renderer *renderer = nullptr;
    TTF_Font *titleFont = nullptr;
    TTF_Font *bodyFont = nullptr;
    SDL_GameController *controller = nullptr;
    streaming::FfmpegStreamBackend mediaBackend {};
    bool ttfInitialized = false;
  };

  struct ControllerSnapshot {
    int buttonFlags = 0;
    unsigned char leftTrigger = 0;
    unsigned char rightTrigger = 0;
    short leftStickX = 0;
    short leftStickY = 0;
    short rightStickX = 0;
    short rightStickY = 0;
  };

  struct StreamConnectionState {
    std::atomic<int> currentStage = STAGE_NONE;
    std::atomic<int> failedStage = STAGE_NONE;
    std::atomic<int> failedCode = 0;
    std::atomic<int> terminationError = 0;
    std::atomic<int> startResult = -1;
    std::atomic<bool> startCompleted = false;
    std::atomic<bool> connectionStarted = false;
    std::atomic<bool> connectionTerminated = false;
    std::atomic<bool> poorConnection = false;
    std::atomic<bool> stopRequested = false;
    mutable std::mutex protocolLogMutex;
    std::deque<std::string> recentProtocolMessages;
  };

  struct StreamStartContext {
    StreamConnectionState *connectionState = nullptr;
    STREAM_CONFIGURATION streamConfiguration {};
    SERVER_INFORMATION serverInformation {};
    CONNECTION_LISTENER_CALLBACKS connectionCallbacks {};
    DECODER_RENDERER_CALLBACKS videoCallbacks {};
    AUDIO_RENDERER_CALLBACKS audioCallbacks {};
    streaming::FfmpegStreamBackend *mediaBackend = nullptr;
    std::string address;
    std::string reportedAppVersion;
    std::string appVersion;
    std::string gfeVersion;
    std::string rtspSessionUrl;
  };

  struct ResolvedStreamParameters {
    VIDEO_MODE videoMode {};
    int fps = DEFAULT_STREAM_FPS;
    int bitrateKbps = DEFAULT_STREAM_BITRATE_KBPS;
    int packetSize = DEFAULT_PACKET_SIZE;
    int streamingRemotely = STREAM_CFG_AUTO;
  };

  /**
   * @brief Describe the active Moonlight network profile for logging.
   *
   * @param streamingRemotely Moonlight stream locality mode.
   * @return Human-readable profile label.
   */
  const char *describe_streaming_remotely_mode(int streamingRemotely) {
    switch (streamingRemotely) {
      case STREAM_CFG_LOCAL:
        return "local";
      case STREAM_CFG_REMOTE:
        return "remote";
      default:
        return "auto";
    }
  }

  StreamConnectionState *g_active_connection_state = nullptr;

  /**
   * @brief Remove trailing CR and LF characters from a log line.
   *
   * @param message Candidate log line.
   * @return Trimmed log line.
   */
  std::string trim_trailing_line_breaks(std::string message) {
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
      message.pop_back();
    }
    return message;
  }

  /**
   * @brief Return a printable fallback for optional log fields.
   *
   * @param value Candidate string value.
   * @return The original value, or `<none>` when empty.
   */
  std::string printable_log_value(std::string_view value) {
    return value.empty() ? std::string("<none>") : std::string(value);
  }

  /**
   * @brief Return whether a string begins with the requested ASCII prefix.
   *
   * @param text Candidate text.
   * @param prefix Expected prefix.
   * @return True when the prefix matches case-insensitively.
   */
  bool starts_with_ascii_case_insensitive(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
      return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
      const unsigned char textCharacter = static_cast<unsigned char>(text[index]);
      const unsigned char prefixCharacter = static_cast<unsigned char>(prefix[index]);
      if (std::tolower(textCharacter) != std::tolower(prefixCharacter)) {
        return false;
      }
    }

    return true;
  }

  /**
   * @brief Return whether the host metadata indicates Sunshine.
   *
   * @param gfeVersion Reported GFE or Sunshine version string.
   * @return True when the host looks like Sunshine.
   */
  bool is_sunshine_host_version(std::string_view gfeVersion) {
    return starts_with_ascii_case_insensitive(gfeVersion, "Sunshine");
  }

  /**
   * @brief Normalize the host appversion string for Sunshine protocol selection.
   *
   * moonlight-common-c uses the appversion quad to pick RTSP behavior and marks
   * Sunshine hosts by making the fourth component negative. Sunshine also tracks
   * the newer Gen 7 TCP RTSP flow, so older emulated appversion values are
   * normalized to the 7.1.431 generation before the connection starts.
   *
   * @param appVersion Reported appversion from the host launch response.
   * @param gfeVersion Reported GFE or Sunshine version string.
   * @return Appversion string to pass into moonlight-common-c.
   */
  std::string normalize_streaming_app_version(std::string_view appVersion, std::string_view gfeVersion) {
    if (!is_sunshine_host_version(gfeVersion)) {
      return std::string(appVersion);
    }

    int major = 7;
    int minor = 1;
    int patch = 431;
    int build = 0;
    const int parsedFields = std::sscanf(std::string(appVersion).c_str(), "%d.%d.%d.%d", &major, &minor, &patch, &build);
    if (parsedFields < 3) {
      return "7.1.431.-1";
    }

    if (major < 7 || (major == 7 && minor < 1) || (major == 7 && minor == 1 && patch < 431)) {
      major = 7;
      minor = 1;
      patch = 431;
    }

    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch) + ".-1";
  }

  /**
   * @brief Format one `moonlight-common-c` log callback line.
   *
   * @param format `printf`-style format string supplied by the library.
   * @param arguments Variadic argument list matching `format`.
   * @return Formatted log line.
   */
  std::string format_connection_log_message(const char *format, va_list arguments) {
    if (format == nullptr) {
      return {};
    }

    std::array<char, 1024> stackBuffer {};
    va_list argumentsCopy;
    va_copy(argumentsCopy, arguments);
    const int requiredLength = std::vsnprintf(stackBuffer.data(), stackBuffer.size(), format, argumentsCopy);
    va_end(argumentsCopy);
    if (requiredLength < 0) {
      return {};
    }
    if (static_cast<std::size_t>(requiredLength) < stackBuffer.size()) {
      return trim_trailing_line_breaks(std::string(stackBuffer.data(), static_cast<std::size_t>(requiredLength)));
    }

    std::string dynamicBuffer(static_cast<std::size_t>(requiredLength) + 1U, '\0');
    va_copy(argumentsCopy, arguments);
    std::vsnprintf(dynamicBuffer.data(), dynamicBuffer.size(), format, argumentsCopy);
    va_end(argumentsCopy);
    dynamicBuffer.resize(static_cast<std::size_t>(requiredLength));
    return trim_trailing_line_breaks(std::move(dynamicBuffer));
  }

  /**
   * @brief Append one connection-protocol log line to the retained rolling buffer.
   *
   * @param connectionState Connection state owning the retained protocol log buffer.
   * @param message Log line to retain.
   */
  void append_connection_protocol_message(StreamConnectionState *connectionState, std::string message) {
    if (connectionState == nullptr) {
      return;
    }

    message = trim_trailing_line_breaks(std::move(message));
    if (message.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(connectionState->protocolLogMutex);
    if (connectionState->recentProtocolMessages.size() >= MAX_CONNECTION_PROTOCOL_MESSAGES) {
      connectionState->recentProtocolMessages.pop_front();
    }
    connectionState->recentProtocolMessages.push_back(std::move(message));
  }

  /**
   * @brief Return the most recent retained connection-protocol line.
   *
   * @param connectionState Connection state holding retained protocol logs.
   * @return Latest protocol message, or an empty string when none was recorded.
   */
  std::string latest_connection_protocol_message(const StreamConnectionState &connectionState) {
    std::lock_guard<std::mutex> lock(connectionState.protocolLogMutex);
    return connectionState.recentProtocolMessages.empty() ? std::string {} : connectionState.recentProtocolMessages.back();
  }

  /**
   * @brief Reset the mutable connection state before retrying stream startup.
   *
   * @param connectionState State object to reset.
   */
  void reset_connection_state(StreamConnectionState *connectionState) {
    if (connectionState == nullptr) {
      return;
    }

    connectionState->currentStage.store(STAGE_NONE);
    connectionState->failedStage.store(STAGE_NONE);
    connectionState->failedCode.store(0);
    connectionState->terminationError.store(0);
    connectionState->startResult.store(-1);
    connectionState->startCompleted.store(false);
    connectionState->connectionStarted.store(false);
    connectionState->connectionTerminated.store(false);
    connectionState->poorConnection.store(false);
    connectionState->stopRequested.store(false);
    std::lock_guard<std::mutex> lock(connectionState->protocolLogMutex);
    connectionState->recentProtocolMessages.clear();
  }

  std::string build_font_path() {
    return std::string(DATA_PATH) + "assets" + PATH_SEP + "fonts" + PATH_SEP + "vegur-regular.ttf";
  }

  void close_controller(SDL_GameController *controller) {
    if (controller != nullptr) {
      SDL_GameControllerClose(controller);
    }
  }

  void close_stream_ui_resources(StreamUiResources *resources) {
    if (resources == nullptr) {
      return;
    }

    resources->mediaBackend.shutdown();
    close_controller(resources->controller);
    resources->controller = nullptr;
    if (resources->bodyFont != nullptr) {
      TTF_CloseFont(resources->bodyFont);
      resources->bodyFont = nullptr;
    }
    if (resources->titleFont != nullptr) {
      TTF_CloseFont(resources->titleFont);
      resources->titleFont = nullptr;
    }
    if (resources->renderer != nullptr) {
      SDL_DestroyRenderer(resources->renderer);
      resources->renderer = nullptr;
    }
    if (resources->ttfInitialized) {
      TTF_Quit();
      resources->ttfInitialized = false;
    }
  }

  SDL_GameController *open_primary_controller() {
    for (int joystickIndex = 0; joystickIndex < SDL_NumJoysticks(); ++joystickIndex) {
      if (!SDL_IsGameController(joystickIndex)) {
        continue;
      }

      SDL_GameController *controller = SDL_GameControllerOpen(joystickIndex);
      if (controller != nullptr) {
        return controller;
      }
    }

    return nullptr;
  }

  bool initialize_stream_ui_resources(SDL_Window *window, const VIDEO_MODE &videoMode, StreamUiResources *resources, std::string *errorMessage) {
    if (window == nullptr || resources == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = "Streaming requires a valid SDL window";
      }
      return false;
    }

    if (TTF_Init() != 0) {
      if (errorMessage != nullptr) {
        *errorMessage = std::string("TTF_Init failed for the streaming session: ") + TTF_GetError();
      }
      return false;
    }
    resources->ttfInitialized = true;

    resources->renderer = SDL_CreateRenderer(window, -1, 0);
    if (resources->renderer == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = std::string("SDL_CreateRenderer failed for the streaming session: ") + SDL_GetError();
      }
      close_stream_ui_resources(resources);
      return false;
    }

    const std::string fontPath = build_font_path();
    resources->titleFont = TTF_OpenFont(fontPath.c_str(), std::max(22, videoMode.height / 18));
    resources->bodyFont = TTF_OpenFont(fontPath.c_str(), std::max(16, videoMode.height / 28));
    if (resources->titleFont == nullptr || resources->bodyFont == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = std::string("Failed to load the streaming session font from ") + fontPath + ": " + TTF_GetError();
      }
      close_stream_ui_resources(resources);
      return false;
    }

    resources->controller = open_primary_controller();
    return true;
  }

  bool fill_random_bytes(unsigned char *buffer, std::size_t size, std::string *errorMessage) {
#ifdef NXDK
    if (buffer == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = "Streaming could not allocate the random-input key buffer";
      }
      return false;
    }

    std::size_t offset = 0;
    while (offset < size) {
      unsigned int randomValue = 0;
      if (::rand_s(&randomValue) != 0) {
        if (errorMessage != nullptr) {
          *errorMessage = "Failed to generate secure random bytes for the streaming input keys";
        }
        return false;
      }

      const std::size_t chunkSize = std::min(sizeof(randomValue), size - offset);
      std::memcpy(buffer + offset, &randomValue, chunkSize);
      offset += chunkSize;
    }
    return true;
#else
    (void) buffer;
    (void) size;
    if (errorMessage != nullptr) {
      *errorMessage = "The streaming runtime is only supported on the Xbox build";
    }
    return false;
#endif
  }

  std::string hex_encode(const unsigned char *data, std::size_t size) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";

    std::string output;
    output.resize(size * 2U);
    for (std::size_t index = 0; index < size; ++index) {
      output[index * 2U] = HEX_DIGITS[(data[index] >> 4U) & 0x0F];
      output[(index * 2U) + 1U] = HEX_DIGITS[data[index] & 0x0F];
    }
    return output;
  }

  int select_stream_width(const VIDEO_MODE &videoMode) {
    return videoMode.width > 0 ? std::max(320, static_cast<int>(videoMode.width)) : 640;
  }

  int select_stream_height(const VIDEO_MODE &videoMode) {
    return videoMode.height > 0 ? std::max(240, static_cast<int>(videoMode.height)) : 480;
  }

  int select_client_refresh_rate_x100(const VIDEO_MODE &videoMode) {
    return videoMode.refresh > 0 ? static_cast<int>(videoMode.refresh) * 100 : 6000;
  }

  /**
   * @brief Resolve the effective stream resolution to request for the current stream.
   *
   * @param fallbackVideoMode Active shell output mode.
   * @param settings Current shell settings.
   * @return Preferred stream resolution, or fallbackVideoMode when no override exists.
   */
  VIDEO_MODE select_effective_stream_video_mode(const VIDEO_MODE &fallbackVideoMode, const app::SettingsState &settings) {
    return settings.preferredVideoModeSet ? settings.preferredVideoMode : fallbackVideoMode;
  }

  /**
   * @brief Clamp the preferred frame rate into the supported stream range.
   *
   * @param settings Current shell settings.
   * @return Effective frame rate for the next stream.
   */
  int select_stream_fps(const app::SettingsState &settings) {
    return std::clamp(settings.streamFramerate, MIN_STREAM_FPS, MAX_STREAM_FPS);
  }

  /**
   * @brief Clamp the preferred bitrate into a reasonable stream range.
   *
   * @param settings Current shell settings.
   * @return Effective bitrate for the next stream in kilobits per second.
   */
  int select_stream_bitrate_kbps(const app::SettingsState &settings) {
    return std::clamp(settings.streamBitrateKbps, MIN_STREAM_BITRATE_KBPS, MAX_STREAM_BITRATE_KBPS);
  }

  /**
   * @brief Resolve the effective stream parameters for the next session.
   *
   * @param fallbackVideoMode Active shell output mode.
   * @param settings Current shell settings.
   * @return Stream parameters requested through the settings UI.
   */
  ResolvedStreamParameters resolve_stream_parameters(const VIDEO_MODE &fallbackVideoMode, const app::SettingsState &settings) {
    ResolvedStreamParameters resolved {};
    resolved.videoMode = select_effective_stream_video_mode(fallbackVideoMode, settings);
    resolved.fps = select_stream_fps(settings);
    resolved.bitrateKbps = select_stream_bitrate_kbps(settings);

    return resolved;
  }

  void configure_stream_start_context(
    const network::StreamLaunchResult &launchResult,
    const std::array<unsigned char, 16> &remoteInputKey,
    const std::array<unsigned char, 16> &remoteInputIv,
    const VIDEO_MODE &outputVideoMode,
    const ResolvedStreamParameters &streamParameters,
    StreamStartContext *context
  ) {
    if (context == nullptr) {
      return;
    }

    context->address = launchResult.serverInfo.activeAddress;
    if (context->address.empty()) {
      context->address = launchResult.serverInfo.localAddress;
    }
    if (context->address.empty()) {
      context->address = launchResult.serverInfo.remoteAddress;
    }
    context->reportedAppVersion = launchResult.appVersion;
    context->appVersion = normalize_streaming_app_version(launchResult.appVersion, launchResult.gfeVersion);
    context->gfeVersion = launchResult.gfeVersion;
    context->rtspSessionUrl = launchResult.rtspSessionUrl;

    LiInitializeServerInformation(&context->serverInformation);
    context->serverInformation.address = context->address.c_str();
    context->serverInformation.serverInfoAppVersion = context->appVersion.c_str();
    context->serverInformation.serverInfoGfeVersion = context->gfeVersion.empty() ? nullptr : context->gfeVersion.c_str();
    context->serverInformation.rtspSessionUrl = context->rtspSessionUrl.empty() ? nullptr : context->rtspSessionUrl.c_str();
    context->serverInformation.serverCodecModeSupport = launchResult.serverCodecModeSupport;

    LiInitializeStreamConfiguration(&context->streamConfiguration);
    context->streamConfiguration.width = select_stream_width(streamParameters.videoMode);
    context->streamConfiguration.height = select_stream_height(streamParameters.videoMode);
    context->streamConfiguration.fps = streamParameters.fps;
    context->streamConfiguration.bitrate = streamParameters.bitrateKbps;
    context->streamConfiguration.packetSize = streamParameters.packetSize;
    context->streamConfiguration.streamingRemotely = streamParameters.streamingRemotely;
    context->streamConfiguration.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    context->streamConfiguration.supportedVideoFormats = VIDEO_FORMAT_H264;
    context->streamConfiguration.clientRefreshRateX100 = select_client_refresh_rate_x100(outputVideoMode);
    context->streamConfiguration.colorSpace = COLORSPACE_REC_601;
    context->streamConfiguration.colorRange = COLOR_RANGE_LIMITED;
    context->streamConfiguration.encryptionFlags = ENCFLG_NONE;
    std::memcpy(context->streamConfiguration.remoteInputAesKey, remoteInputKey.data(), remoteInputKey.size());
    std::memcpy(context->streamConfiguration.remoteInputAesIv, remoteInputIv.data(), remoteInputIv.size());

    LiInitializeConnectionCallbacks(&context->connectionCallbacks);
  }

  void on_stage_starting(int stage) {
    if (g_active_connection_state != nullptr) {
      g_active_connection_state->currentStage.store(stage);
    }
    logging::debug("stream", std::string("Starting connection stage: ") + LiGetStageName(stage));
  }

  void on_stage_complete(int stage) {
    if (g_active_connection_state != nullptr) {
      g_active_connection_state->currentStage.store(stage);
    }
    logging::debug("stream", std::string("Completed connection stage: ") + LiGetStageName(stage));
  }

  void on_stage_failed(int stage, int errorCode) {
    if (g_active_connection_state != nullptr) {
      g_active_connection_state->failedStage.store(stage);
      g_active_connection_state->failedCode.store(errorCode);
    }
    logging::warn("stream", std::string("Connection stage failed: ") + LiGetStageName(stage) + " (error " + std::to_string(errorCode) + ")");
  }

  void on_connection_started() {
    if (g_active_connection_state != nullptr) {
      g_active_connection_state->connectionStarted.store(true);
    }
    logging::info("stream", "Streaming transport started");
  }

  void on_connection_terminated(int errorCode) {
    if (g_active_connection_state != nullptr) {
      g_active_connection_state->terminationError.store(errorCode);
      g_active_connection_state->connectionTerminated.store(true);
    }
    logging::warn("stream", std::string("Streaming transport terminated with error ") + std::to_string(errorCode));
  }

  void on_connection_status_update(int connectionStatus) {
    if (g_active_connection_state != nullptr) {
      const bool poorConnection = connectionStatus != CONN_STATUS_OKAY;
      if (g_active_connection_state->poorConnection.exchange(poorConnection) != poorConnection) {
        logging::warn("stream", poorConnection ? "Streaming transport reported poor network conditions" : "Streaming transport recovered to okay network conditions");
      }
    }
  }

  void on_log_message(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const std::string message = format_connection_log_message(format, arguments);
    va_end(arguments);
    if (message.empty()) {
      return;
    }

    append_connection_protocol_message(g_active_connection_state, message);
    logging::debug("moonlight", message);
  }

  int run_stream_start_thread(void *context) {
    auto *startContext = static_cast<StreamStartContext *>(context);
    if (startContext == nullptr || startContext->connectionState == nullptr) {
      return -1;
    }

    g_active_connection_state = startContext->connectionState;
    startContext->connectionState->startResult.store(
      LiStartConnection(
        &startContext->serverInformation,
        &startContext->streamConfiguration,
        &startContext->connectionCallbacks,
        &startContext->videoCallbacks,
        &startContext->audioCallbacks,
        startContext->mediaBackend,
        0,
        startContext->mediaBackend,
        0
      )
    );
    startContext->connectionState->startCompleted.store(true);
    return 0;
  }

  bool render_text_line(SDL_Renderer *renderer, TTF_Font *font, const std::string &text, SDL_Color color, int x, int y, int maxWidth, int *drawnHeight = nullptr) {
    if (renderer == nullptr || font == nullptr || maxWidth <= 0) {
      if (drawnHeight != nullptr) {
        *drawnHeight = 0;
      }
      return false;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, static_cast<Uint32>(maxWidth));
    if (surface == nullptr) {
      if (drawnHeight != nullptr) {
        *drawnHeight = 0;
      }
      return false;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
      SDL_FreeSurface(surface);
      if (drawnHeight != nullptr) {
        *drawnHeight = 0;
      }
      return false;
    }

    const SDL_Rect destination {x, y, surface->w, surface->h};
    const int surfaceHeight = surface->h;
    SDL_FreeSurface(surface);
    const bool rendered = SDL_RenderCopy(renderer, texture, nullptr, &destination) == 0;
    SDL_DestroyTexture(texture);
    if (drawnHeight != nullptr) {
      *drawnHeight = surfaceHeight;
    }
    return rendered;
  }

  std::string build_stage_status_line(const StreamConnectionState &connectionState) {
    const int failedStage = connectionState.failedStage.load();
    if (failedStage != STAGE_NONE) {
      return std::string("Connection failed during ") + LiGetStageName(failedStage) + " (error " + std::to_string(connectionState.failedCode.load()) + ")";
    }
    if (connectionState.connectionTerminated.load()) {
      const int terminationError = connectionState.terminationError.load();
      return terminationError == ML_ERROR_GRACEFUL_TERMINATION ? "The host ended the stream gracefully." : "The host ended the stream (error " + std::to_string(terminationError) + ")";
    }
    if (connectionState.connectionStarted.load()) {
      return "Streaming transport is active. Hold Back + Start to stop.";
    }
    return std::string("Connecting: ") + LiGetStageName(connectionState.currentStage.load());
  }

  void pump_stream_events(StreamUiResources *resources) {
    SDL_Event event {};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_CONTROLLERDEVICEADDED && resources != nullptr && resources->controller == nullptr) {
        resources->controller = SDL_GameControllerOpen(event.cdevice.which);
      }
    }
  }

  unsigned char convert_trigger_axis(Sint16 value) {
    const int normalized = std::clamp(static_cast<int>(value), 0, 32767);
    return static_cast<unsigned char>((normalized * 255) / 32767);
  }

  ControllerSnapshot read_controller_snapshot(SDL_GameController *controller) {
    ControllerSnapshot snapshot {};
    if (controller == nullptr) {
      return snapshot;
    }

    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0) {
      snapshot.buttonFlags |= A_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B) != 0) {
      snapshot.buttonFlags |= B_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X) != 0) {
      snapshot.buttonFlags |= X_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y) != 0) {
      snapshot.buttonFlags |= Y_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0) {
      snapshot.buttonFlags |= UP_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0) {
      snapshot.buttonFlags |= DOWN_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0) {
      snapshot.buttonFlags |= LEFT_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0) {
      snapshot.buttonFlags |= RIGHT_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0) {
      snapshot.buttonFlags |= LB_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0) {
      snapshot.buttonFlags |= RB_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START) != 0) {
      snapshot.buttonFlags |= PLAY_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK) != 0) {
      snapshot.buttonFlags |= BACK_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK) != 0) {
      snapshot.buttonFlags |= LS_CLK_FLAG;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK) != 0) {
      snapshot.buttonFlags |= RS_CLK_FLAG;
    }

    snapshot.leftTrigger = convert_trigger_axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    snapshot.rightTrigger = convert_trigger_axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
    snapshot.leftStickX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    snapshot.leftStickY = static_cast<short>(-SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
    snapshot.rightStickX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
    snapshot.rightStickY = static_cast<short>(-SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
    return snapshot;
  }

  bool controller_snapshots_match(const ControllerSnapshot &left, const ControllerSnapshot &right) {
    return left.buttonFlags == right.buttonFlags && left.leftTrigger == right.leftTrigger && left.rightTrigger == right.rightTrigger && left.leftStickX == right.leftStickX && left.leftStickY == right.leftStickY &&
           left.rightStickX == right.rightStickX && left.rightStickY == right.rightStickY;
  }

  void update_stream_exit_combo(SDL_GameController *controller, Uint32 *comboActivatedTick, StreamConnectionState *connectionState) {
    if (comboActivatedTick == nullptr || connectionState == nullptr || controller == nullptr) {
      return;
    }

    const bool backPressed = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK) != 0;
    const bool startPressed = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START) != 0;
    if (!backPressed || !startPressed) {
      *comboActivatedTick = 0U;
      return;
    }

    const Uint32 now = SDL_GetTicks();
    if (*comboActivatedTick == 0U) {
      *comboActivatedTick = now;
      return;
    }
    if (now - *comboActivatedTick >= STREAM_EXIT_COMBO_HOLD_MILLISECONDS) {
      connectionState->stopRequested.store(true);
    }
  }

  void send_controller_state_if_needed(SDL_GameController *controller, bool *arrivalSent, ControllerSnapshot *lastSnapshot) {
    if (controller == nullptr || arrivalSent == nullptr || lastSnapshot == nullptr) {
      return;
    }

    if (!*arrivalSent) {
      LiSendControllerArrivalEvent(0, PRESENT_GAMEPAD_MASK, LI_CTYPE_XBOX, CONTROLLER_BUTTON_CAPABILITIES, CONTROLLER_CAPABILITIES);
      *arrivalSent = true;
    }

    const ControllerSnapshot snapshot = read_controller_snapshot(controller);
    if (controller_snapshots_match(snapshot, *lastSnapshot)) {
      return;
    }

    LiSendControllerEvent(snapshot.buttonFlags, snapshot.leftTrigger, snapshot.rightTrigger, snapshot.leftStickX, snapshot.leftStickY, snapshot.rightStickX, snapshot.rightStickY);
    *lastSnapshot = snapshot;
  }

  streaming::StreamStatisticsSnapshot sample_stream_statistics(const StreamStartContext &context, const StreamConnectionState &connectionState) {
    streaming::StreamStatisticsSnapshot snapshot {
      context.streamConfiguration.width,
      context.streamConfiguration.height,
      context.streamConfiguration.fps,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      connectionState.poorConnection.load(),
    };

    uint32_t estimatedRtt = 0;
    uint32_t estimatedRttVariance = 0;
    if (LiGetEstimatedRttInfo(&estimatedRtt, &estimatedRttVariance)) {
      (void) estimatedRttVariance;
      snapshot.roundTripTimeMs = static_cast<int>(estimatedRtt);
    }

    snapshot.videoQueueDepth = LiGetPendingVideoFrames();
    snapshot.audioQueueDurationMs = LiGetPendingAudioDuration();
    if (const RTP_VIDEO_STATS *videoStats = LiGetRTPVideoStats(); videoStats != nullptr) {
      snapshot.videoPacketsReceived = static_cast<int>(videoStats->packetCountVideo);
      snapshot.videoPacketsRecovered = static_cast<int>(videoStats->packetCountFecRecovered);
      snapshot.videoPacketsLost = static_cast<int>(videoStats->packetCountFecFailed);
    }
    return snapshot;
  }

  bool render_stream_frame(
    const app::HostRecord &host,
    const app::HostAppRecord &app,
    const StreamStartContext &context,
    const StreamConnectionState &connectionState,
    streaming::FfmpegStreamBackend *mediaBackend,
    bool showPerformanceStats,
    StreamUiResources *resources
  ) {
    if (resources == nullptr || resources->renderer == nullptr || resources->titleFont == nullptr || resources->bodyFont == nullptr) {
      return false;
    }

    int screenWidth = 0;
    int screenHeight = 0;
    SDL_GetRendererOutputSize(resources->renderer, &screenWidth, &screenHeight);

    const bool hasDecodedVideo = mediaBackend != nullptr && mediaBackend->has_decoded_video();
    if (hasDecodedVideo) {
      SDL_SetRenderDrawColor(resources->renderer, 0x00, 0x00, 0x00, 0xFF);
    } else {
      SDL_SetRenderDrawColor(resources->renderer, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xFF);
    }
    SDL_RenderClear(resources->renderer);

    const bool renderedVideo = hasDecodedVideo && mediaBackend->render_latest_video_frame(resources->renderer, screenWidth, screenHeight);
    if (renderedVideo && showPerformanceStats) {
      SDL_SetRenderDrawBlendMode(resources->renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(resources->renderer, 0x00, 0x00, 0x00, 0x90);
      const SDL_Rect overlayBackground {18, 18, std::max(1, screenWidth - 36), std::max(1, std::min(screenHeight - 36, 220))};
      SDL_RenderFillRect(resources->renderer, &overlayBackground);
    }

    if (renderedVideo && !showPerformanceStats) {
      SDL_RenderPresent(resources->renderer);
      return true;
    }

    int cursorY = 28;
    int titleHeight = 0;
    render_text_line(resources->renderer, resources->titleFont, renderedVideo ? "Moonlight Streaming" : "Moonlight Streaming", {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, 28, cursorY, std::max(1, screenWidth - 56), &titleHeight);
    cursorY += titleHeight + 8;

    std::vector<std::string> lines = {
      std::string("Host: ") + host.displayName + " (" + context.address + ")",
      std::string("App: ") + app.name + (context.rtspSessionUrl.empty() ? std::string {} : std::string(" | Session URL received")),
      build_stage_status_line(connectionState),
      mediaBackend != nullptr ? mediaBackend->build_overlay_status_line() : std::string("FFmpeg decode backend unavailable"),
      "Hold Back + Start for about one second to stop streaming.",
    };
    if (!renderedVideo) {
      lines.insert(lines.begin() + 2, std::string("Mode: ") + std::to_string(context.streamConfiguration.width) + "x" + std::to_string(context.streamConfiguration.height) + " @ " + std::to_string(context.streamConfiguration.fps) + " FPS | H.264 | Stereo");
      lines.insert(lines.begin() + 4, std::string("Launch mode: ") + (context.serverInformation.rtspSessionUrl != nullptr ? "Session URL supplied by host" : "Default RTSP discovery"));
      lines.insert(lines.begin() + 5, "Waiting for the first decoded video frame and audio output.");
    }

    if (showPerformanceStats) {
      for (const std::string &line : streaming::build_stats_overlay_lines(sample_stream_statistics(context, connectionState))) {
        lines.push_back(line);
      }
    }

    for (const std::string &line : lines) {
      int drawnHeight = 0;
      render_text_line(resources->renderer, resources->bodyFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, 28, cursorY, std::max(1, screenWidth - 56), &drawnHeight);
      cursorY += drawnHeight + 6;
    }

    SDL_RenderPresent(resources->renderer);
    return true;
  }

  std::string describe_start_failure(const StreamConnectionState &connectionState) {
    if (const int failedStage = connectionState.failedStage.load(); failedStage != STAGE_NONE) {
      std::string message = std::string("Failed to start streaming during ") + LiGetStageName(failedStage) + " (error " + std::to_string(connectionState.failedCode.load()) + ")";
      if (const std::string protocolMessage = latest_connection_protocol_message(connectionState); !protocolMessage.empty()) {
        message += ": ";
        message += protocolMessage;
      }
      return message;
    }
    if (const int startResult = connectionState.startResult.load(); startResult != 0) {
      std::string message = std::string("Failed to start streaming transport (library error ") + std::to_string(startResult) + ")";
      if (const std::string protocolMessage = latest_connection_protocol_message(connectionState); !protocolMessage.empty()) {
        message += ": ";
        message += protocolMessage;
      }
      return message;
    }
    return "Failed to start streaming transport";
  }

  std::string describe_session_end(const StreamConnectionState &connectionState, std::string_view appName) {
    if (connectionState.stopRequested.load()) {
      return "Stopped streaming " + std::string(appName);
    }
    if (const int terminationError = connectionState.terminationError.load(); terminationError == ML_ERROR_GRACEFUL_TERMINATION) {
      return std::string("The host closed ") + std::string(appName) + " cleanly";
    }
    if (connectionState.connectionTerminated.load()) {
      return std::string("The stream ended unexpectedly for ") + std::string(appName) + " (error " + std::to_string(connectionState.terminationError.load()) + ")";
    }
    return std::string("Streaming session ended for ") + std::string(appName);
  }

}  // namespace

namespace streaming {

  bool run_stream_session(
    SDL_Window *window,
    const VIDEO_MODE &videoMode,
    const app::SettingsState &settings,
    const app::HostRecord &host,
    const app::HostAppRecord &app,
    const network::PairingIdentity &clientIdentity,
    std::string *statusMessage
  ) {
    StreamUiResources resources {};
    if (std::string initializationError; !initialize_stream_ui_resources(window, videoMode, &resources, &initializationError)) {
      if (statusMessage != nullptr) {
        *statusMessage = initializationError;
      }
      logging::error("stream", initializationError);
      return false;
    }

    startup::log_memory_statistics();

    std::array<unsigned char, 16> remoteInputKey {};
    std::array<unsigned char, 16> remoteInputIv {};
    if (std::string randomError; !fill_random_bytes(remoteInputKey.data(), remoteInputKey.size(), &randomError) || !fill_random_bytes(remoteInputIv.data(), remoteInputIv.size(), &randomError)) {
      close_stream_ui_resources(&resources);
      if (statusMessage != nullptr) {
        *statusMessage = randomError;
      }
      logging::error("stream", randomError);
      return false;
    }

    const std::string hostAddress = host.activeAddress.empty() ? host.address : host.activeAddress;
    const uint16_t httpPort = host.resolvedHttpPort == 0U ? app::effective_host_port(host.port) : host.resolvedHttpPort;

    network::StreamLaunchResult launchResult {};
    network::StreamLaunchConfiguration launchConfiguration {};
    const ResolvedStreamParameters streamParameters = resolve_stream_parameters(videoMode, settings);
    launchConfiguration.appId = app.id;
    launchConfiguration.width = select_stream_width(streamParameters.videoMode);
    launchConfiguration.height = select_stream_height(streamParameters.videoMode);
    launchConfiguration.fps = streamParameters.fps;
    launchConfiguration.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    launchConfiguration.playAudioOnPc = settings.playAudioOnPc;
    launchConfiguration.clientRefreshRateX100 = select_client_refresh_rate_x100(videoMode);
    launchConfiguration.remoteInputAesKeyHex = hex_encode(remoteInputKey.data(), remoteInputKey.size());
    launchConfiguration.remoteInputAesIvHex = hex_encode(remoteInputIv.data(), remoteInputIv.size());

    std::string launchError;
    if (!network::launch_or_resume_stream(hostAddress, httpPort, clientIdentity, launchConfiguration, &launchResult, &launchError)) {
      close_stream_ui_resources(&resources);
      if (statusMessage != nullptr) {
        *statusMessage = launchError;
      }
      logging::error("stream", launchError);
      return false;
    }

    StreamConnectionState connectionState {};
    StreamStartContext startContext {};
    startContext.connectionState = &connectionState;
    startContext.mediaBackend = &resources.mediaBackend;
    configure_stream_start_context(launchResult, remoteInputKey, remoteInputIv, videoMode, streamParameters, &startContext);
    if (startContext.appVersion != startContext.reportedAppVersion) {
      logging::info(
        "stream",
        std::string("Normalized host appversion from ") + startContext.reportedAppVersion + " to " + startContext.appVersion + " for " + (startContext.gfeVersion.empty() ? std::string("the current host") : startContext.gfeVersion)
      );
    }
    logging::info(
      "stream",
      std::string("Starting stream setup for ") + app.name + " on " + host.displayName + " at " + startContext.address + " | appversion " + startContext.appVersion + (startContext.rtspSessionUrl.empty() ? std::string(" | default RTSP discovery") : std::string(" | host session URL supplied"))
    );
    logging::info(
      "stream",
      std::string("Requested stream configuration | resolution=") + std::to_string(startContext.streamConfiguration.width) + "x" + std::to_string(startContext.streamConfiguration.height) + " | fps=" + std::to_string(startContext.streamConfiguration.fps) + " | bitrate=" + std::to_string(startContext.streamConfiguration.bitrate) + " kbps | packetSize=" + std::to_string(startContext.streamConfiguration.packetSize) + " | networkProfile=" + describe_streaming_remotely_mode(startContext.streamConfiguration.streamingRemotely) + " | clientRefreshX100=" + std::to_string(startContext.streamConfiguration.clientRefreshRateX100)
    );
    logging::debug(
      "stream",
      std::string("Stream connection metadata | active=") + printable_log_value(startContext.serverInformation.address == nullptr ? std::string_view {} : std::string_view {startContext.serverInformation.address}) +
        " | local=" + printable_log_value(launchResult.serverInfo.localAddress) +
        " | remote=" + printable_log_value(launchResult.serverInfo.remoteAddress) +
        " | gfeVersion=" + printable_log_value(startContext.gfeVersion) +
        " | sessionUrl=" + printable_log_value(startContext.rtspSessionUrl)
    );
    resources.mediaBackend.initialize_callbacks(&startContext.videoCallbacks, &startContext.audioCallbacks);
    startContext.connectionCallbacks.stageStarting = on_stage_starting;
    startContext.connectionCallbacks.stageComplete = on_stage_complete;
    startContext.connectionCallbacks.stageFailed = on_stage_failed;
    startContext.connectionCallbacks.connectionStarted = on_connection_started;
    startContext.connectionCallbacks.connectionTerminated = on_connection_terminated;
    startContext.connectionCallbacks.connectionStatusUpdate = on_connection_status_update;
    startContext.connectionCallbacks.logMessage = on_log_message;

    const auto run_start_attempt = [&]() -> bool {
      SDL_Thread *startThread = SDL_CreateThread(run_stream_start_thread, "start-stream", &startContext);
      if (startThread == nullptr) {
        const std::string createThreadError = std::string("Failed to start the streaming transport thread: ") + SDL_GetError();
        close_stream_ui_resources(&resources);
        if (statusMessage != nullptr) {
          *statusMessage = createThreadError;
        }
        logging::error("stream", createThreadError);
        return false;
      }

      Uint32 exitComboActivatedTick = 0U;
      while (!connectionState.startCompleted.load() && !connectionState.stopRequested.load()) {
        pump_stream_events(&resources);
        update_stream_exit_combo(resources.controller, &exitComboActivatedTick, &connectionState);
        render_stream_frame(host, app, startContext, connectionState, &resources.mediaBackend, true, &resources);
        if (connectionState.stopRequested.load()) {
          LiInterruptConnection();
        }
        SDL_Delay(STREAM_FRAME_DELAY_MILLISECONDS);
      }

      int threadResult = 0;
      SDL_WaitThread(startThread, &threadResult);
      (void) threadResult;
      return true;
    };

    if (!run_start_attempt()) {
      return false;
    }

    bool rtspFallbackAttempted = false;
    if (!connectionState.stopRequested.load() && !connectionState.connectionStarted.load() && connectionState.failedStage.load() == STAGE_RTSP_HANDSHAKE && !startContext.rtspSessionUrl.empty()) {
      rtspFallbackAttempted = true;
      logging::warn("stream", "RTSP handshake failed with the host-supplied session URL; retrying with default RTSP discovery");
      resources.mediaBackend.shutdown();
      startContext.rtspSessionUrl.clear();
      startContext.serverInformation.rtspSessionUrl = nullptr;
      reset_connection_state(&connectionState);
      if (!run_start_attempt()) {
        return false;
      }
    }

    if (connectionState.startResult.load() != 0 || !connectionState.connectionStarted.load()) {
      std::string failureMessage = connectionState.stopRequested.load() ? std::string("Cancelled the stream start for ") + app.name : describe_start_failure(connectionState);
      if (rtspFallbackAttempted && !connectionState.stopRequested.load()) {
        failureMessage += " after retrying default RTSP discovery";
      }
      g_active_connection_state = nullptr;
      close_stream_ui_resources(&resources);
      if (statusMessage != nullptr) {
        *statusMessage = failureMessage;
      }
      logging::warn("stream", failureMessage);
      return false;
    }

    bool controllerArrivalSent = false;
    ControllerSnapshot lastControllerSnapshot {};
    Uint32 exitComboActivatedTick = 0U;
    logging::info("stream", std::string(launchResult.resumedSession ? "Resumed stream for " : "Launched stream for ") + app.name + " on " + host.displayName);
    while (!connectionState.connectionTerminated.load() && !connectionState.stopRequested.load()) {
      pump_stream_events(&resources);
      update_stream_exit_combo(resources.controller, &exitComboActivatedTick, &connectionState);
      send_controller_state_if_needed(resources.controller, &controllerArrivalSent, &lastControllerSnapshot);
      render_stream_frame(host, app, startContext, connectionState, &resources.mediaBackend, settings.showPerformanceStats, &resources);
      SDL_Delay(STREAM_FRAME_DELAY_MILLISECONDS);
    }

    LiStopConnection();
    g_active_connection_state = nullptr;

    const std::string finalMessage = describe_session_end(connectionState, app.name);
    logging::info("stream", finalMessage);
    startup::log_memory_statistics();
    close_stream_ui_resources(&resources);
    if (statusMessage != nullptr) {
      *statusMessage = finalMessage;
    }
    return true;
  }

}  // namespace streaming
