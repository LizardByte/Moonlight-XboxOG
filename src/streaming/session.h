/**
 * @file src/streaming/session.h
 * @brief Declares the Xbox streaming session runtime.
 */
#pragma once

// standard includes
#include <string>

// local includes
#include "src/app/client_state.h"
#include "src/app/host_records.h"
#include "src/network/host_pairing.h"
#include "src/startup/video_mode.h"

struct SDL_Window;

namespace streaming {

  /**
   * @brief Run one Xbox streaming session for the selected host app.
   *
   * The session launches or resumes the selected app on the host, starts the
   * Moonlight transport runtime, decodes H.264 video and Opus audio with
   * FFmpeg, forwards controller input, renders the latest decoded frame with a
   * lightweight overlay, and returns once the user stops streaming or the host
   * terminates the session.
   *
   * @param window Shared SDL window reused from the shell.
   * @param videoMode Active Xbox video mode.
   * @param settings Active shell settings that control stream resolution, bitrate, frame rate, host audio playback, and the optional stats overlay.
   * @param host Selected paired host.
   * @param app Selected host app.
   * @param clientIdentity Paired client identity used for authenticated launch requests.
   * @param statusMessage Output message describing the final session result.
   * @return True when the stream session started successfully.
   */
  bool run_stream_session(
    SDL_Window *window,
    const VIDEO_MODE &videoMode,
    const app::SettingsState &settings,
    const app::HostRecord &host,
    const app::HostAppRecord &app,
    const network::PairingIdentity &clientIdentity,
    std::string *statusMessage
  );

}  // namespace streaming
