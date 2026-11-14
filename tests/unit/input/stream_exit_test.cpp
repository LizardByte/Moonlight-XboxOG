/**
 * @file tests/unit/input/stream_exit_test.cpp
 * @brief Verifies digital-only exit detection, event ordering and disconnect cleanup.
 */
// test includes
#include <lizardbyte/common/testing.h>

// standard includes
#include <algorithm>
#include <array>

// local includes
#include "src/input/stream_exit.h"
#include "third-party/moonlight-common-c/src/Limelight.h"

/**
 * @brief Back and both shoulders quit immediately without Start, axes or a timer.
 */
TEST(StreamExitTest, RequiresAllThreeDigitalButtons) {
  const std::uint32_t chord = BACK_FLAG | LB_FLAG | RB_FLAG;
  EXPECT_TRUE(input::is_stream_exit_requested(chord, true));
  for (const auto button : {BACK_FLAG, LB_FLAG, RB_FLAG}) {
    EXPECT_FALSE(input::is_stream_exit_requested(chord & ~button, true));
  }
  EXPECT_FALSE(input::is_stream_exit_requested(BACK_FLAG | PLAY_FLAG, true));
  EXPECT_FALSE(input::is_stream_exit_requested(0, true));
}

/**
 * @brief Additional digital buttons do not mask a valid chord, but absence cancels it.
 */
TEST(StreamExitTest, RequiresConnectedControllerAndAllowsOtherButtons) {
  const std::uint32_t chord = BACK_FLAG | LB_FLAG | RB_FLAG;
  EXPECT_FALSE(input::is_stream_exit_requested(chord, false));
  EXPECT_TRUE(input::is_stream_exit_requested(chord | A_FLAG | X_FLAG, true));
}

/**
 * @brief Every press order recognizes the chord before the next polling sample.
 */
TEST(StreamExitTest, RecognizesEveryDigitalPressOrder) {
  std::array<std::uint32_t, 3> buttons {BACK_FLAG, LB_FLAG, RB_FLAG};
  do {
    input::StreamExitChord chord;
    EXPECT_FALSE(chord.update_button(buttons[0], true));
    EXPECT_FALSE(chord.update_button(buttons[1], true));
    EXPECT_TRUE(chord.update_button(buttons[2], true));
    EXPECT_FALSE(chord.update_button(buttons[0], false));
  } while (std::next_permutation(buttons.begin(), buttons.end()));
}

/**
 * @brief A chord followed by queued releases is recognized even if the final sampled state is empty.
 */
TEST(StreamExitTest, RecognizesChordBeforeQueuedReleases) {
  input::StreamExitChord chord;
  EXPECT_FALSE(chord.update(BACK_FLAG, true));
  EXPECT_FALSE(chord.update_button(LB_FLAG, true));
  EXPECT_TRUE(chord.update_button(RB_FLAG, true));
  EXPECT_FALSE(chord.update_button(BACK_FLAG, false));
  EXPECT_FALSE(chord.update_button(LB_FLAG, false));
  EXPECT_FALSE(chord.update_button(RB_FLAG, false));
  EXPECT_FALSE(chord.update(0, true));
}

/**
 * @brief Disconnect and reconnect cannot combine stale presses into a new chord.
 */
TEST(StreamExitTest, DisconnectClearsEventState) {
  input::StreamExitChord chord;
  EXPECT_FALSE(chord.update(BACK_FLAG | LB_FLAG, true));
  EXPECT_FALSE(chord.update(BACK_FLAG | LB_FLAG, false));
  EXPECT_FALSE(chord.update_button(RB_FLAG, true));
  EXPECT_FALSE(chord.update(0, true));
  EXPECT_FALSE(chord.update_button(BACK_FLAG, true));
}
