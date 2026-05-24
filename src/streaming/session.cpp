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

/**
 * @brief Return the platform monotonic clock value in microseconds.
 *
 * @return Microseconds from the platform timer used by moonlight-common-c.
 */
extern "C" std::uint64_t PltGetMicroseconds(void);

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
  constexpr Uint32 STREAM_PRESENT_POLL_MILLISECONDS = 2U;
  constexpr Uint32 STREAM_INPUT_POLL_MILLISECONDS = 4U;
  constexpr Uint32 STREAM_END_STATS_POLL_MILLISECONDS = 50U;
  constexpr Uint32 STREAM_VIDEO_IDLE_IDR_MILLISECONDS = 1500U;
  constexpr Uint32 STREAM_VIDEO_IDLE_IDR_COOLDOWN_MILLISECONDS = 2000U;
  constexpr int DEFAULT_STREAM_FPS = 30;
  constexpr int DEFAULT_STREAM_BITRATE_KBPS = 1000;
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
    mutable std::mutex controllerMutex;
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

  struct StreamInputThreadState {
    StreamUiResources *resources = nullptr;
    StreamConnectionState *connectionState = nullptr;
    std::atomic<bool> stopRequested = false;
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

  struct StreamStartAttemptContext {
    const app::HostRecord &host;
    const app::HostAppRecord &app;
    StreamStartContext &startContext;
    StreamConnectionState &connectionState;
    StreamUiResources &resources;
    std::string *statusMessage = nullptr;
  };

  struct ActiveStreamLoopContext {
    const app::SettingsState &settings;
    const app::HostRecord &host;
    const app::HostAppRecord &app;
    StreamStartContext &startContext;
    StreamConnectionState &connectionState;
    StreamUiResources &resources;
  };

  struct TextLineLayout {
    SDL_Color color {};
    int x = 0;
    int y = 0;
    int maxWidth = 0;
    int *drawnHeight = nullptr;
  };

  struct ResolvedStreamParameters {
    VIDEO_MODE videoMode {};
    int fps = DEFAULT_STREAM_FPS;
    int bitrateKbps = DEFAULT_STREAM_BITRATE_KBPS;
    int packetSize = DEFAULT_PACKET_SIZE;
    int streamingRemotely = STREAM_CFG_AUTO;
  };

  void assign_status_message(std::string *statusMessage, std::string_view message) {
    if (statusMessage != nullptr) {
      statusMessage->assign(message.data(), message.size());
    }
  }

  class ScopedThreadPriority {
  public:
    explicit ScopedThreadPriority(SDL_ThreadPriority priority) {
      active_ = SDL_SetThreadPriority(priority) == 0;
      if (!active_) {
        logging::debug("stream", std::string("SDL_SetThreadPriority failed for stream render thread: ") + SDL_GetError());
      }
    }

    ~ScopedThreadPriority() {
      if (active_) {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL);
      }
    }

    ScopedThreadPriority(const ScopedThreadPriority &) = delete;
    ScopedThreadPriority &operator=(const ScopedThreadPriority &) = delete;

  private:
    bool active_ = false;
  };

  bool video_modes_match(const VIDEO_MODE &left, const VIDEO_MODE &right) {
    return left.width == right.width && left.height == right.height && left.bpp == right.bpp && left.refresh == right.refresh;
  }

  VIDEO_MODE select_stream_presentation_video_mode(const VIDEO_MODE &fallbackVideoMode, const VIDEO_MODE &streamVideoMode) {
    if (streamVideoMode.width > 0 && streamVideoMode.height > 0) {
      return streamVideoMode;
    }

    return fallbackVideoMode;
  }

  class ScopedStreamVideoMode {
  public:
    ScopedStreamVideoMode(SDL_Window *window, const VIDEO_MODE &originalVideoMode, const VIDEO_MODE &requestedVideoMode):
        window_(window),
        originalVideoMode_(originalVideoMode),
        activeVideoMode_(originalVideoMode) {
      if (requestedVideoMode.width <= 0 || requestedVideoMode.height <= 0 || video_modes_match(requestedVideoMode, originalVideoMode)) {
        return;
      }

#ifdef NXDK
      if (!XVideoSetMode(requestedVideoMode.width, requestedVideoMode.height, requestedVideoMode.bpp, requestedVideoMode.refresh)) {
        logging::warn(
          "stream",
          "Failed to switch Xbox video mode for stream presentation to " + std::to_string(requestedVideoMode.width) + "x" + std::to_string(requestedVideoMode.height) + " @ " +
            std::to_string(requestedVideoMode.refresh) + " Hz; using current output mode"
        );
        return;
      }

      activeVideoMode_ = requestedVideoMode;
      changed_ = true;
      if (window_ != nullptr) {
        SDL_SetWindowSize(window_, activeVideoMode_.width, activeVideoMode_.height);
      }
      logging::info("stream", "Switched Xbox video mode for stream presentation to " + std::to_string(activeVideoMode_.width) + "x" + std::to_string(activeVideoMode_.height));
#else
      (void) requestedVideoMode;
#endif
    }

    ~ScopedStreamVideoMode() {
#ifdef NXDK
      if (changed_) {
        XVideoSetMode(originalVideoMode_.width, originalVideoMode_.height, originalVideoMode_.bpp, originalVideoMode_.refresh);
        if (window_ != nullptr) {
          SDL_SetWindowSize(window_, originalVideoMode_.width, originalVideoMode_.height);
        }
      }
#endif
    }

    ScopedStreamVideoMode(const ScopedStreamVideoMode &) = delete;
    ScopedStreamVideoMode &operator=(const ScopedStreamVideoMode &) = delete;

    const VIDEO_MODE &active_video_mode() const {
      return activeVideoMode_;
    }

  private:
    SDL_Window *window_ = nullptr;
    VIDEO_MODE originalVideoMode_ {};
    VIDEO_MODE activeVideoMode_ {};
    bool changed_ = false;
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

  /**
   * @brief Return the connection state currently receiving moonlight-common-c callbacks.
   *
   * @return Mutable callback connection-state slot.
   */
  StreamConnectionState *&active_connection_state() {
    static StreamConnectionState *connectionState = nullptr;
    return connectionState;
  }

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
      const auto textCharacter = static_cast<unsigned char>(text[index]);
      const auto prefixCharacter = static_cast<unsigned char>(prefix[index]);
      if (std::tolower(textCharacter) != std::tolower(prefixCharacter)) {
        return false;
      }
    }

    return true;
  }

  bool contains_ascii_case_insensitive(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
      return true;
    }
    if (text.size() < needle.size()) {
      return false;
    }

    for (std::size_t offset = 0; offset <= text.size() - needle.size(); ++offset) {
      if (starts_with_ascii_case_insensitive(text.substr(offset), needle)) {
        return true;
      }
    }

    return false;
  }

  bool is_high_volume_connection_log(std::string_view text) {
    static constexpr std::array<std::string_view, 22> HIGH_VOLUME_PREFIXES {
      "Audio packet queue overflow",
      "Control message took over 10 ms",
      "Depacketizer detected corrupt frame",
      "Failed to decrypt audio packet",
      "Failed to decrypt video packet",
      "IDR frame request sent",
      "Input queue reached maximum size limit",
      "Invalidate reference frame request sent",
      "Invalid last payload length",
      "Leaving speculative RFI mode",
      "Network dropped ",
      "Next post-invalidation frame is:",
      "Received OOS audio data",
      "Recovered ",
      "Requesting IDR frame on behalf of DR",
      "Sending RFI request for unrecoverable frame",
      "Sending speculative RFI request",
      "Unable to recover audio data",
      "Unrecoverable frame ",
      "Video decode unit queue overflow",
      "Waiting for IDR frame",
      "Waiting for RFI frame",
    };

    return std::any_of(HIGH_VOLUME_PREFIXES.begin(), HIGH_VOLUME_PREFIXES.end(), [text](std::string_view prefix) {
      return starts_with_ascii_case_insensitive(text, prefix);
    });
  }

  logging::LogLevel connection_log_level(std::string_view message) {
    if (
      starts_with_ascii_case_insensitive(message, "WARNING:") ||
      starts_with_ascii_case_insensitive(message, "Failed") ||
      starts_with_ascii_case_insensitive(message, "Invalid") ||
      starts_with_ascii_case_insensitive(message, "No ") ||
      contains_ascii_case_insensitive(message, " failed")
    ) {
      return logging::LogLevel::warning;
    }

    return logging::LogLevel::debug;
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
    if (const int parsedFields = std::sscanf(std::string(appVersion).c_str(), "%d.%d.%d.%d", &major, &minor, &patch, &build); parsedFields < 3) {
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
    const int requiredLength = std::vsnprintf(stackBuffer.data(), stackBuffer.size(), format, argumentsCopy);  // NOSONAR(cpp:S5281) format is supplied by moonlight-common-c.
    va_end(argumentsCopy);
    if (requiredLength < 0) {
      return {};
    }
    if (static_cast<std::size_t>(requiredLength) < stackBuffer.size()) {
      return trim_trailing_line_breaks(std::string(stackBuffer.data(), static_cast<std::size_t>(requiredLength)));
    }

    std::string dynamicBuffer(static_cast<std::size_t>(requiredLength) + 1U, '\0');
    va_copy(argumentsCopy, arguments);
    std::vsnprintf(dynamicBuffer.data(), dynamicBuffer.size(), format, argumentsCopy);  // NOSONAR(cpp:S5281) format is supplied by moonlight-common-c.
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

    std::scoped_lock lock(connectionState->protocolLogMutex);
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
    std::scoped_lock lock(connectionState.protocolLogMutex);
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
    std::scoped_lock lock(connectionState->protocolLogMutex);
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
    {
      std::scoped_lock lock(resources->controllerMutex);
      close_controller(resources->controller);
      resources->controller = nullptr;
    }
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
    static constexpr std::string_view HEX_DIGITS = "0123456789abcdef";

    std::string output;
    output.resize(size * 2U);
    for (std::size_t index = 0; index < size; ++index) {
      output[index * 2U] = HEX_DIGITS[(data[index] >> 4U) & 0x0F];  // NOSONAR(cpp:S6022) hex encoding is byte-oriented by design.
      output[(index * 2U) + 1U] = HEX_DIGITS[data[index] & 0x0F];  // NOSONAR(cpp:S6022) hex encoding is byte-oriented by design.
    }
    return output;
  }

  int select_stream_width(const VIDEO_MODE &videoMode) {
    return videoMode.width > 0 ? std::max(320, videoMode.width) : 640;
  }

  int select_stream_height(const VIDEO_MODE &videoMode) {
    return videoMode.height > 0 ? std::max(240, videoMode.height) : 480;
  }

  int select_client_refresh_rate_x100(const VIDEO_MODE &videoMode) {
    return videoMode.refresh > 0 ? videoMode.refresh * 100 : 6000;
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
    if (StreamConnectionState *connectionState = active_connection_state(); connectionState != nullptr) {
      connectionState->currentStage.store(stage);
    }
    logging::debug("stream", std::string("Starting connection stage: ") + LiGetStageName(stage));
  }

  void on_stage_complete(int stage) {
    if (StreamConnectionState *connectionState = active_connection_state(); connectionState != nullptr) {
      connectionState->currentStage.store(stage);
    }
    logging::debug("stream", std::string("Completed connection stage: ") + LiGetStageName(stage));
  }

  void on_stage_failed(int stage, int errorCode) {
    if (StreamConnectionState *connectionState = active_connection_state(); connectionState != nullptr) {
      connectionState->failedStage.store(stage);
      connectionState->failedCode.store(errorCode);
    }
    logging::warn("stream", std::string("Connection stage failed: ") + LiGetStageName(stage) + " (error " + std::to_string(errorCode) + ")");
  }

  void on_connection_started() {
    if (StreamConnectionState *connectionState = active_connection_state(); connectionState != nullptr) {
      connectionState->connectionStarted.store(true);
    }
    logging::info("stream", "Streaming transport started");
  }

  void on_connection_terminated(int errorCode) {
    if (StreamConnectionState *connectionState = active_connection_state(); connectionState != nullptr) {
      connectionState->terminationError.store(errorCode);
      connectionState->connectionTerminated.store(true);
    }
    logging::warn("stream", std::string("Streaming transport terminated with error ") + std::to_string(errorCode));
  }

  void on_connection_status_update(int connectionStatus) {
    if (StreamConnectionState *connectionState = active_connection_state(); connectionState != nullptr) {
      const bool poorConnection = connectionStatus != CONN_STATUS_OKAY;
      if (connectionState->poorConnection.exchange(poorConnection) != poorConnection) {
        logging::warn("stream", poorConnection ? "Streaming transport reported poor network conditions" : "Streaming transport recovered to okay network conditions");
      }
    }
  }

  void on_log_message(const char *format, ...) {  // NOSONAR(cpp:S923) moonlight-common-c log callback requires printf-style ellipsis.
    if (format == nullptr || is_high_volume_connection_log(format)) {
      return;
    }

    va_list arguments;
    va_start(arguments, format);
    const std::string message = format_connection_log_message(format, arguments);
    va_end(arguments);
    if (message.empty() || is_high_volume_connection_log(message)) {
      return;
    }

    append_connection_protocol_message(active_connection_state(), message);
    logging::log(connection_log_level(message), "moonlight", message);
  }

  int run_stream_start_thread(void *context) {
    auto *startContext = static_cast<StreamStartContext *>(context);
    if (startContext == nullptr || startContext->connectionState == nullptr) {
      return -1;
    }

    active_connection_state() = startContext->connectionState;
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

  bool render_text_line(SDL_Renderer *renderer, TTF_Font *font, const std::string &text, const TextLineLayout &layout) {
    if (renderer == nullptr || font == nullptr || layout.maxWidth <= 0) {
      if (layout.drawnHeight != nullptr) {
        *layout.drawnHeight = 0;
      }
      return false;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), layout.color, static_cast<Uint32>(layout.maxWidth));
    if (surface == nullptr) {
      if (layout.drawnHeight != nullptr) {
        *layout.drawnHeight = 0;
      }
      return false;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
      SDL_FreeSurface(surface);
      if (layout.drawnHeight != nullptr) {
        *layout.drawnHeight = 0;
      }
      return false;
    }

    const SDL_Rect destination {layout.x, layout.y, surface->w, surface->h};
    const int surfaceHeight = surface->h;
    SDL_FreeSurface(surface);
    const bool rendered = SDL_RenderCopy(renderer, texture, nullptr, &destination) == 0;
    SDL_DestroyTexture(texture);
    if (layout.drawnHeight != nullptr) {
      *layout.drawnHeight = surfaceHeight;
    }
    return rendered;
  }

  void render_text_lines(SDL_Renderer *renderer, TTF_Font *font, const std::vector<std::string> &lines, int screenWidth, int *cursorY) {
    if (cursorY == nullptr) {
      return;
    }

    for (const std::string &line : lines) {
      int drawnHeight = 0;
      render_text_line(renderer, font, line, {{TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, 28, *cursorY, std::max(1, screenWidth - 56), &drawnHeight});
      *cursorY += drawnHeight + 6;
    }
  }

  std::string build_stage_status_line(const StreamConnectionState &connectionState) {
    if (const int failedStage = connectionState.failedStage.load(); failedStage != STAGE_NONE) {
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

  void open_controller_if_needed(StreamUiResources *resources, int deviceIndex) {
    if (resources == nullptr) {
      return;
    }

    std::scoped_lock lock(resources->controllerMutex);
    if (resources->controller == nullptr) {
      resources->controller = SDL_GameControllerOpen(deviceIndex);
    }
  }

  void close_controller_if_removed(StreamUiResources *resources, int joystickInstanceId) {
    if (resources == nullptr) {
      return;
    }

    std::scoped_lock lock(resources->controllerMutex);
    if (resources->controller == nullptr) {
      return;
    }

    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(resources->controller);
    if (joystick != nullptr && SDL_JoystickInstanceID(joystick) == joystickInstanceId) {
      close_controller(resources->controller);
      resources->controller = nullptr;
    }
  }

  SDL_ControllerDeviceEvent copy_controller_device_event(const SDL_Event &event) {
    SDL_ControllerDeviceEvent controllerEvent {};
    std::memcpy(&controllerEvent, &event, sizeof(controllerEvent));
    return controllerEvent;
  }

  void pump_stream_events(StreamUiResources *resources) {
    SDL_Event event {};
    while (SDL_PollEvent(&event) != 0) {
      if (resources == nullptr) {
        continue;
      }

      if (event.type == SDL_CONTROLLERDEVICEADDED) {
        const SDL_ControllerDeviceEvent controllerEvent = copy_controller_device_event(event);
        open_controller_if_needed(resources, controllerEvent.which);
      } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        const SDL_ControllerDeviceEvent controllerEvent = copy_controller_device_event(event);
        close_controller_if_removed(resources, controllerEvent.which);
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
    if (const bool startPressed = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START) != 0; !backPressed || !startPressed) {
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

  void update_stream_exit_combo_from_snapshot(const ControllerSnapshot &snapshot, bool controllerPresent, Uint32 *comboActivatedTick, StreamConnectionState *connectionState) {
    if (comboActivatedTick == nullptr || connectionState == nullptr || !controllerPresent) {
      if (comboActivatedTick != nullptr) {
        *comboActivatedTick = 0U;
      }
      return;
    }

    const bool backPressed = (snapshot.buttonFlags & BACK_FLAG) != 0;
    if (const bool startPressed = (snapshot.buttonFlags & PLAY_FLAG) != 0; !backPressed || !startPressed) {
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

  ControllerSnapshot read_controller_snapshot(StreamUiResources *resources, bool *controllerPresent) {
    if (controllerPresent != nullptr) {
      *controllerPresent = false;
    }
    if (resources == nullptr) {
      return {};
    }

    std::scoped_lock lock(resources->controllerMutex);
    if (resources->controller == nullptr) {
      return {};
    }

    if (controllerPresent != nullptr) {
      *controllerPresent = true;
    }
    return read_controller_snapshot(resources->controller);
  }

  void send_controller_snapshot_if_needed(const ControllerSnapshot &snapshot, bool controllerPresent, bool *arrivalSent, ControllerSnapshot *lastSnapshot) {
    if (!controllerPresent || arrivalSent == nullptr || lastSnapshot == nullptr) {
      return;
    }

    if (!*arrivalSent) {
      LiSendControllerArrivalEvent(0, PRESENT_GAMEPAD_MASK, LI_CTYPE_XBOX, CONTROLLER_BUTTON_CAPABILITIES, CONTROLLER_CAPABILITIES);
      *arrivalSent = true;
    }

    if (controller_snapshots_match(snapshot, *lastSnapshot)) {
      return;
    }

    LiSendControllerEvent(snapshot.buttonFlags, snapshot.leftTrigger, snapshot.rightTrigger, snapshot.leftStickX, snapshot.leftStickY, snapshot.rightStickX, snapshot.rightStickY);
    *lastSnapshot = snapshot;
  }

  int run_stream_input_thread(void *context) {
    auto *inputThreadState = static_cast<StreamInputThreadState *>(context);
    if (inputThreadState == nullptr || inputThreadState->resources == nullptr || inputThreadState->connectionState == nullptr) {
      return -1;
    }

    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) != 0) {
      logging::debug("stream", std::string("SDL_SetThreadPriority failed for stream input: ") + SDL_GetError());
    }

    bool controllerArrivalSent = false;
    ControllerSnapshot lastControllerSnapshot {};
    Uint32 exitComboActivatedTick = 0U;

    while (!inputThreadState->stopRequested.load() && !inputThreadState->connectionState->connectionTerminated.load() && !inputThreadState->connectionState->stopRequested.load()) {
      bool controllerPresent = false;
      const ControllerSnapshot snapshot = read_controller_snapshot(inputThreadState->resources, &controllerPresent);
      update_stream_exit_combo_from_snapshot(snapshot, controllerPresent, &exitComboActivatedTick, inputThreadState->connectionState);
      send_controller_snapshot_if_needed(snapshot, controllerPresent, &controllerArrivalSent, &lastControllerSnapshot);
      SDL_Delay(STREAM_INPUT_POLL_MILLISECONDS);
    }

    return 0;
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
    if (uint32_t estimatedRttVariance = 0; LiGetEstimatedRttInfo(&estimatedRtt, &estimatedRttVariance)) {
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

  void request_idle_video_refresh_if_needed(const streaming::FfmpegStreamBackend *mediaBackend, Uint32 *lastRequestTicks) {
    if (mediaBackend == nullptr || lastRequestTicks == nullptr || !mediaBackend->has_decoded_video()) {
      return;
    }

    const std::uint64_t idleMilliseconds = mediaBackend->milliseconds_since_last_decoded_video_frame(PltGetMicroseconds());
    if (idleMilliseconds < STREAM_VIDEO_IDLE_IDR_MILLISECONDS) {
      return;
    }
    const Uint32 nowTicks = SDL_GetTicks();
    if (*lastRequestTicks != 0U && nowTicks - *lastRequestTicks < STREAM_VIDEO_IDLE_IDR_COOLDOWN_MILLISECONDS) {
      return;
    }

    LiRequestIdrFrame();
    *lastRequestTicks = nowTicks;
    logging::debug("stream", std::string("Requested IDR frame after ") + std::to_string(idleMilliseconds) + " ms without decoded video");
  }

  bool render_stream_frame(
    const app::HostRecord &host,
    const app::HostAppRecord &app,
    const StreamStartContext &context,
    const StreamConnectionState &connectionState,
    streaming::FfmpegStreamBackend *mediaBackend,
    StreamUiResources *resources
  ) {
    if (resources == nullptr || resources->renderer == nullptr || resources->titleFont == nullptr || resources->bodyFont == nullptr) {
      return false;
    }

    int screenWidth = 0;
    int screenHeight = 0;
    SDL_GetRendererOutputSize(resources->renderer, &screenWidth, &screenHeight);

    const bool hasDecodedVideo = mediaBackend != nullptr && mediaBackend->has_decoded_video();
#ifdef NXDK
    if (hasDecodedVideo && mediaBackend->render_latest_video_frame(resources->renderer, screenWidth, screenHeight, true)) {
      return true;
    }
#endif
    if (hasDecodedVideo) {
      SDL_SetRenderDrawColor(resources->renderer, 0x00, 0x00, 0x00, 0xFF);
    } else {
      SDL_SetRenderDrawColor(resources->renderer, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xFF);
    }
    SDL_RenderClear(resources->renderer);

    const bool renderedVideo = hasDecodedVideo && mediaBackend->render_latest_video_frame(resources->renderer, screenWidth, screenHeight, false);
    if (renderedVideo) {
      SDL_RenderPresent(resources->renderer);
      return true;
    }

    int cursorY = 28;
    int titleHeight = 0;
    render_text_line(resources->renderer, resources->titleFont, "Moonlight Streaming", {{ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, 28, cursorY, std::max(1, screenWidth - 56), &titleHeight});
    cursorY += titleHeight + 8;

    std::vector<std::string> lines = {
      std::string("Host: ") + host.displayName + " (" + context.address + ")",
      std::string("App: ") + app.name + (context.rtspSessionUrl.empty() ? std::string {} : std::string(" | Session URL received")),
      build_stage_status_line(connectionState),
      mediaBackend != nullptr ? mediaBackend->build_overlay_status_line() : std::string("FFmpeg decode backend unavailable"),
      "Hold Back + Start for about one second to stop streaming.",
    };
    if (!renderedVideo) {
      lines.emplace(lines.begin() + 2, std::string("Mode: ") + std::to_string(context.streamConfiguration.width) + "x" + std::to_string(context.streamConfiguration.height) + " @ " + std::to_string(context.streamConfiguration.fps) + " FPS | H.264 | Stereo");
      lines.emplace(lines.begin() + 4, std::string("Launch mode: ") + (context.serverInformation.rtspSessionUrl != nullptr ? "Session URL supplied by host" : "Default RTSP discovery"));
      lines.emplace(lines.begin() + 5, "Waiting for the first decoded video frame and audio output.");
    }

    render_text_lines(resources->renderer, resources->bodyFont, lines, screenWidth, &cursorY);

    SDL_RenderPresent(resources->renderer);
    return true;
  }

  bool render_stream_end_statistics_frame(
    const app::HostRecord &host,
    const app::HostAppRecord &app,
    const StreamStartContext &context,
    const streaming::StreamStatisticsSnapshot &statisticsSnapshot,
    const std::string &finalMessage,
    StreamUiResources *resources
  ) {
    if (resources == nullptr || resources->renderer == nullptr || resources->titleFont == nullptr || resources->bodyFont == nullptr) {
      return false;
    }

    int screenWidth = 0;
    SDL_GetRendererOutputSize(resources->renderer, &screenWidth, nullptr);

    SDL_SetRenderDrawColor(resources->renderer, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xFF);
    SDL_RenderClear(resources->renderer);

    int cursorY = 28;
    int titleHeight = 0;
    render_text_line(resources->renderer, resources->titleFont, "Stream Summary", {{ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, 28, cursorY, std::max(1, screenWidth - 56), &titleHeight});
    cursorY += titleHeight + 8;

    std::vector<std::string> lines = {
      finalMessage,
      std::string("Host: ") + host.displayName + " (" + context.address + ")",
      std::string("App: ") + app.name,
    };
    for (const std::string &line : streaming::build_stats_overlay_lines(statisticsSnapshot)) {
      lines.push_back(line);
    }
    lines.emplace_back("Press any button to return.");

    render_text_lines(resources->renderer, resources->bodyFont, lines, screenWidth, &cursorY);

    SDL_RenderPresent(resources->renderer);
    return true;
  }

  bool should_close_stream_end_statistics(const SDL_Event &event, StreamUiResources *resources) {
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
      const SDL_ControllerDeviceEvent controllerEvent = copy_controller_device_event(event);
      open_controller_if_needed(resources, controllerEvent.which);
      return false;
    }
    if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
      const SDL_ControllerDeviceEvent controllerEvent = copy_controller_device_event(event);
      close_controller_if_removed(resources, controllerEvent.which);
      return false;
    }
    return event.type == SDL_QUIT || event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN;
  }

  bool controller_snapshot_has_button_press(const ControllerSnapshot &snapshot) {
    return snapshot.buttonFlags != 0 || snapshot.leftTrigger > 0 || snapshot.rightTrigger > 0;
  }

  void show_stream_end_statistics(
    const app::HostRecord &host,
    const app::HostAppRecord &app,
    const StreamStartContext &context,
    const streaming::StreamStatisticsSnapshot &statisticsSnapshot,
    const std::string &finalMessage,
    StreamUiResources *resources
  ) {
    if (!render_stream_end_statistics_frame(host, app, context, statisticsSnapshot, finalMessage, resources)) {
      return;
    }

    bool initialControllerPresent = false;
    const ControllerSnapshot initialSnapshot = read_controller_snapshot(resources, &initialControllerPresent);
    bool controllerReadyForPress = !initialControllerPresent || !controller_snapshot_has_button_press(initialSnapshot);

    for (;;) {
      SDL_Event event {};
      while (SDL_PollEvent(&event) != 0) {
        if (should_close_stream_end_statistics(event, resources)) {
          return;
        }
      }
      bool controllerPresent = false;
      if (const ControllerSnapshot snapshot = read_controller_snapshot(resources, &controllerPresent); !controllerPresent || !controller_snapshot_has_button_press(snapshot)) {
        controllerReadyForPress = true;
      } else if (controllerReadyForPress) {
        return;
      }
      SDL_Delay(STREAM_END_STATS_POLL_MILLISECONDS);
    }
  }

  bool run_stream_start_attempt(const StreamStartAttemptContext &attempt) {
    if (SDL_Thread *startThread = SDL_CreateThread(run_stream_start_thread, "start-stream", &attempt.startContext); startThread != nullptr) {
      Uint32 exitComboActivatedTick = 0U;
      while (!attempt.connectionState.startCompleted.load() && !attempt.connectionState.stopRequested.load()) {
        pump_stream_events(&attempt.resources);
        update_stream_exit_combo(attempt.resources.controller, &exitComboActivatedTick, &attempt.connectionState);
        render_stream_frame(attempt.host, attempt.app, attempt.startContext, attempt.connectionState, &attempt.resources.mediaBackend, &attempt.resources);
        if (attempt.connectionState.stopRequested.load()) {
          LiInterruptConnection();
        }
        SDL_Delay(STREAM_FRAME_DELAY_MILLISECONDS);
      }

      int threadResult = 0;
      SDL_WaitThread(startThread, &threadResult);
      (void) threadResult;
      return true;
    }

    const std::string createThreadError = std::string("Failed to start the streaming transport thread: ") + SDL_GetError();
    close_stream_ui_resources(&attempt.resources);
    assign_status_message(attempt.statusMessage, createThreadError);
    logging::error("stream", createThreadError);
    return false;
  }

  bool should_retry_stream_without_rtsp_session_url(const StreamStartContext &startContext, const StreamConnectionState &connectionState) {
    return !connectionState.stopRequested.load() && !connectionState.connectionStarted.load() && connectionState.failedStage.load() == STAGE_RTSP_HANDSHAKE && !startContext.rtspSessionUrl.empty();
  }

  bool run_stream_start_with_rtsp_fallback(const StreamStartAttemptContext &attempt, bool *rtspFallbackAttempted) {
    if (rtspFallbackAttempted != nullptr) {
      *rtspFallbackAttempted = false;
    }
    if (!run_stream_start_attempt(attempt)) {
      return false;
    }
    if (!should_retry_stream_without_rtsp_session_url(attempt.startContext, attempt.connectionState)) {
      return true;
    }

    if (rtspFallbackAttempted != nullptr) {
      *rtspFallbackAttempted = true;
    }
    logging::warn("stream", "RTSP handshake failed with the host-supplied session URL; retrying with default RTSP discovery");
    attempt.resources.mediaBackend.shutdown();
    attempt.startContext.rtspSessionUrl.clear();
    attempt.startContext.serverInformation.rtspSessionUrl = nullptr;
    reset_connection_state(&attempt.connectionState);
    return run_stream_start_attempt(attempt);
  }

  void poll_fallback_controller_if_needed(
    const SDL_Thread *inputThread,
    StreamUiResources *resources,
    StreamConnectionState *connectionState,
    Uint32 *exitComboActivatedTick,
    bool *controllerArrivalSent,
    ControllerSnapshot *lastControllerSnapshot
  ) {
    if (inputThread != nullptr) {
      return;
    }

    bool controllerPresent = false;
    const ControllerSnapshot snapshot = read_controller_snapshot(resources, &controllerPresent);
    update_stream_exit_combo_from_snapshot(snapshot, controllerPresent, exitComboActivatedTick, connectionState);
    send_controller_snapshot_if_needed(snapshot, controllerPresent, controllerArrivalSent, lastControllerSnapshot);
  }

  void render_active_stream_frame_if_needed(const ActiveStreamLoopContext &loopContext, Uint32 *lastIdleVideoIdrRequestTick) {
    const bool hasDecodedVideo = loopContext.resources.mediaBackend.has_decoded_video();
    if (const bool shouldRenderFrame = !hasDecodedVideo || loopContext.resources.mediaBackend.has_unrendered_video_frame(); shouldRenderFrame) {
      render_stream_frame(
        loopContext.host,
        loopContext.app,
        loopContext.startContext,
        loopContext.connectionState,
        &loopContext.resources.mediaBackend,
        &loopContext.resources
      );
    }
    request_idle_video_refresh_if_needed(&loopContext.resources.mediaBackend, lastIdleVideoIdrRequestTick);
    SDL_Delay(hasDecodedVideo ? STREAM_PRESENT_POLL_MILLISECONDS : STREAM_FRAME_DELAY_MILLISECONDS);
  }

  void wait_for_stream_input_thread(SDL_Thread *inputThread, StreamInputThreadState *inputThreadState) {
    if (inputThreadState != nullptr) {
      inputThreadState->stopRequested.store(true);
    }
    if (inputThread == nullptr) {
      return;
    }

    int inputThreadResult = 0;
    SDL_WaitThread(inputThread, &inputThreadResult);
    (void) inputThreadResult;
  }

  void run_active_stream_loop(const ActiveStreamLoopContext &loopContext) {
    StreamInputThreadState inputThreadState {};
    inputThreadState.resources = &loopContext.resources;
    inputThreadState.connectionState = &loopContext.connectionState;
    SDL_Thread *inputThread = SDL_CreateThread(run_stream_input_thread, "stream-input", &inputThreadState);
    if (inputThread == nullptr) {
      logging::warn("stream", std::string("Failed to start the stream input thread; polling controller from the render loop: ") + SDL_GetError());
    }

    bool fallbackControllerArrivalSent = false;
    ControllerSnapshot fallbackLastControllerSnapshot {};
    Uint32 fallbackExitComboActivatedTick = 0U;
    Uint32 lastIdleVideoIdrRequestTick = 0U;
    while (!loopContext.connectionState.connectionTerminated.load() && !loopContext.connectionState.stopRequested.load()) {
      pump_stream_events(&loopContext.resources);
      poll_fallback_controller_if_needed(inputThread, &loopContext.resources, &loopContext.connectionState, &fallbackExitComboActivatedTick, &fallbackControllerArrivalSent, &fallbackLastControllerSnapshot);
      render_active_stream_frame_if_needed(loopContext, &lastIdleVideoIdrRequestTick);
    }

    wait_for_stream_input_thread(inputThread, &inputThreadState);
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
    const ResolvedStreamParameters streamParameters = resolve_stream_parameters(videoMode, settings);
    const VIDEO_MODE streamPresentationVideoMode = select_stream_presentation_video_mode(videoMode, streamParameters.videoMode);
    ScopedStreamVideoMode scopedStreamVideoMode(window, videoMode, streamPresentationVideoMode);
    const VIDEO_MODE &activeStreamVideoMode = scopedStreamVideoMode.active_video_mode();

    StreamUiResources resources {};
    if (std::string initializationError; !initialize_stream_ui_resources(window, activeStreamVideoMode, &resources, &initializationError)) {
      assign_status_message(statusMessage, initializationError);
      logging::error("stream", initializationError);
      return false;
    }
    ScopedThreadPriority streamThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    startup::log_memory_statistics();

    std::array<unsigned char, 16> remoteInputKey {};
    std::array<unsigned char, 16> remoteInputIv {};
    if (std::string randomError; !fill_random_bytes(remoteInputKey.data(), remoteInputKey.size(), &randomError) || !fill_random_bytes(remoteInputIv.data(), remoteInputIv.size(), &randomError)) {
      close_stream_ui_resources(&resources);
      assign_status_message(statusMessage, randomError);
      logging::error("stream", randomError);
      return false;
    }

    const std::string hostAddress = host.activeAddress.empty() ? host.address : host.activeAddress;
    const uint16_t httpPort = host.resolvedHttpPort == 0U ? app::effective_host_port(host.port) : host.resolvedHttpPort;

    network::StreamLaunchResult launchResult {};
    network::StreamLaunchConfiguration launchConfiguration {};
    launchConfiguration.appId = app.id;
    launchConfiguration.width = select_stream_width(streamParameters.videoMode);
    launchConfiguration.height = select_stream_height(streamParameters.videoMode);
    launchConfiguration.fps = streamParameters.fps;
    launchConfiguration.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    launchConfiguration.playAudioOnPc = settings.playAudioOnPc;
    launchConfiguration.clientRefreshRateX100 = select_client_refresh_rate_x100(activeStreamVideoMode);
    launchConfiguration.remoteInputAesKeyHex = hex_encode(remoteInputKey.data(), remoteInputKey.size());
    launchConfiguration.remoteInputAesIvHex = hex_encode(remoteInputIv.data(), remoteInputIv.size());

    if (std::string launchError; !network::launch_or_resume_stream(hostAddress, httpPort, clientIdentity, launchConfiguration, &launchResult, &launchError)) {
      close_stream_ui_resources(&resources);
      assign_status_message(statusMessage, launchError);
      logging::error("stream", launchError);
      return false;
    }

    StreamConnectionState connectionState {};
    StreamStartContext startContext {};
    startContext.connectionState = &connectionState;
    startContext.mediaBackend = &resources.mediaBackend;
    configure_stream_start_context(launchResult, remoteInputKey, remoteInputIv, activeStreamVideoMode, streamParameters, &startContext);
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
    resources.mediaBackend.set_audio_playback_enabled(settings.playAudioOnXbox);
    logging::info("stream", std::string("Xbox audio playback ") + (settings.playAudioOnXbox ? "enabled" : "disabled for lower decode latency"));
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

    bool rtspFallbackAttempted = false;
    if (const StreamStartAttemptContext startAttempt {host, app, startContext, connectionState, resources, statusMessage}; !run_stream_start_with_rtsp_fallback(startAttempt, &rtspFallbackAttempted)) {
      return false;
    }

    if (connectionState.startResult.load() != 0 || !connectionState.connectionStarted.load()) {
      std::string failureMessage = connectionState.stopRequested.load() ? std::string("Cancelled the stream start for ") + app.name : describe_start_failure(connectionState);
      if (rtspFallbackAttempted && !connectionState.stopRequested.load()) {
        failureMessage += " after retrying default RTSP discovery";
      }
      active_connection_state() = nullptr;
      close_stream_ui_resources(&resources);
      assign_status_message(statusMessage, failureMessage);
      logging::warn("stream", failureMessage);
      return false;
    }

    logging::info("stream", std::string(launchResult.resumedSession ? "Resumed stream for " : "Launched stream for ") + app.name + " on " + host.displayName);
    run_active_stream_loop({settings, host, app, startContext, connectionState, resources});

    const streaming::StreamStatisticsSnapshot finalStatistics = sample_stream_statistics(startContext, connectionState);
    LiStopConnection();
    active_connection_state() = nullptr;

    const std::string finalMessage = describe_session_end(connectionState, app.name);
    logging::info("stream", finalMessage);
    startup::log_memory_statistics();
    if (settings.showPerformanceStats) {
      show_stream_end_statistics(host, app, startContext, finalStatistics, finalMessage, &resources);
    }
    close_stream_ui_resources(&resources);
    assign_status_message(statusMessage, finalMessage);
    return true;
  }

}  // namespace streaming
