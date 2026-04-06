// class header include
#include "src/app/pairing_flow.h"

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

    for (char digit : pin) {
      if (digit < '0' || digit > '9') {
        return false;
      }
    }

    return true;
  }

}  // namespace app

