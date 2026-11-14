/**
 * @file src/streaming/stats_overlay.cpp
 * @brief Implements streaming statistics text formatting.
 */
// class header include
#include "src/streaming/stats_overlay.h"

// standard includes
#include <algorithm>
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

  /**
   * @brief Format an available measurement without inventing a zero for missing data.
   *
   * @param value Measurement, or a negative unavailable sentinel.
   * @param suffix Unit suffix.
   * @return One-decimal measurement or N/A.
   */
  std::string metric(double value, const char *suffix) {
    if (!(value >= 0) || value > 1000000000.0) {
      return "N/A";
    }
    // nxdk's PDCLib does not consume floating-point printf arguments. Using
    // %.1f%s there makes %s interpret part of the double as a string pointer.
    const auto tenths = static_cast<std::uint64_t>(value * 10.0 + 0.5);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + suffix;
  }

  /**
   * @brief Subtract concurrent cumulative counters defensively.
   *
   * @param value Current count.
   * @param previous Previous count.
   * @return Nonnegative delta.
   */
  std::uint64_t delta(std::uint64_t value, std::uint64_t previous) {
    return value >= previous ? value - previous : 0;
  }

}  // namespace

namespace streaming {

  std::vector<std::string> build_live_stats_overlay_lines(const StreamStatisticsSnapshot &s) {
    return {
      "Video stream: " + std::to_string(s.width) + "x" + std::to_string(s.height) + " @ " + std::to_string(s.fps) + " FPS (" + s.videoCodec + ")",
      "FPS: incoming " + metric(s.incomingFps, "") + " | decoded " + metric(s.decodingFps, "") + " | rendered " + metric(s.renderingFps, ""),
      "Video bitrate: " + metric(s.videoBitrateMbps, " Mbps") + " | Host processing: " + metric(s.hostProcessingMs, " ms"),
      "Network latency: " + (s.roundTripTimeMs < 0 ? std::string("N/A") : std::to_string(s.roundTripTimeMs) + " ms") + " | Variance: " + (s.roundTripVarianceMs < 0 ? std::string("N/A") : std::to_string(s.roundTripVarianceMs) + " ms"),
      "Decode: " + metric(s.decodeTimeMs, " ms") + " | Decoder queue: " + metric(s.queueTimeMs, " ms") + " | Present: " + metric(s.presentationTimeMs, " ms"),
      "Frames dropped: network " + metric(s.networkDropPercent, "%") + " | client " + metric(s.clientDropPercent, "%"),
      "Video queue: " + (s.videoQueueDepth < 0 ? std::string("N/A") : std::to_string(s.videoQueueDepth) + " frames") + " | Audio queue: " + (s.audioQueueDurationMs < 0 ? std::string("N/A") : std::to_string(s.audioQueueDurationMs) + " ms"),
      "Decoder: FFmpeg CPU / " + s.decoderFeatures + " | Presentation: " + s.presentationPath,
    };
  }

  void VideoStatistics::reset() {
    receivedFrames_ = 0;
    networkDroppedFrames_ = 0;
    receivedBytes_ = 0;
    decodedFrames_ = 0;
    renderedFrames_ = 0;
    clientDroppedFrames_ = 0;
    hostSamples_ = 0;
    hostTimeTenthsMs_ = 0;
    decodeSamples_ = 0;
    decodeTimeUs_ = 0;
    queueTimeUs_ = 0;
    presentationTimeUs_ = 0;
    haveFrameNumber_ = false;
    lastFrameNumber_ = 0;
  }

  void VideoStatistics::received(std::uint32_t number, std::uint64_t bytes, std::uint16_t hostTimeTenthsMs) {
    const std::uint32_t distance = number - lastFrameNumber_;
    if (haveFrameNumber_ && distance > 1 && distance < 0x80000000U) {
      networkDroppedFrames_.fetch_add(distance - 1);
    }
    if (!haveFrameNumber_ || (distance > 0 && distance < 0x80000000U)) {
      lastFrameNumber_ = number;
    }
    haveFrameNumber_ = true;
    receivedFrames_.fetch_add(1);
    receivedBytes_.fetch_add(bytes);
    if (hostTimeTenthsMs != 0) {
      hostTimeTenthsMs_.fetch_add(hostTimeTenthsMs);
      hostSamples_.fetch_add(1);
    }
  }

  void VideoStatistics::decoded() {
    decodedFrames_.fetch_add(1);
  }

  void VideoStatistics::decode_time(std::uint64_t durationUs, std::uint64_t queueUs) {
    decodeTimeUs_.fetch_add(durationUs);
    queueTimeUs_.fetch_add(queueUs);
    decodeSamples_.fetch_add(1);
  }

