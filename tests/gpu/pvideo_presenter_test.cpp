/**
 * @file tests/gpu/pvideo_presenter_test.cpp
 * @brief Exercises the production presenter against deterministic NV2A, pbkit and allocator shims.
 */
// test includes
// test includes
#include <lizardbyte/common/testing.h>

// standard includes
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>

// lib includes
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

// local includes
#include "src/streaming/pvideo_presenter.h"

namespace {
  /**
   * @brief Mutable fake hardware, reset independently for each lifecycle test.
   */
  struct Hardware {
    std::array<std::uint32_t, 0x700000 / 4> registers {};  ///< MMIO storage.
    std::array<std::uint8_t, 256> crtc {};  ///< Indexed VGA registers.
    std::uint8_t crtcIndex = 0;  ///< Active VGA selector.
    std::array<unsigned char, 640 * 480 * 4> framebuffer {};  ///< CPU-owned framebuffer.
    std::array<std::uint32_t, 640 * 480> gpuBuffer {};  ///< Separate GPU-owned scanout.
    VIDEO_MODE mode {640, 480, 32, 60};  ///< Active output mode.
    DWORD ticks = 0;  ///< Deterministic clock.
    DWORD vblanks = 0;  ///< Deterministic scanout clock.
    int initResult = 0;  ///< Injected pbkit initialization result.
    int allocationsUntilFailure = -1;  ///< Negative disables allocation failure injection.
    int initializations = 0;  ///< Number of GPU context acquisitions.
    int shutdowns = 0;  ///< Number of GPU context releases.
    unsigned int pitch = 640 * 4;  ///< GPU buffer pitch.
    std::uint32_t originalStart = 0;  ///< CPU scanout restored by pb_kill.
    bool videoEnabled = true;  ///< TV encoder output state.
    bool backgroundCleared = false;  ///< GPU background was cleared before overlay submission.
    std::map<void *, std::uint32_t> allocations;  ///< Live DMA allocations and addresses.
    std::uint32_t nextPhysical = 0x100000;  ///< Next fake physical allocation address.
  };

  Hardware hardware;  ///< State shared by the hardware API shims.

  /**
   * @brief Presenter fixture with a small valid YUV420 picture and clean hardware state.
   */
  class PvideoPresenterTest: public testing::Test {
  protected:
    /**
     * @brief Reset hardware without placing its large register bank on the stack.
     */
    void SetUp() override {
      hardware.registers.fill(0);
      hardware.crtc.fill(0);
      hardware.crtcIndex = 0;
      hardware.mode = {640, 480, 32, 60};
      hardware.ticks = hardware.vblanks = 0;
      hardware.initResult = 0;
      hardware.allocationsUntilFailure = -1;
      hardware.initializations = hardware.shutdowns = 0;
      hardware.pitch = 640 * 4;
      hardware.videoEnabled = true;
      hardware.backgroundCleared = false;
      hardware.nextPhysical = 0x100000;
      ASSERT_TRUE(hardware.allocations.empty());
    }

    /**
     * @brief Require each test to release all DMA storage.
     */
    void TearDown() override {
      EXPECT_TRUE(hardware.allocations.empty());
    }

    std::array<std::uint8_t, 4> y {16, 81, 145, 235};  ///< Source luma.
    std::array<std::uint8_t, 1> chroma {128};  ///< Neutral source chroma.
    streaming::Yuv420FrameView frame {2, 2, {y.data(), chroma.data(), chroma.data()}, {4, 1, 1}, {2, 1, 1}};  ///< Valid source.
    streaming::VideoRectangle destination {0, 60, 640, 360};  ///< Letterboxed output.
  };
}  // namespace

std::uint32_t &pvideo_test_register(std::uint32_t address) {
  return hardware.registers.at(address / 4);
}

std::uint8_t &pvideo_test_register8(std::uint32_t address) {
  return address == 0x6013d4 ? hardware.crtcIndex : hardware.crtc.at(hardware.crtcIndex);
}

VIDEO_MODE XVideoGetMode() {
  return hardware.mode;
}

unsigned char *XVideoGetFB() {
  return hardware.framebuffer.data();
}

void XVideoSetVideoEnable(int enable) {
  hardware.videoEnabled = enable != 0;
}

DWORD GetTickCount() {
  return hardware.ticks;
}

void Sleep(DWORD milliseconds) {
  hardware.ticks += milliseconds;
  ++hardware.vblanks;
}

std::uint32_t pb_get_vbl_counter() {
  return hardware.vblanks;
}

int pb_busy() {
  return 0;
}

