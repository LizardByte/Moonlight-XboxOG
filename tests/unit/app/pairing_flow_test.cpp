// class header include
#include "src/app/pairing_flow.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(PairingFlowTest, CreatesAFreshPairingDraftWithTheDefaultPin) {
    const app::PairingDraft draft = app::create_pairing_draft("192.168.1.20", 47984, "4821");

    EXPECT_EQ(draft.targetAddress, "192.168.1.20");
    EXPECT_EQ(draft.targetPort, 47984);
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