  void VideoStatistics::presented(std::uint64_t durationUs) {
    presentationTimeUs_.fetch_add(durationUs);
    renderedFrames_.fetch_add(1);
  }

  void VideoStatistics::dropped(std::uint64_t count) {
    clientDroppedFrames_.fetch_add(count);
  }

  VideoStatisticsCounters VideoStatistics::counters() const {
    return {receivedFrames_.load(), networkDroppedFrames_.load(), receivedBytes_.load(), decodedFrames_.load(), renderedFrames_.load(), clientDroppedFrames_.load(), hostSamples_.load(), hostTimeTenthsMs_.load(), decodeSamples_.load(), decodeTimeUs_.load(), queueTimeUs_.load(), presentationTimeUs_.load()};
  }

  bool VideoStatisticsSampler::due(std::uint64_t nowUs) const {
    return !initialized_ || nowUs < sampledAtUs_ || nowUs - sampledAtUs_ >= 1000000U;
  }

  void VideoStatisticsSampler::sample(std::uint64_t nowUs, const VideoStatisticsCounters &c) {
    if (!due(nowUs)) {
      return;
    }
    if (initialized_ && nowUs > sampledAtUs_ && c.receivedFrames >= previous_.receivedFrames && c.decodedFrames >= previous_.decodedFrames && c.renderedFrames >= previous_.renderedFrames) {
      const double seconds = static_cast<double>(nowUs - sampledAtUs_) / 1000000.0;
      const auto received = delta(c.receivedFrames, previous_.receivedFrames);
      const auto decoded = delta(c.decodedFrames, previous_.decodedFrames);
      const auto rendered = delta(c.renderedFrames, previous_.renderedFrames);
      const auto missing = delta(c.networkDroppedFrames, previous_.networkDroppedFrames);
      const auto hostSamples = delta(c.hostSamples, previous_.hostSamples);
      const auto decodeSamples = delta(c.decodeSamples, previous_.decodeSamples);
      measured_.incomingFps = received / seconds;
      measured_.decodingFps = decoded / seconds;
      measured_.renderingFps = rendered / seconds;
      measured_.videoBitrateMbps = delta(c.receivedBytes, previous_.receivedBytes) * 8.0 / seconds / 1000000.0;
      measured_.hostProcessingMs = hostSamples ? delta(c.hostTimeTenthsMs, previous_.hostTimeTenthsMs) / (hostSamples * 10.0) : -1;
      measured_.decodeTimeMs = decodeSamples ? delta(c.decodeTimeUs, previous_.decodeTimeUs) / (decodeSamples * 1000.0) : -1;
      measured_.queueTimeMs = decodeSamples ? delta(c.queueTimeUs, previous_.queueTimeUs) / (decodeSamples * 1000.0) : -1;
      measured_.presentationTimeMs = rendered ? delta(c.presentationTimeUs, previous_.presentationTimeUs) / (rendered * 1000.0) : -1;
      measured_.networkDropPercent = received + missing ? missing * 100.0 / (received + missing) : -1;
      measured_.clientDropPercent = received ? std::min(100.0, delta(c.clientDroppedFrames, previous_.clientDroppedFrames) * 100.0 / received) : -1;
    } else {
      measured_ = {};
    }
    previous_ = c;
    sampledAtUs_ = nowUs;
    initialized_ = true;
  }

  void VideoStatisticsSampler::apply(StreamStatisticsSnapshot *s) const {
    if (s == nullptr) {
      return;
    }
    s->incomingFps = measured_.incomingFps;
    s->decodingFps = measured_.decodingFps;
    s->renderingFps = measured_.renderingFps;
    s->videoBitrateMbps = measured_.videoBitrateMbps;
    s->hostProcessingMs = measured_.hostProcessingMs;
    s->decodeTimeMs = measured_.decodeTimeMs;
    s->queueTimeMs = measured_.queueTimeMs;
    s->presentationTimeMs = measured_.presentationTimeMs;
    s->networkDropPercent = measured_.networkDropPercent;
    s->clientDropPercent = measured_.clientDropPercent;
  }

  std::vector<std::string> build_stats_overlay_lines(const StreamStatisticsSnapshot &snapshot) {
    std::vector<std::string> lines;

    {
      std::ostringstream streamLine;
      streamLine << "Stream: " << snapshot.width << "x" << snapshot.height << " @ " << snapshot.fps << " FPS";
      if (!snapshot.videoCodec.empty()) {
        streamLine << " | Decoder: " << snapshot.videoCodec;
      }
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
