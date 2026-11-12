/**
 * @file tests/unit/platform/fenv_compat_test.cpp
 * @brief Verifies that nxdk rounding changes preserve the other x87 control settings.
 */
#include "src/_nxdk_compat/fenv_compat.h"

#include <array>
#include <cstdint>
#include <lizardbyte/common/testing.h>

#if defined(__i386__) || defined(__x86_64__)
namespace {

  /**
   * @brief Read the x87 control word without changing the floating-point environment.
   *
   * @return Current x87 control word.
   */
  std::uint16_t read_control_word() {
    std::uint16_t controlWord = 0;
    asm volatile("fnstcw %0" : "=m"(controlWord));
    return controlWord;
  }

  /**
   * @brief Restore the x87 control word after a rounding test.
   */
  class FenvCompatTest: public BaseTest {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    std::uint16_t originalControlWord = 0;  ///< Control word restored after each test.

    /**
     * @brief Save the control word before changing the rounding mode.
     */
    void SetUp() override {
      BaseTest::SetUp();
      originalControlWord = read_control_word();
    }

    /**
     * @brief Restore the original control word after each test.
     */
    void TearDown() override {
      asm volatile("fldcw %0" : : "m"(originalControlWord));
      BaseTest::TearDown();
    }
  };

  TEST_F(FenvCompatTest, PreservesExceptionMasksAndPrecisionWhenChangingRoundingMode) {
    const std::uint16_t initialControlWord = static_cast<std::uint16_t>(originalControlWord | 0x003F);
    asm volatile("fldcw %0" : : "m"(initialControlWord));

    for (const int roundingMode : std::array {0x0000, 0x0400, 0x0800, 0x0C00}) {
      ASSERT_EQ(nxdk_compat::set_x87_rounding_mode(roundingMode), 0);
      const std::uint16_t controlWord = read_control_word();
      EXPECT_EQ(controlWord & 0x0C00, roundingMode);
      EXPECT_EQ(controlWord & ~0x0C00, initialControlWord & ~0x0C00);
    }
  }

  TEST_F(FenvCompatTest, RejectsInvalidRoundingModesWithoutChangingControlWord) {
    for (const int roundingMode : std::array {-1, 0x0001, 0x0100, 0x1000}) {
      EXPECT_EQ(nxdk_compat::set_x87_rounding_mode(roundingMode), -1);
      EXPECT_EQ(read_control_word(), originalControlWord);
    }
  }

}  // namespace
#endif
