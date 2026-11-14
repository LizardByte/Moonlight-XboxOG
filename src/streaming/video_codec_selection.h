/**
 * @file src/streaming/video_codec_selection.h
 * @brief Declares the codec capabilities associated with client preferences.
 */
#pragma once

#include "src/app/client_state.h"

namespace streaming {
  /**
   * @brief Advertise available decoders or strictly request the selected codec.
   *
   * @param selection Codec preference selected by the user.
   * @return Moonlight video format mask used for negotiation.
   */
  int supported_video_formats(app::VideoCodecSelection selection);
}  // namespace streaming
