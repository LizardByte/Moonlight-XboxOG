/**
 * @file src/streaming/pvideo_presenter.cpp
 * @brief Submits bounded YUY2 overlay buffers using documented NV2A PVIDEO registers.
 */
#include "src/streaming/pvideo_presenter.h"

#ifdef NXDK
  #include <algorithm>
  #include <cstring>
  #include <hal/video.h>
  #include <pbkit/pbkit.h>
  #include <windows.h>
  #include <xboxkrnl/xboxkrnl.h>

namespace {
  constexpr std::uint32_t VIDEO_BUFFER = 0x8700;  ///< Overlay buffer submission register.
  constexpr std::uint32_t VIDEO_STOP = 0x8704;  ///< Normal overlay stop register.
  constexpr std::array<std::uint8_t, 4> CRTC_REGISTERS {31, 82, 32, 27};  ///< VGA controls changed by pb_init but not restored by pb_kill.
  bool overlayQuarantined = false;  ///< A synchronization failure disables overlays until process exit.
  std::array<std::uint8_t *, 2> quarantinedBuffers {};  ///< Keep uncertain DMA references resident instead of freeing memory still in use.

  /**
   * @brief Wait for pbkit's vblank counter without an unbounded event wait or a synthetic raster register.
   *
   * xemu's raster register increments on reads rather than reporting scanlines,
   * so a raster-wrap poll cannot reliably synchronize buffer reuse there.
   *
   * @return True on a new vblank, or false after 50 milliseconds.
   */
  bool wait_for_scanout() {
    const DWORD started = GetTickCount();
    const DWORD previous = pb_get_vbl_counter();
    while (GetTickCount() - started < 50U) {
      Sleep(1);
      if (pb_get_vbl_counter() != previous) {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Bound GPU command completion before releasing the scanout context.
   *
   * @return True when the command processor is idle, or false after 100 milliseconds.
   */
  bool wait_for_gpu_idle() {
    const DWORD started = GetTickCount();
    while (pb_busy()) {
      if (GetTickCount() - started >= 100U) {
        return false;
      }
      Sleep(1);
    }
    return true;
  }

  constexpr std::array<std::uint32_t, 28> SAVED_REGISTERS {
    0x8140,
    0x8704,
    0x8900,
    0x8908,
    0x8910,
    0x8918,
    0x8920,
    0x8928,
    0x8930,
    0x8938,
    0x8940,
    0x8948,
    0x8950,
    0x8958,
    0x8904,
    0x890c,
    0x8914,
    0x891c,
    0x8924,
    0x892c,
    0x8934,
    0x893c,
    0x8944,
    0x894c,
    0x8954,
    0x895c,
    0x8088,
    0x808c,
  };  ///< Interrupt, buffer and fetch state touched by the overlay presenter.

  /**
   * @brief Encode an endpoint-aligned 12.20 scaling increment.
   *
   * @param source Source extent in pixels.
   * @param destination Destination extent in pixels.
   *
   * @return Hardware scaling increment.
   */
  std::uint32_t scaling_increment(int source, int destination) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(source - 1) << 20U) / static_cast<unsigned int>(destination - 1));
  }
}  // namespace
#endif

namespace streaming {
  PvideoPresenter::~PvideoPresenter() {
    reset();
  }

  bool PvideoPresenter::active() const {
    return active_;
  }