int pb_init() {
  ++hardware.initializations;
  hardware.originalStart = VIDEOREG(0x600800);
  hardware.crtc[31] = 87;
  hardware.crtc[82] += 4;
  hardware.crtc[32] = 41;
  hardware.crtc[27] = 5;
  VIDEOREG(0x8910) = 0x1000;
  VIDEOREG(0x600804) = 2;
  if (hardware.initResult != 0) {
    hardware.videoEnabled = false;
  }
  return hardware.initResult;
}

void pb_kill() {
  ++hardware.shutdowns;
  VIDEOREG(0x600800) = hardware.originalStart;
  hardware.videoEnabled = false;
}

void pb_set_color_format(unsigned int format, bool swizzled) {
  EXPECT_EQ(format, NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8);
  EXPECT_FALSE(swizzled);
}

void pb_fill(int x, int y, int width, int height, std::uint32_t color) {
  EXPECT_EQ(x, 0);
  EXPECT_EQ(y, 0);
  EXPECT_EQ(width, hardware.mode.width);
  EXPECT_EQ(height, hardware.mode.height);
  EXPECT_EQ(color, 0xff000000U);
  hardware.backgroundCleared = true;
}

std::uint32_t *pb_back_buffer() {
  return hardware.gpuBuffer.data();
}

unsigned int pb_back_buffer_pitch() {
  return hardware.pitch;
}

void *MmAllocateContiguousMemoryEx(std::size_t size, unsigned int, unsigned int, unsigned int, unsigned int) {
  if (hardware.allocationsUntilFailure == 0) {
    return nullptr;
  }
  if (hardware.allocationsUntilFailure > 0) {
    --hardware.allocationsUntilFailure;
  }
  void *buffer = std::malloc(size);
  if (buffer != nullptr) {
    hardware.allocations.emplace(buffer, hardware.nextPhysical);
    hardware.nextPhysical += 0x10000;
  }
  return buffer;
}

void MmFreeContiguousMemory(void *buffer) {
  EXPECT_EQ(hardware.allocations.erase(buffer), 1U);
  std::free(buffer);
}

std::uintptr_t MmGetPhysicalAddress(void *buffer) {
  return buffer == hardware.gpuBuffer.data() ? 0x80000 : hardware.allocations.at(buffer);
}

/**
 * @brief Both RAM buffers are submitted through bank zero with source dimensions in pixels.
 */
TEST_F(PvideoPresenterTest, AlternatesMemoryWithoutSelectingUnsupportedOverlayBank) {
  streaming::PvideoPresenter presenter;
  std::string error;
  ASSERT_TRUE(presenter.present(frame, destination, &error)) << error;
  EXPECT_TRUE(hardware.backgroundCleared);
  EXPECT_EQ(VIDEOREG(0x600800), 0x80000U);
  EXPECT_EQ(VIDEOREG(0x8928), 2U | (2U << 16U));
  EXPECT_EQ(VIDEOREG(0x8700), 1U);
  const auto first = VIDEOREG(0x8920);
  const auto firstLimit = VIDEOREG(0x8908);
  ASSERT_TRUE(presenter.present(frame, destination, &error)) << error;
  EXPECT_EQ(VIDEOREG(0x8700), 1U);
  EXPECT_NE(VIDEOREG(0x8920), first);
  const auto second = VIDEOREG(0x8920);
  const auto bytes = 2U * 64U;
  // A compositor can retain either frame's limit while reading either address.
  EXPECT_LE(second + bytes, firstLimit);
  EXPECT_EQ(VIDEOREG(0x8908), firstLimit);
  ASSERT_TRUE(presenter.present(frame, destination, &error)) << error;
  EXPECT_EQ(VIDEOREG(0x8920), first);
  EXPECT_LE(second + bytes, VIDEOREG(0x8908));
  EXPECT_EQ(VIDEOREG(0x8908), firstLimit);
  EXPECT_EQ(VIDEOREG(0x8924), 0U);
  EXPECT_GE(VIDEOREG(0x8908) - VIDEOREG(0x8920) + 1U, 2U * 64U + 64U);
}

/**
 * @brief The production GPU path composites the cached panel before submitting either buffer.
 */
