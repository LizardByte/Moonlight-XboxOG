/**
 * @file src/app/pairing_flow.cpp
 * @brief Implements the host pairing flow.
 */
// class header include
#include "src/app/pairing_flow.h"

// standard includes
#include <algorithm>

namespace app {

  PairingDraft create_pairing_draft(std::string_view targetAddress, uint16_t targetPort, std::string generatedPin) {
    PairingDraft draft {
      std::string(targetAddress),
      targetPort,
      std::move(generatedPin),
      PairingStage::pin_ready,
      "Enter the PIN on the host. Pairing will continue automatically.",
    };
    return draft;
  }

  bool is_valid_pairing_pin(std::string_view pin) {
    if (pin.size() != 4) {
      return false;
    }

    return std::all_of(pin.begin(), pin.end(), [](char digit) {
      return digit >= '0' && digit <= '9';
    });
  }

}  // namespace app
