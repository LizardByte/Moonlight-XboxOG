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
typedef struct _SDL_GameController SDL_GameController;  ///< SDL controller handle shared with the shell without including SDL platform headers.

namespace streaming {

  /**
   * @brief Run one Xbox streaming session for the selected host app.
   *
   * The session launches or resumes the selected app on the host, starts the
   * Moonlight transport runtime, decodes negotiated video and Opus audio with
   * FFmpeg, forwards controller input, renders the latest decoded frame with a
   * lightweight overlay, and returns once the user stops streaming or the host
   * terminates the session.
   * The shell retains its controller reference until its own resources have been
   * restored; the session acquires a separate reference to that same device. This
   * keeps the Xbox USB input pipe alive across both render-resource transitions.
   *
   * @param window Shared SDL window reused from the shell.
   * @param videoMode Active Xbox video mode.
   * @param settings Active shell settings that control stream resolution, bitrate, frame rate, codec selection, host audio playback, and optional performance metrics in the end-of-stream summary.
   * @param host Selected paired host.
   * @param app Selected host app.
   * @param clientIdentity Paired client identity used for authenticated launch requests.
   * @param preferredController Controller retained by the shell throughout the session, or nullptr to use the first available device.
   * @param returnController Optional output receiving an owned reference to the controller used to dismiss the summary, or nullptr on failure. The shell must preserve or close this reference.
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
    SDL_GameController *preferredController,
    SDL_GameController **returnController,
    std::string *statusMessage
  );

}  // namespace streaming
