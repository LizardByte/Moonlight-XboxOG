/**
 * @file src/input/stream_exit.h
 * @brief Defines a digital stream exit chord that does not invoke xemu's Start and Back menu.
 */
#pragma once

#include <cstdint>

namespace input {
  constexpr const char *STREAM_EXIT_HINT = "Press Back/Select + Black + White (LB + RB) together to stop streaming. No hold required.";  ///< Shared startup and streaming instruction.

  /**
   * @brief Recognize the digital exit chord without consulting axes or a clock.
   *
   * @param buttons Current Moonlight controller button flags.
   * @param controllerPresent True when the sampled controller is connected.
   * @return True when Back and both shoulders are pressed on a connected controller.
   */
  bool is_stream_exit_requested(std::uint32_t buttons, bool controllerPresent);

  /**
   * @brief Track digital button events so a complete chord is detected before queued releases are drained.
   */
  class StreamExitChord {
  public:
    /**
     * @brief Synchronize event state with a sampled controller, clearing it on disconnect.
     *
     * @param buttons Current Moonlight controller button flags.
     * @param controllerPresent True when the sampled controller is connected.
     * @return True when the complete digital chord is pressed.
     */
    bool update(std::uint32_t buttons, bool controllerPresent);

    /**
     * @brief Apply one digital button transition in event order.
     *
     * @param buttonFlag Moonlight flag for the button reported by the event.
     * @param pressed True for a press, false for a release.
     * @return True as soon as the transition completes the chord.
     */
    bool update_button(std::uint32_t buttonFlag, bool pressed);

  private:
    std::uint32_t buttons_ = 0;  ///< Digital buttons currently down in event order.
  };
}  // namespace input