  bool PvideoPresenter::initialize_scanout_surface(std::string *error) {
#ifdef NXDK
    const VIDEO_MODE mode = XVideoGetMode();
    void *framebuffer = XVideoGetFB();
    if (framebuffer == nullptr || mode.bpp != 32 || mode.width <= 0 || mode.height <= 0 || (mode.width * 4 & 63) != 0) {
      *error = "GPU scanout requires a 32-bit framebuffer with a 64-byte aligned pitch";
      return false;
    }
    for (std::size_t index = 0; index < SAVED_REGISTERS.size(); ++index) {
      savedRegisters_[index] = VIDEOREG(SAVED_REGISTERS[index]);
    }
    const auto crtcIndex = VIDEOREG8(0x6013d4);
    for (std::size_t index = 0; index < CRTC_REGISTERS.size(); ++index) {
      VIDEOREG8(0x6013d4) = CRTC_REGISTERS[index];
      savedCrtcRegisters_[index] = VIDEOREG8(0x6013d5);
    }
    VIDEOREG8(0x6013d4) = crtcIndex;
    savedCrtcConfig_ = VIDEOREG(0x600804);
    scanoutStateSaved_ = true;
    pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8, false);
    const int status = pb_init();
    if (status != 0) {
      *error = "GPU scanout initialization failed (pbkit " + std::to_string(status) + ")";
      return false;
    }
    surfaceActive_ = true;
    if (pb_back_buffer_pitch() != static_cast<unsigned int>(mode.width * 4)) {
      *error = "GPU scanout pitch does not match the active display mode";
      return false;
    }
    // Keep SDL's CPU framebuffer outside the GPU render targets. Rendering
    // into it would leave a cached GPU surface over later CPU menu updates.
    pb_fill(0, 0, mode.width, mode.height, 0xff000000U);
    if (!wait_for_gpu_idle()) {
      *error = "Timed out preparing the GPU scanout surface";
      return false;
    }
    VIDEOREG(0x600800) = static_cast<std::uint32_t>(MmGetPhysicalAddress(pb_back_buffer()));
    return true;
#else
    *error = "GPU scanout is unavailable on this platform";
    return false;
#endif
  }

  bool PvideoPresenter::reset() {
    bool synchronized = true;
#ifdef NXDK
    if (active_) {
      VIDEOREG(VIDEO_STOP) = 1;
      VIDEOREG(VIDEO_BUFFER) = 0;
      synchronized = wait_for_scanout();
      if (!synchronized) {
        overlayQuarantined = true;
        for (std::size_t index = 0; index < buffers_.size(); ++index) {
          quarantinedBuffers[index] = buffers_[index];
          buffers_[index] = nullptr;
        }
      }
    }
    for (auto *&buffer : buffers_) {
      if (buffer != nullptr) {
        MmFreeContiguousMemory(buffer);
        buffer = nullptr;
      }
    }
    if (surfaceActive_) {
      if (wait_for_gpu_idle()) {
        pb_kill();
      } else {
        overlayQuarantined = true;
        synchronized = false;
      }
      surfaceActive_ = false;
    }
    if (scanoutStateSaved_) {
      for (std::size_t index = 0; index < SAVED_REGISTERS.size(); ++index) {
        VIDEOREG(SAVED_REGISTERS[index]) = savedRegisters_[index];
      }
      const auto crtcIndex = VIDEOREG8(0x6013d4);
      for (std::size_t index = 0; index < CRTC_REGISTERS.size(); ++index) {
        VIDEOREG8(0x6013d4) = CRTC_REGISTERS[index];
        VIDEOREG8(0x6013d5) = savedCrtcRegisters_[index];
      }
      VIDEOREG8(0x6013d4) = crtcIndex;
      VIDEOREG(0x600804) = savedCrtcConfig_;
      // pb_kill disables the TV encoder; the CPU framebuffer needs output
      // explicitly re-enabled even when the video mode has not changed.
      XVideoSetVideoEnable(TRUE);
      scanoutStateSaved_ = false;
    }
#endif
    active_ = false;
    bufferSize_ = 0;
    nextBuffer_ = 0;
    return synchronized;
  }

  bool PvideoPresenter::present(const Yuv420FrameView &frame, const VideoRectangle &destination, std::string *error, const OverlaySamplingRegion &sampling, VideoOverlay *overlay) {
    std::string failure = overlay_frame_error(frame);
    const int sourceWidth = sampling.width == 0 ? frame.width : sampling.width;
    const int sourceHeight = sampling.height == 0 ? frame.height : sampling.height;
    if (failure.empty() && (destination.x < 0 || destination.y < 0 || destination.width < 2 || destination.height < 2 || destination.width > 4095 || destination.height > 4095 || destination.x > 4095 - destination.width || destination.y > 4095 - destination.height)) {
      failure = "Output rectangle is outside the GPU overlay limits";
    }
    if (failure.empty() && (sourceWidth < 2 || sourceHeight < 2 || sampling.xHalfPel < 0 || sampling.yHalfPel < 0 || static_cast<std::int64_t>(sampling.xHalfPel) + 2LL * (sourceWidth - 1) + (sampling.xHalfPel & 1) > 2LL * (frame.width - 1) || static_cast<std::int64_t>(sampling.yHalfPel) + 2LL * (sourceHeight - 1) + (sampling.yHalfPel & 1) > 2LL * (frame.height - 1))) {
      failure = "Source sampling region exceeds the GPU input buffer";
    }
#ifdef NXDK
    if (failure.empty() && overlayQuarantined) {
      failure = "GPU overlay disabled after a scanout timeout";
    }
    const int pitch = failure.empty() ? (frame.width * 2 + 63) & ~63 : 0;
    // Retain one cache line beyond the visible rows for fetch padding and an
    // inclusive DMA limit that also satisfies xemu's exclusive-end check.
    const std::size_t required = failure.empty() ? static_cast<std::size_t>(pitch) * static_cast<std::size_t>(frame.height) + 64U : 0;
    if (failure.empty() && (!active_ || required != bufferSize_ || pitch != pitch_)) {
      if (!reset()) {
        failure = "GPU overlay teardown timed out";
      } else if ((VIDEOREG(VIDEO_BUFFER) & 0x11U) != 0) {
        failure = "Another owner is using the GPU overlay";
      } else if (!initialize_scanout_surface(&failure)) {
        // The explicit initialization reason is retained below.
      } else {
        for (auto *&buffer : buffers_) {
          buffer = static_cast<std::uint8_t *>(MmAllocateContiguousMemoryEx(required, 0, 0x03ffffff, 64, PAGE_READWRITE | PAGE_WRITECOMBINE));
          if (buffer == nullptr) {
            failure = "Not enough contiguous memory for GPU presentation";
            break;
          }
          std::memset(buffer, 0, required);
        }
        if (failure.empty()) {
          active_ = true;
          bufferSize_ = required;
          pitch_ = pitch;
          // xemu reads the descriptor concurrently with MMIO writes. Keep a
          // single DMA window for both buffers so a new address paired with
          // the previous frame's limit is still within bounds.
          const auto highestAddress = std::max(MmGetPhysicalAddress(buffers_[0]), MmGetPhysicalAddress(buffers_[1]));
          VIDEOREG(0x8900) = 0;
          VIDEOREG(0x8908) = static_cast<std::uint32_t>(highestAddress + bufferSize_ - 1U);
          VIDEOREG(0x8140) = 0;
          VIDEOREG(0x8100) = 0x11;
          VIDEOREG(0x8088) = (VIDEOREG(0x8088) & 0xf43ff43fU) | 0x04000400U;
          VIDEOREG(0x808c) = (VIDEOREG(0x808c) & 0xf40ff40fU) | 0x04000400U;
        }
      }
    }
    if (failure.empty()) {
      // The previous submission may latch at this blank. Wait before writing
      // the retired buffer, rather than overwriting it while scanout still uses it.
      if (!wait_for_scanout()) {
        failure = "Timed out waiting for GPU scanout; using software presentation";
      } else if (!pack_overlay_yuy2(frame, buffers_[nextBuffer_], bufferSize_, pitch_)) {
        failure = "Failed to pack the GPU overlay frame";
      }
    }
    if (failure.empty()) {
      if (overlay != nullptr) {
        overlay->draw_yuy2(buffers_[nextBuffer_], bufferSize_, pitch_, frame.width, frame.height, destination);
      }
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
      // Rotate memory buffers through overlay bank zero. xemu composes bank
      // zero only; alternating hardware overlay banks hides every other frame.
      constexpr std::uint32_t offset = 0;
      const auto physical = static_cast<std::uint32_t>(MmGetPhysicalAddress(buffers_[nextBuffer_]));
      VIDEOREG(0x8910 + offset) = 0x1000;
      VIDEOREG(0x8918 + offset) = 0x1000;
      VIDEOREG(0x8920 + offset) = physical;
      // SIZE_IN uses luma pixels; YUY2's two-pixel packing affects byte pitch only.
      VIDEOREG(0x8928 + offset) = static_cast<std::uint32_t>(frame.width) | (static_cast<std::uint32_t>(frame.height) << 16U);
      VIDEOREG(0x8930 + offset) = static_cast<std::uint32_t>(sampling.xHalfPel * 8) | (static_cast<std::uint32_t>(sampling.yHalfPel * 4) << 17U);
      VIDEOREG(0x8938 + offset) = scaling_increment(sourceWidth, destination.width);
      VIDEOREG(0x8940 + offset) = scaling_increment(sourceHeight, destination.height);
      VIDEOREG(0x8948 + offset) = static_cast<std::uint32_t>(destination.x) | (static_cast<std::uint32_t>(destination.y) << 16U);
      VIDEOREG(0x8950 + offset) = static_cast<std::uint32_t>(destination.width) | (static_cast<std::uint32_t>(destination.height) << 16U);
      const std::uint32_t format = static_cast<std::uint32_t>(pitch_) | (1U << 16U) | (frame.matrix == VideoColorMatrix::bt709 ? 1U << 24U : 0U);
      VIDEOREG(0x8958 + offset) = format;
      if (VIDEOREG(0x8958 + offset) != format || VIDEOREG(0x8920 + offset) != physical) {
        failure = "GPU overlay register verification failed";
      } else {
        VIDEOREG(VIDEO_STOP) = 0;
        VIDEOREG(VIDEO_BUFFER) = 1U;
        nextBuffer_ ^= 1U;
        return true;
      }
    }
#else
    if (failure.empty()) {
      failure = "GPU overlay is unavailable on this platform";
    }
#endif
    reset();
    if (error != nullptr) {
      *error = failure;
    }
    return false;
  }
}  // namespace streaming
