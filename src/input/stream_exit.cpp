/**
 * @file src/input/stream_exit.cpp
 * @brief Recognizes the digital Back, Black and White exit chord from snapshots and events.
 */
#include "src/input/stream_exit.h"

#include "third-party/moonlight-common-c/src/Limelight.h"

namespace input {
  bool is_stream_exit_requested(std::uint32_t buttons, bool controllerPresent) {
    constexpr std::uint32_t required = BACK_FLAG | LB_FLAG | RB_FLAG;
    return controllerPresent && (buttons & required) == required;
  }

  bool StreamExitChord::update(std::uint32_t buttons, bool controllerPresent) {
    buttons_ = controllerPresent ? buttons : 0;
    return is_stream_exit_requested(buttons_, controllerPresent);
  }

  bool StreamExitChord::update_button(std::uint32_t buttonFlag, bool pressed) {
    if (pressed) {
      buttons_ |= buttonFlag;
    } else {
      buttons_ &= ~buttonFlag;
    }
    return is_stream_exit_requested(buttons_, true);
  }
}  // namespace input
