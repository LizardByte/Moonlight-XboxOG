/**
 * @file src/streaming/stats_overlay.h
 * @brief Declares the streaming statistics overlay.
 */
#pragma once

// standard includes
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace streaming {

  /**
   * @brief Snapshot of stream telemetry shown in diagnostics or stream summaries.
   *
   * The requested stream rate is separate from measured delivery, decode and
   * presentation rates. Queue timing covers the wait before decoding, rather
   * than post-decode frame pacing. Presentation timing measures CPU preparation
   * and submission, including a scanout wait when used, rather than physical
   * display latency. Negative measurements are unavailable; the live panel
   * displays N/A until an interval or optional host metadata is available.
   */
  struct StreamStatisticsSnapshot {
    int width;  ///< Stream width in pixels.
    int height;  ///< Stream height in pixels.
    int fps;  ///< Requested stream frame rate, not the measured rendering rate.
    int roundTripTimeMs;  ///< Estimated network round-trip latency in milliseconds.
    int hostLatencyMs;  ///< Host-side processing latency in milliseconds.
    int decoderLatencyMs;  ///< Video decoder latency in milliseconds.
    int videoQueueDepth;  ///< Number of video frames buffered locally.
    int audioQueueDurationMs;  ///< Estimated buffered audio duration in milliseconds.
    int videoPacketsReceived;  ///< Total video packets received.
    int videoPacketsRecovered;  ///< Video packets recovered through FEC or retransmission.
    int videoPacketsLost;  ///< Video packets permanently lost.
    bool poorConnection;  ///< True when the host flags the connection as poor.
    std::string videoCodec;  ///< Human-readable video codec used for the stream.
    std::vector<std::string> accelerationSummary;  ///< Retained acceleration paths and fallback reasons.
    bool showPerformanceMetrics = true;  ///< Optional detailed telemetry; acceleration diagnostics always remain visible.
    double incomingFps = -1;  ///< Completed frames delivered by the network per second.
    double decodingFps = -1;  ///< Decoded pictures published per second.
    double renderingFps = -1;  ///< New pictures submitted for presentation per second.
    int roundTripVarianceMs = -1;  ///< Estimated variation of network round-trip latency.
    double hostProcessingMs = -1;  ///< Mean host processing time; absent or zero metadata is excluded.
    double decodeTimeMs = -1;  ///< Mean CPU decode and frame publication time.
    double queueTimeMs = -1;  ///< Mean wait between completed frame assembly and starting decode.
    double presentationTimeMs = -1;  ///< Mean CPU time preparing and submitting a new picture.
    double videoBitrateMbps = -1;  ///< Received compressed video payload rate, excluding transport headers and FEC.
    double networkDropPercent = -1;  ///< Missing frame sequence numbers as a percentage of expected frames.
    double clientDropPercent = -1;  ///< Queue, decode and superseded-picture discards as a percentage of received frames.
    std::string presentationPath;  ///< Active GPU, software or waiting presentation path.
    std::string decoderFeatures;  ///< Enabled FFmpeg CPU optimizations.
  };

  /**
   * @brief Build text rows for streaming statistics displays.
   *
   * Negative metric values are treated as unavailable and omitted from the
   * corresponding row.
   *
   * @param snapshot Telemetry values to display.
   * @return Text rows ready for rendering.
   */
  std::vector<std::string> build_stats_overlay_lines(const StreamStatisticsSnapshot &snapshot);

  /**
   * @brief Build the live performance panel using measured rates and explicit unavailable values.
   *
   * @param snapshot Current stream measurements.
   * @return Compact rows shared by GPU and software presentation.
   */
  std::vector<std::string> build_live_stats_overlay_lines(const StreamStatisticsSnapshot &snapshot);

  /**
   * @brief Cumulative video counters sampled without locking the decoder or renderer.
   */
  struct VideoStatisticsCounters {
    std::uint64_t receivedFrames = 0;  ///< Fully assembled network frames observed by the client.
    std::uint64_t networkDroppedFrames = 0;  ///< Missing frame sequence numbers.
    std::uint64_t receivedBytes = 0;  ///< Compressed payload bytes, including locally discarded frames.
    std::uint64_t decodedFrames = 0;  ///< Published decoded pictures.
    std::uint64_t renderedFrames = 0;  ///< New pictures submitted for display.
    std::uint64_t clientDroppedFrames = 0;  ///< Queue, decode and presentation discards.
    std::uint64_t hostSamples = 0;  ///< Frames with host processing metadata.
    std::uint64_t hostTimeTenthsMs = 0;  ///< Summed host processing time in tenths of milliseconds.
    std::uint64_t decodeSamples = 0;  ///< Timed decode operations.
    std::uint64_t decodeTimeUs = 0;  ///< Summed decode time in microseconds.
    std::uint64_t queueTimeUs = 0;  ///< Summed pre-decode queue time in microseconds.
    std::uint64_t presentationTimeUs = 0;  ///< Summed presentation preparation and submission time.
  };

  /**
   * @brief Collect cumulative telemetry from one decoder worker and one render thread.
   *
   * Atomic accesses use sequentially consistent ordering. Reading each counter
   * independently still allows a snapshot to span multiple worker updates.
   */
  class VideoStatistics {
  public:
    /**
     * @brief Reset counters while both workers are stopped.
     */
    void reset();
    /**
     * @brief Observe every retrieved frame, before any client-side queue dropping.
     * @param number Protocol frame sequence number.
     * @param bytes Compressed video payload size.
     * @param hostTimeTenthsMs Optional host processing time; zero is unavailable.
     */
    void received(std::uint32_t number, std::uint64_t bytes, std::uint16_t hostTimeTenthsMs);
    /**
     * @brief Count a successfully published decoded picture.
     */
    void decoded();
    /**
     * @brief Record one decode attempt, including frame publication.
     * @param durationUs CPU processing duration.
     * @param queueUs Time spent waiting for decoding.
     */
    void decode_time(std::uint64_t durationUs, std::uint64_t queueUs);
    /**
     * @brief Record one newly presented picture.
     *
     * @param durationUs Preparation and submission duration.
     */
    void presented(std::uint64_t durationUs);
    /**
     * @brief Count frames intentionally discarded locally.
     *
     * @param count Number of frames.
     */
    void dropped(std::uint64_t count);
    /**
     * @brief Read cumulative counters without blocking either video worker.
     *
     * @return Approximate concurrent snapshot.
     */
    VideoStatisticsCounters counters() const;

  private:
    std::atomic<std::uint64_t> receivedFrames_ {0};  ///< Network delivery count.
    std::atomic<std::uint64_t> networkDroppedFrames_ {0};  ///< Missing sequence count.
    std::atomic<std::uint64_t> receivedBytes_ {0};  ///< Delivered payload bytes.
    std::atomic<std::uint64_t> decodedFrames_ {0};  ///< Published picture count.
    std::atomic<std::uint64_t> renderedFrames_ {0};  ///< Submitted picture count.
    std::atomic<std::uint64_t> clientDroppedFrames_ {0};  ///< Local discard count.
    std::atomic<std::uint64_t> hostSamples_ {0};  ///< Valid host measurements.
    std::atomic<std::uint64_t> hostTimeTenthsMs_ {0};  ///< Host processing sum.
    std::atomic<std::uint64_t> decodeSamples_ {0};  ///< Decode timing sample count.
    std::atomic<std::uint64_t> decodeTimeUs_ {0};  ///< Decode duration sum.
    std::atomic<std::uint64_t> queueTimeUs_ {0};  ///< Queue duration sum.
    std::atomic<std::uint64_t> presentationTimeUs_ {0};  ///< Presentation duration sum.
    std::uint32_t lastFrameNumber_ = 0;  ///< Last sequence observed by the decoder worker.
    bool haveFrameNumber_ = false;  ///< Sequence baseline is available.
  };

  /**
   * @brief Derive one-second interval metrics independently of the requested stream frame rate.
   */
  class VideoStatisticsSampler {
  public:
    /**
     * @brief Check whether a new interval can be sampled.
     *
     * @param nowUs Monotonic clock.
     * @return True when sampling is due.
     */
    bool due(std::uint64_t nowUs) const;
    /**
     * @brief Update interval measurements, handling restarts and clock regressions.
     * @param nowUs Monotonic sample time.
     * @param counters Current cumulative counters.
     */
    void sample(std::uint64_t nowUs, const VideoStatisticsCounters &counters);
    /**
     * @brief Copy measured fields into a display snapshot.
     *
     * @param snapshot Receives the latest interval measurements.
     */
    void apply(StreamStatisticsSnapshot *snapshot) const;

  private:
    VideoStatisticsCounters previous_ {};  ///< Previous cumulative sample.
    StreamStatisticsSnapshot measured_ {};  ///< Latest interval measurements.
    std::uint64_t sampledAtUs_ = 0;  ///< Previous monotonic sample time.
    bool initialized_ = false;  ///< An interval baseline has been captured.
  };

}  // namespace streaming
