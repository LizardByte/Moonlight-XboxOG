#pragma once

// standard includes
#include <string>
#include <vector>

namespace streaming {

  /**
   * @brief Snapshot of stream telemetry shown in the on-screen stats overlay.
   */
  struct StreamStatisticsSnapshot {
    int width;  ///< Stream width in pixels.
    int height;  ///< Stream height in pixels.
    int fps;  ///< Current stream frame rate.
    int roundTripTimeMs;  ///< End-to-end measured round-trip latency in milliseconds.
    int hostLatencyMs;  ///< Host-side processing latency in milliseconds.
    int decoderLatencyMs;  ///< Video decoder latency in milliseconds.
    int videoQueueDepth;  ///< Number of video frames buffered locally.
    int audioQueueDurationMs;  ///< Estimated buffered audio duration in milliseconds.
    int videoPacketsReceived;  ///< Total video packets received.
    int videoPacketsRecovered;  ///< Video packets recovered through FEC or retransmission.
    int videoPacketsLost;  ///< Video packets permanently lost.
    bool poorConnection;  ///< True when the host flags the connection as poor.
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