TEST_F(PvideoPresenterTest, CompositesStatisticsIntoSubmittedVideoBuffer) {
  streaming::PvideoPresenter presenter;
  streaming::VideoOverlay overlay;
  overlay.set_bitmap(2, 2, {255, 255, 0, 0});
  std::string error;
  ASSERT_TRUE(presenter.present(frame, destination, &error, {}, &overlay)) << error;
  const auto allocation = std::find_if(hardware.allocations.begin(), hardware.allocations.end(), [](const auto &entry) {
    return entry.second == VIDEOREG(0x8920);
  });
  ASSERT_NE(allocation, hardware.allocations.end());
  const auto *pixels = static_cast<const std::uint8_t *>(allocation->first);
  EXPECT_EQ(pixels[0], 235);
  EXPECT_EQ(pixels[1], 128);
  EXPECT_EQ(pixels[2], 235);
  ASSERT_TRUE(presenter.present(frame, destination, &error)) << error;
  const auto next = std::find_if(hardware.allocations.begin(), hardware.allocations.end(), [](const auto &entry) {
    return entry.second == VIDEOREG(0x8920);
  });
  ASSERT_NE(next, hardware.allocations.end());
  EXPECT_EQ(static_cast<const std::uint8_t *>(next->first)[2], 81);
}

/**
 * @brief Repeated sessions restore the CPU scanout, legacy registers and enabled video output.
 */
TEST_F(PvideoPresenterTest, RestoresOutputAndRegistersAcrossRepeatedSessions) {
  constexpr std::array<int, 4> crtcRegisters {31, 82, 32, 27};
  const std::array<std::uint8_t, 4> originalCrtc {13, 14, 15, 16};
  for (std::size_t index = 0; index < originalCrtc.size(); ++index) {
    hardware.crtc[crtcRegisters[index]] = originalCrtc[index];
  }
  VIDEOREG(0x8910) = 0x2000;
  VIDEOREG(0x600800) = 0x3eb4000;
  VIDEOREG(0x600804) = 3;
  streaming::PvideoPresenter presenter;
  for (int session = 0; session < 2; ++session) {
    std::string error;
    ASSERT_TRUE(presenter.present(frame, destination, &error)) << error;
    ASSERT_TRUE(presenter.reset());
    EXPECT_FALSE(presenter.active());
    EXPECT_EQ(VIDEOREG(0x8700), 0U);
    EXPECT_EQ(VIDEOREG(0x8910), 0x2000U);
    EXPECT_EQ(VIDEOREG(0x600800), 0x3eb4000U);
    EXPECT_EQ(VIDEOREG(0x600804), 3U);
    EXPECT_TRUE(hardware.videoEnabled);
    EXPECT_TRUE(hardware.allocations.empty());
    for (std::size_t index = 0; index < originalCrtc.size(); ++index) {
      EXPECT_EQ(hardware.crtc[crtcRegisters[index]], originalCrtc[index]);
    }
  }
  EXPECT_EQ(hardware.initializations, 2);
  EXPECT_EQ(hardware.shutdowns, 2);
}

/**
 * @brief A partial allocation failure releases GPU ownership and restores visible software output.
 */
TEST_F(PvideoPresenterTest, RestoresSoftwareOutputAfterPartialAllocationFailure) {
  hardware.allocationsUntilFailure = 1;
  streaming::PvideoPresenter presenter;
  std::string error;
  EXPECT_FALSE(presenter.present(frame, destination, &error));
  EXPECT_NE(error.find("contiguous memory"), std::string::npos);
  EXPECT_TRUE(hardware.videoEnabled);
  EXPECT_TRUE(hardware.allocations.empty());
  EXPECT_EQ(hardware.shutdowns, 1);
}

/**
 * @brief pbkit failure may disable output internally, so initialization failures also restore it.
 */
TEST_F(PvideoPresenterTest, RestoresSoftwareOutputAfterGpuInitializationFailure) {
  hardware.initResult = -4;
  streaming::PvideoPresenter presenter;
  std::string error;
  EXPECT_FALSE(presenter.present(frame, destination, &error));
  EXPECT_NE(error.find("pbkit -4"), std::string::npos);
  EXPECT_TRUE(hardware.videoEnabled);
  EXPECT_EQ(hardware.shutdowns, 0);
}

/**
 * @brief Unsupported output format and scanout pitch select software with a clear explanation.
 */
TEST_F(PvideoPresenterTest, RejectsIncompatibleOutputBeforeSubmittingOverlay) {
  streaming::PvideoPresenter presenter;
  std::string error;
  hardware.mode.bpp = 16;
  EXPECT_FALSE(presenter.present(frame, destination, &error));
  EXPECT_NE(error.find("32-bit framebuffer"), std::string::npos);
  EXPECT_EQ(hardware.initializations, 0);
  hardware.mode.bpp = 32;
  hardware.pitch += 64;
  EXPECT_FALSE(presenter.present(frame, destination, &error));
  EXPECT_NE(error.find("pitch"), std::string::npos);
  EXPECT_EQ(VIDEOREG(0x8700), 0U);
  EXPECT_TRUE(hardware.videoEnabled);
  EXPECT_EQ(hardware.shutdowns, 1);
}
