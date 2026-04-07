#pragma once

// standard includes
#include <cstdint>
#include <string>

namespace app {

  /**
   * @brief Reducer-driven stages for the manual pairing shell flow.
   */
  enum class PairingStage {
    idle,
    pin_ready,
    in_progress,
    paired,
    failed,
  };

  /**
   * @brief Controller-friendly state for a client-generated pairing PIN.
   */
  struct PairingDraft {
    std::string targetAddress;  ///< Host address currently targeted by pairing.
    uint16_t targetPort;  ///< Effective HTTP port currently targeted by pairing.
    std::string generatedPin;  ///< Client-generated PIN shown to the user.
    PairingStage stage;  ///< Current stage of the reducer-driven pairing flow.
    std::string statusMessage;  ///< User-visible progress or error message for the pairing flow.
  };

  /**
   * @brief Create a fresh pairing draft for the provided host.
   *
   * @param targetAddress Host address being paired.
   * @param targetPort Effective host port being paired.
   * @param generatedPin Client-generated PIN to show to the user.
   * @return Initialized pairing draft.
   */
  PairingDraft create_pairing_draft(const std::string &targetAddress, uint16_t targetPort, std::string generatedPin);

  /**
   * @brief Return whether a PIN string is a valid Moonlight-style four-digit PIN.
   *
   * @param pin Candidate PIN string.
   * @return true when the PIN contains exactly four digits.
   */
  bool is_valid_pairing_pin(const std::string &pin);

}  // namespace app
