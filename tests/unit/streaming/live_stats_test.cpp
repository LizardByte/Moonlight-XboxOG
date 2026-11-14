/**
 * @file tests/unit/streaming/live_stats_test.cpp
 * @brief Covers telemetry intervals, network loss accounting and unavailable measurements.
 */
// test includes
// test includes
#include <lizardbyte/common/testing.h>

// standard includes
#include <limits>

// local includes
#include "src/streaming/stats_overlay.h"

/**
 * @brief Rates derive from elapsed time and counters rather than the requested stream FPS.
 */
TEST(LiveStatsTest, MeasuresActualRatesAndStageTimes) {
  streaming::VideoStatisticsSampler sampler;
  sampler.sample(0, {});
  streaming::StreamStatisticsSnapshot display {};
  display.fps = 60;
  sampler.apply(&display);
  EXPECT_LT(display.renderingFps, 0);
  EXPECT_FALSE(sampler.due(999999));
  streaming::VideoStatisticsCounters counters {30, 2, 500000, 10, 8, 20, 2, 120, 10, 200000, 30000, 24000};
  sampler.sample(1000000, counters);
  sampler.apply(&display);
  EXPECT_EQ(display.fps, 60);
  EXPECT_DOUBLE_EQ(display.incomingFps, 30);
  EXPECT_DOUBLE_EQ(display.decodingFps, 10);
  EXPECT_DOUBLE_EQ(display.renderingFps, 8);
  EXPECT_DOUBLE_EQ(display.videoBitrateMbps, 4);
  EXPECT_DOUBLE_EQ(display.hostProcessingMs, 6);
  EXPECT_DOUBLE_EQ(display.decodeTimeMs, 20);
  EXPECT_DOUBLE_EQ(display.queueTimeMs, 3);
  EXPECT_DOUBLE_EQ(display.presentationTimeMs, 3);
  EXPECT_DOUBLE_EQ(display.networkDropPercent, 6.25);
  EXPECT_NEAR(display.clientDropPercent, 66.6667, 0.001);
  counters.receivedFrames += 10;
  sampler.sample(2000000, counters);
  sampler.apply(&display);
  EXPECT_DOUBLE_EQ(display.incomingFps, 10);
  EXPECT_DOUBLE_EQ(display.renderingFps, 0);
  EXPECT_LT(display.decodeTimeMs, 0);
  EXPECT_LT(display.hostProcessingMs, 0);
}

/**
 * @brief Restarts and backwards clocks cannot produce negative or enormous interval rates.
 */
TEST(LiveStatsTest, RebaselinesAfterRestartAndClockRegression) {
  streaming::VideoStatisticsSampler sampler;
  streaming::VideoStatisticsCounters counters;
  counters.receivedFrames = 50;
  sampler.sample(2000000, counters);
  sampler.sample(3000000, {});
  streaming::StreamStatisticsSnapshot display {};
  sampler.apply(&display);
  EXPECT_LT(display.incomingFps, 0);
  sampler.sample(1000000, {});
  sampler.apply(&display);
  EXPECT_LT(display.incomingFps, 0);
  EXPECT_FALSE(sampler.due(1500000));
}

/**
 * @brief Sequence gaps are distinct from local discards, including wraparound and out-of-order frames.
 */
TEST(LiveStatsTest, SeparatesNetworkLossAndLocalDiscards) {
  streaming::VideoStatistics statistics;
  statistics.received(100, 1000, 0);
  statistics.received(102, 2000, 65);
  statistics.received(101, 500, 0);
  statistics.received(103, 500, 75);
  statistics.dropped(2);
  statistics.decoded();
  statistics.decode_time(1200, 300);
  statistics.presented(400);
  auto counters = statistics.counters();
  EXPECT_EQ(counters.receivedFrames, 4);
  EXPECT_EQ(counters.networkDroppedFrames, 1);
  EXPECT_EQ(counters.clientDroppedFrames, 2);
  EXPECT_EQ(counters.receivedBytes, 4000);
  EXPECT_EQ(counters.hostSamples, 2);
  EXPECT_EQ(counters.hostTimeTenthsMs, 140);
  EXPECT_EQ(counters.decodeTimeUs, 1200);
  EXPECT_EQ(counters.presentationTimeUs, 400);
  statistics.reset();
  statistics.received(0xfffffffeU, 1, 0);
  statistics.received(0, 1, 0);
  EXPECT_EQ(statistics.counters().networkDroppedFrames, 1);
  EXPECT_EQ(statistics.counters().hostSamples, 0);
}

/**
 * @brief The live panel identifies actual rates and explicit unavailable data on either presentation path.
 */
TEST(LiveStatsTest, FormatsBothPresentationPathsWithoutFabricatedTimings) {
  streaming::StreamStatisticsSnapshot snapshot {};
  snapshot.width = 1280;
  snapshot.height = 720;
  snapshot.fps = 30;
  snapshot.videoCodec = "H.264";
  snapshot.decoderFeatures = "MMX/SSE";
  snapshot.roundTripTimeMs = 4;
  snapshot.roundTripVarianceMs = 1;
  snapshot.renderingFps = 18;
  for (const auto *path : {"GPU", "Software"}) {
    snapshot.presentationPath = path;
    const auto lines = streaming::build_live_stats_overlay_lines(snapshot);
    ASSERT_EQ(lines.size(), 8U);
    EXPECT_NE(lines[0].find("1280x720 @ 30 FPS"), std::string::npos);
    EXPECT_NE(lines[1].find("rendered 18.0"), std::string::npos);
    EXPECT_NE(lines[2].find("Host processing: N/A"), std::string::npos);
    EXPECT_NE(lines[3].find("Variance: 1 ms"), std::string::npos);
    EXPECT_NE(lines[7].find(std::string("Presentation: ") + path), std::string::npos);
  }
}

/**
 * @brief Numeric updates use bounded fixed-point text, including rounding and invalid inputs.
 */
TEST(LiveStatsTest, FormatsNumericUpdatesWithoutFloatingPointPrintf) {
  streaming::StreamStatisticsSnapshot snapshot {};
  snapshot.incomingFps = 29.96;
  snapshot.decodingFps = 18.24;
  snapshot.renderingFps = 0;
  snapshot.videoBitrateMbps = std::numeric_limits<double>::infinity();
  snapshot.hostProcessingMs = std::numeric_limits<double>::quiet_NaN();
  const auto lines = streaming::build_live_stats_overlay_lines(snapshot);
  EXPECT_EQ(lines[1], "FPS: incoming 30.0 | decoded 18.2 | rendered 0.0");
  EXPECT_EQ(lines[2], "Video bitrate: N/A | Host processing: N/A");
}
