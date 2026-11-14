/**
 * @file src/streaming/pvideo_presenter.h
 * @brief Owns the NV2A overlay independently of SDL framebuffer rendering.
 */
#pragma once

#include "src/streaming/video_acceleration.h"
#include "src/streaming/video_overlay.h"

namespace streaming {
  /**
   * @brief Optional source region for isolated half-pixel prediction experiments.
   */
  struct OverlaySamplingRegion {
    int xHalfPel = 0;  ///< Horizontal source origin in half-pixel units.
    int yHalfPel = 0;  ///< Vertical source origin in half-pixel units.
    int width = 0;  ///< Source extent; zero selects the whole frame.
    int height = 0;  ///< Source extent; zero selects the whole frame.
  };

  /**
   * @brief Double-buffered, main-thread-only NV2A YUV overlay presenter.
   *
   * Supports progressive limited-range BT.601/BT.709 YUV420 with a compatible
   * 32-bit output mode. Callers select software presentation when present()
   * returns false. Successful register submission does not verify the image
   * displayed by the emulator or console.
   *
   * @see https://github.com/abaire/nxdk_pgraph_tests/blob/main/src/pvideo_control.cpp
   * @see https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/gl/display.c
   */
  class PvideoPresenter {
  public:
    /**
     * @brief Construct an inactive overlay owner.
     */
    PvideoPresenter() = default;
    /**
     * @brief Stop overlay DMA before releasing its contiguous buffers.
     */
    ~PvideoPresenter();
    PvideoPresenter(const PvideoPresenter &) = delete;
    PvideoPresenter &operator=(const PvideoPresenter &) = delete;

    /**
     * @brief Pack and submit one frame, or return an explicit fallback reason.
     *
     * @param frame Source YUV420 data.
     * @param destination Letterboxed output rectangle.
     * @param error Receives the reason acceleration could not be used.
     * @param sampling Optional crop used by the standalone prediction experiment.
     * @param overlay Optional cached statistics bitmap, composited before GPU scanout.
     *
     * @return True after the overlay registers have accepted the new buffer.
     */
    bool present(const Yuv420FrameView &frame, const VideoRectangle &destination, std::string *error, const OverlaySamplingRegion &sampling = {}, VideoOverlay *overlay = nullptr);

    /**
     * @brief Stop DMA and release buffers only after a bounded scanout synchronization.
     *
     * @return False when buffers had to be quarantined after a scanout timeout.
     */
    bool reset();

    /**
     * @brief Return whether the overlay currently owns presentation resources.
     * @return True after successful initialization until reset.
     */
    bool active() const;

  private:
    /**
     * @brief Select a GPU-rendered scanout surface while retaining SDL's separate CPU framebuffer.
     *
     * @param error Receives an initialization or GPU completion failure.
     * @return True after the background clear has completed.
     */
    bool initialize_scanout_surface(std::string *error);

    std::array<std::uint8_t *, 2> buffers_ {};  ///< Contiguous write-combined YUY2 storage.
    std::array<std::uint32_t, 28> savedRegisters_ {};  ///< Overlay state restored on teardown.
    std::array<std::uint8_t, 4> savedCrtcRegisters_ {};  ///< Legacy scanout controls changed by pbkit.
    std::uint32_t savedCrtcConfig_ = 0;  ///< Scanout start-address latching mode before pbkit.
    bool scanoutStateSaved_ = false;  ///< Legacy scanout and overlay state must be restored, including initialization failures.
    std::size_t bufferSize_ = 0;  ///< Allocated size of each DMA buffer.
    int pitch_ = 0;  ///< Packed source stride.
    unsigned int nextBuffer_ = 0;  ///< Buffer not currently being scanned out.
    bool active_ = false;  ///< Registers and buffers are owned by this presenter.
    bool surfaceActive_ = false;  ///< pbkit owns the GPU context and vblank counter for the scanout surface.
  };
}  // namespace streaming
