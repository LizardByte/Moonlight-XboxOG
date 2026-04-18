/**
 * @file src/streaming/stats_overlay.cpp
 * @brief Implements the streaming statistics overlay.
 */
// class header include
#include "src/streaming/stats_overlay.h"

// standard includes
#include <sstream>
#include <utility>
#include <vector>

namespace {

  bool has_metric(int value) {
    return value >= 0;
  }

  std::string join_segments(const std::vector<std::string> &segments) {
    std::ostringstream stream;

    for (std::size_t index = 0; index < segments.size(); ++index) {
      if (index > 0) {
        stream << " | ";
      }
      stream << segments[index];
    }

    return stream.str();
  }

}  // namespace

namespace streaming {

  std::vector<std::string> build_stats_overlay_lines(const StreamStatisticsSnapshot &snapshot) {
    std::vector<std::string> lines;

    {
      std::ostringstream streamLine;
      streamLine << "Stream: " << snapshot.width << "x" << snapshot.height << " @ " << snapshot.fps << " FPS";
      lines.push_back(streamLine.str());
    }

    std::vector<std::string> latencySegments;
    if (has_metric(snapshot.roundTripTimeMs)) {
      latencySegments.push_back("RTT " + std::to_string(snapshot.roundTripTimeMs) + " ms");
    }
    if (has_metric(snapshot.hostLatencyMs)) {
      latencySegments.push_back("Host " + std::to_string(snapshot.hostLatencyMs) + " ms");
    }
    if (has_metric(snapshot.decoderLatencyMs)) {
      latencySegments.push_back("Decode " + std::to_string(snapshot.decoderLatencyMs) + " ms");
    }
    if (!latencySegments.empty()) {
      lines.push_back("Latency: " + join_segments(latencySegments));
    }

    std::vector<std::string> queueSegments;
    if (has_metric(snapshot.videoQueueDepth)) {
      queueSegments.push_back("Video " + std::to_string(snapshot.videoQueueDepth) + " frames");
    }
    if (has_metric(snapshot.audioQueueDurationMs)) {
      queueSegments.push_back("Audio " + std::to_string(snapshot.audioQueueDurationMs) + " ms");
    }
    if (!queueSegments.empty()) {
      lines.push_back("Queues: " + join_segments(queueSegments));
    }

    std::vector<std::string> packetSegments;
    if (has_metric(snapshot.videoPacketsReceived)) {
      packetSegments.push_back(std::to_string(snapshot.videoPacketsReceived) + " rx");
    }
    if (has_metric(snapshot.videoPacketsRecovered)) {
      packetSegments.push_back(std::to_string(snapshot.videoPacketsRecovered) + " recovered");
    }
    if (has_metric(snapshot.videoPacketsLost)) {
      packetSegments.push_back(std::to_string(snapshot.videoPacketsLost) + " lost");
    }
    if (!packetSegments.empty()) {
      lines.push_back("Video packets: " + join_segments(packetSegments));
    }

    lines.push_back(std::string("Connection: ") + (snapshot.poorConnection ? "Poor" : "Okay"));
    return lines;
  }

}  // namespace streaming
