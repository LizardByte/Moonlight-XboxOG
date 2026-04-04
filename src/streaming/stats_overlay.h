#pragma once

// standard includes
#include <string>
#include <vector>

namespace streaming {

  /**
   * @brief Snapshot of stream telemetry shown in the on-screen stats overlay.
   */
  struct StreamStatisticsSnapshot {
    int width;
    int height;
    int fps;
    int roundTripTimeMs;
    int hostLatencyMs;
    int decoderLatencyMs;
    int videoQueueDepth;
    int audioQueueDurationMs;
    int videoPacketsReceived;
    int videoPacketsRecovered;
    int videoPacketsLost;
    bool poorConnection;
  };

  /**
   * @brief Build text rows for the streaming statistics overlay.
   *
   * Negative metric values are treated as unavailable and omitted from the
   * corresponding row.
   *
   * @param snapshot Telemetry values to display.
   * @return Text rows ready for rendering.
   */
  std::vector<std::string> build_stats_overlay_lines(const StreamStatisticsSnapshot &snapshot);

}  // namespace streaming
