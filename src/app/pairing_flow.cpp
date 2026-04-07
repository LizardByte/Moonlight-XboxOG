// class header include
#include "src/app/pairing_flow.h"

// standard includes
#include <algorithm>

namespace app {

  PairingDraft create_pairing_draft(const std::string &targetAddress, uint16_t targetPort, std::string generatedPin) {
    PairingDraft draft {
      targetAddress,
      targetPort,
      std::move(generatedPin),
      PairingStage::idle,
      "Checking whether the host is reachable before pairing begins.",
    };
    return draft;
  }

  bool is_valid_pairing_pin(const std::string &pin) {
    if (pin.size() != 4) {
      return false;
    }

    return std::all_of(pin.begin(), pin.end(), [](char digit) {
      return digit >= '0' && digit <= '9';
    });
  }

}  // namespace app
