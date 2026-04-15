/**
 * @file tests/unit/app/pairing_flow_test.cpp
 * @brief Verifies the host pairing flow.
 */
// class header include
#include "src/app/pairing_flow.h"

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/network_test_constants.h"

namespace {

  TEST(PairingFlowTest, CreatesAFreshPairingDraftWithTheDefaultPin) {
    const app::PairingDraft draft = app::create_pairing_draft(
      test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
      test_support::kTestPorts[test_support::kPortPairing],
      "4821"
    );

    EXPECT_EQ(draft.targetAddress, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(draft.targetPort, test_support::kTestPorts[test_support::kPortPairing]);
    EXPECT_EQ(draft.stage, app::PairingStage::idle);
    EXPECT_EQ(draft.generatedPin, "4821");
    EXPECT_EQ(draft.statusMessage, "Checking whether the host is reachable before pairing begins.");
  }

  TEST(PairingFlowTest, AcceptsOnlyFourDigitPins) {
    EXPECT_TRUE(app::is_valid_pairing_pin("1234"));
    EXPECT_TRUE(app::is_valid_pairing_pin("0007"));

    EXPECT_FALSE(app::is_valid_pairing_pin("123"));
    EXPECT_FALSE(app::is_valid_pairing_pin("12345"));
    EXPECT_FALSE(app::is_valid_pairing_pin("12a4"));
  }

}  // namespace
