#include "src/streaming/stats_overlay.h"

#include <vector>

#include <gtest/gtest.h>

namespace {

  TEST(StreamStatsOverlayTest, FormatsKnownMetricsIntoReadableLines) {
    const streaming::StreamStatisticsSnapshot snapshot {
      1280,
      720,
      60,
      18,
      6,
      4,
      2,
      15,
      1024,
      12,
      3,
      false,
    };

    const std::vector<std::string> lines = streaming::build_stats_overlay_lines(snapshot);

    ASSERT_EQ(lines.size(), 5U);
    EXPECT_EQ(lines[0], "Stream: 1280x720 @ 60 FPS");
    EXPECT_EQ(lines[1], "Latency: RTT 18 ms | Host 6 ms | Decode 4 ms");
    EXPECT_EQ(lines[2], "Queues: Video 2 frames | Audio 15 ms");
    EXPECT_EQ(lines[3], "Video packets: 1024 rx | 12 recovered | 3 lost");
    EXPECT_EQ(lines[4], "Connection: Okay");
  }

  TEST(StreamStatsOverlayTest, OmitsUnavailableMetricGroups) {
    const streaming::StreamStatisticsSnapshot snapshot {
      640,
      480,
      30,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      -1,
      true,
    };

    const std::vector<std::string> lines = streaming::build_stats_overlay_lines(snapshot);

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "Stream: 640x480 @ 30 FPS");
    EXPECT_EQ(lines[1], "Connection: Poor");
  }

}  // namespace

