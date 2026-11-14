/**
 * @file src/streaming/ffmpeg_stream_backend.h
 * @brief Declares the FFmpeg-backed streaming decode backend for Xbox sessions.
 */
#pragma once

// standard includes
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// lib includes
#include <SDL.h>

// local includes
#include "src/streaming/aligned_media_buffer.h"
#include "src/streaming/pvideo_presenter.h"
#include "src/streaming/stats_overlay.h"
#include "third-party/moonlight-common-c/src/Limelight.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
struct SwsContext;

namespace streaming {

  /**
   * @brief Opaque SDL thread context payload.
   */
  using SdlThreadContext = void;

  /**
   * @brief Owns the FFmpeg decode and SDL presentation state for one stream.
   *
   * The backend exposes Moonlight-compatible callback tables for video and audio,
   * decodes negotiated video and Opus stereo audio with FFmpeg, presents the most
   * recent decoded frame through SDL, and queues decoded PCM samples to SDL audio.
   */
  class FfmpegStreamBackend {
  public:
    /**
     * @brief Construct an empty backend.
     */
    FfmpegStreamBackend() = default;

    /**
     * @brief Destroy the backend and release all resources.
     */
    ~FfmpegStreamBackend();

    FfmpegStreamBackend(const FfmpegStreamBackend &) = delete;
    FfmpegStreamBackend &operator=(const FfmpegStreamBackend &) = delete;

    /**
     * @brief Populate Moonlight callback tables for this backend.
     *
     * @param videoCallbacks Output video callback table.
     * @param audioCallbacks Output audio callback table.
     */
    void initialize_callbacks(DECODER_RENDERER_CALLBACKS *videoCallbacks, AUDIO_RENDERER_CALLBACKS *audioCallbacks) const;

    /**
     * @brief Configure whether streamed audio should be decoded and played locally.
     *
     * @param enabled True to decode and play Opus audio on the Xbox.
     */
    void set_audio_playback_enabled(bool enabled);

    /**
     * @brief Release all FFmpeg, SDL, and cached frame resources.
     */
    void shutdown();

    /**
     * @brief Return retained presentation and decoder acceleration diagnostics.
     * @return Snapshot to capture on the render thread before session cleanup.
     */
    VideoAccelerationReport acceleration_report() const;

    /**
     * @brief Read cumulative performance telemetry.
     *
     * @return Concurrent video counter snapshot.
     */
    VideoStatisticsCounters performance_counters() const;
    /**
     * @brief Describe the active presentation path.
     *
     * @return GPU, Software or Waiting.
     */
    std::string presentation_path() const;
    /**
     * @brief Replace the shared GPU/software statistics bitmap on the render thread.
     * @param width Bitmap width.
     * @param height Bitmap height.
     * @param grayscale Packed grayscale pixels with an opaque background.
     */
    void set_statistics_bitmap(int width, int height, std::vector<std::uint8_t> grayscale);

    /**
     * @brief Render the latest decoded video frame to the supplied renderer.
     *
     * @param renderer SDL renderer used by the stream session.
     * @param screenWidth Current renderer output width.
     * @param screenHeight Current renderer output height.
     * @param allowDirectFramebuffer True when platform-specific direct presentation may bypass SDL's renderer.
     * @return True when a decoded frame was available and rendered.
     */
    bool render_latest_video_frame(SDL_Renderer *renderer, int screenWidth, int screenHeight, bool allowDirectFramebuffer = true);

    /**
     * @brief Restore CPU framebuffer output before drawing shutdown or summary UI on the render thread.
     */
    void stop_video_presentation();

    /**
     * @brief Report whether at least one decoded video frame is available.
     *
     * @return True when a decoded frame is ready for presentation.
     */
    bool has_decoded_video() const;

    /**
     * @brief Return the human-readable active video decoder name.
     *
     * @return Selected video decoder name, or a generic label before setup.
     */
    std::string active_video_decoder_name() const;

    /**
     * @brief Report whether a newly decoded video frame has not been presented yet.
     *
     * @return True when rendering would present a newer decoded frame.
     */
    bool has_unrendered_video_frame() const;

    /**
     * @brief Return how long it has been since FFmpeg published a decoded video frame.
     *
     * @param nowMicroseconds Current platform monotonic time.
     * @return Milliseconds since the last decoded frame, or zero before a frame is published.
     */
    std::uint64_t milliseconds_since_last_decoded_video_frame(std::uint64_t nowMicroseconds) const;

    /**
     * @brief Build a short user-visible media status line.
     *
     * @return Summary of decoded video and queued audio state.
     */
    std::string build_overlay_status_line() const;

    /**
     * @brief Initialize the FFmpeg decoder for Moonlight video callbacks.
     *
     * @param videoFormat Negotiated Moonlight video format.
     * @param width Negotiated stream width.
     * @param height Negotiated stream height.
     * @param redrawRate Negotiated redraw rate.
     * @param drFlags Moonlight decoder flags.
     * @return Zero on success.
     */
    int setup_video_decoder(int videoFormat, int width, int height, int redrawRate, int drFlags);

    /**
     * @brief Start the video decode path.
     */
    void start_video_decoder();

    /**
     * @brief Stop the video decode path.
     */
    void stop_video_decoder();

    /**
     * @brief Clean up all FFmpeg video decode resources.
     */
    void cleanup_video_decoder();

    /**
     * @brief Initialize the FFmpeg Opus decoder and SDL playback device.
     *
     * @param audioConfiguration Negotiated Moonlight audio configuration.
     * @param opusConfig Negotiated Opus multistream parameters.
     * @param arFlags Moonlight audio renderer flags.
     * @return Zero on success.
     */
    int initialize_audio_decoder(int audioConfiguration, const OPUS_MULTISTREAM_CONFIGURATION *opusConfig, int arFlags);

    /**
     * @brief Start SDL audio playback.
     */
    void start_audio_playback();

    /**
     * @brief Stop SDL audio playback.
     */
    void stop_audio_playback();

    /**
     * @brief Clean up all FFmpeg audio decode resources.
     */
    void cleanup_audio_decoder();

    /**
     * @brief Ensure the audio resampler matches the current decoded frame.
     *
     * @return True when the resampler is ready for audio conversion.
     */
    bool ensure_audio_resampler();

    /**
     * @brief Decode and queue one Moonlight Opus audio payload.
     *
     * @param sampleData Encoded Opus payload.
     * @param sampleLength Encoded payload size in bytes.
     */
    void decode_and_play_audio_sample(const char *sampleData, int sampleLength);

  private:
    /**
     * @brief Present a frame while the public wrapper records submission time.
     * @param renderer SDL video renderer.
     * @param screenWidth Output width.
     * @param screenHeight Output height.
     * @param allowDirectFramebuffer Permit Xbox GPU or direct framebuffer presentation.
     * @return True after successful presentation.
     */
    bool render_video_frame(SDL_Renderer *renderer, int screenWidth, int screenHeight, bool allowDirectFramebuffer);
    /**
     * @brief Draw the cached panel when the SDL presentation fallback is used.
     * @param renderer SDL video renderer.
     * @param destination Output video rectangle.
     */
    void render_sdl_statistics(SDL_Renderer *renderer, const SDL_Rect &destination);
    /**
     * @brief Record a retrieved frame before any client discard.
     *
     * @param unit Completed network frame.
     */
    void record_received_unit(PDECODE_UNIT unit);
    /**
     * @brief Entry point used by the SDL video decode worker thread.
     *
     * @param context Backend instance.
     * @return Zero when the worker exits normally.
     */
    static int run_video_decode_thread_trampoline(SdlThreadContext *context);

    /**
     * @brief Pull Moonlight decode units and feed them into FFmpeg.
     *
     * @return Zero when the worker exits normally.
     */
    int run_video_decode_thread();

    /**
     * @brief Drop queued decode units so the decoder works on the newest frame.
     *
     * @param frameHandle Current frame handle, replaced with the newest queued handle.
     * @param decodeUnit Current decode unit, replaced with the newest queued unit.
     * @return Number of decode units discarded.
     */
    int drop_queued_video_decode_units(VIDEO_FRAME_HANDLE *frameHandle, PDECODE_UNIT *decodeUnit);

    /**
     * @brief Drop stale queued video decode units and prepare the newest unit.
     *
     * @param frameHandle Current frame handle, replaced with the newest queued handle.
     * @param decodeUnit Current decode unit, replaced with the newest queued unit.
     * @return Moonlight decoder status after stale-frame handling.
     */
    int prepare_video_decode_unit(VIDEO_FRAME_HANDLE *frameHandle, PDECODE_UNIT *decodeUnit);

    /**
     * @brief Decode one Moonlight video decode unit on the video worker thread.
     *
     * @param decodeUnit Moonlight Annex B frame payload.
     * @return Moonlight decoder status code.
     */
    int decode_video_decode_unit(PDECODE_UNIT decodeUnit);

    /**
     * @brief Drain decoded frames currently available from FFmpeg.
     *
     * @return FFmpeg receive result.
     */
    int receive_available_video_frames();

    /**
     * @brief Publish one decoded or converted frame for SDL presentation.
     *
     * @param frameToPresent FFmpeg frame in IYUV-compatible layout.
     * @return True when the frame was copied into the presentation buffer.
     */
    bool publish_video_frame(const AVFrame *frameToPresent);

    /**
     * @brief Present the latest decoded frame through a platform-specific direct framebuffer path.
     *
     * @param screenWidth Current framebuffer width.
     * @param screenHeight Current framebuffer height.
     * @return True when the frame was presented without SDL renderer involvement.
     */
    bool render_latest_video_frame_to_framebuffer(int screenWidth, int screenHeight);

    /**
     * @brief Hold the latest IYUV video frame ready for SDL upload.
     */
    struct LatestVideoFrame {
      int width = 0;
      int height = 0;
      int yPitch = 0;
      int uPitch = 0;
      int vPitch = 0;
      AlignedMediaBuffer yPlane;  ///< Aligned, padded luma data.
      AlignedMediaBuffer uPlane;  ///< Aligned, padded blue chroma data.
      AlignedMediaBuffer vPlane;  ///< Aligned, padded red chroma data.
      VideoColorMatrix matrix = VideoColorMatrix::bt601;  ///< Color matrix preserved across frame publication.
      bool fullRange = false;  ///< Source uses full-range YUV.
      bool interlaced = false;  ///< Source contains interlaced fields.
    };

    /**
     * @brief Hold FFmpeg and SDL state used by the video path.
     */
    struct VideoState {  // NOSONAR(cpp:S1820) Owned video pipeline state intentionally groups FFmpeg, SDL, queue, and metric lifetimes.
      AVCodecContext *codecContext = nullptr;
      SwsContext *scaleContext = nullptr;
      SwsContext *presentScaleContext = nullptr;
      AVFrame *decodedFrame = nullptr;
      AVFrame *convertedFrame = nullptr;
      AVPacket *packet = nullptr;
      const char *codecName = "video";  ///< Human-readable active video decoder name.
      SDL_Texture *texture = nullptr;
      SDL_Thread *decoderThread = nullptr;
      int textureWidth = 0;
      int textureHeight = 0;
      std::uint64_t latestFrameVersion = 0;
      std::uint64_t renderedFrameVersion = 0;
      AlignedMediaBuffer convertedBuffer;  ///< Aligned planar conversion storage.
      std::vector<std::uint8_t> packetBuffer;

      /**
       * @brief Return the mutex guarding decoded frame publication.
       *
       * @return Mutex used to exchange frames between decode and render threads.
       */
      [[nodiscard]] std::mutex &frame_mutex() const {
        return frameMutex_;
      }

    private:
      mutable std::mutex frameMutex_;  ///< Guards latest decoded frame publication between worker and render threads.

    public:
      SDL_Rect directFramebufferDestination {0, 0, 0, 0};
      LatestVideoFrame latestFrame;
      LatestVideoFrame decodeFrame;
      LatestVideoFrame renderFrame;
      bool directFramebufferCleared = false;
      std::atomic<bool> decoderStopRequested = false;
      std::atomic<bool> hasFrame = false;
      std::atomic<std::uint64_t> publishedFrameVersion = 0;
      std::atomic<std::uint64_t> submittedDecodeUnitCount = 0;
      std::atomic<std::uint64_t> decodedFrameCount = 0;
      std::atomic<std::uint64_t> droppedDecodeUnitCount = 0;
      std::atomic<std::uint64_t> lastDecodeQueueUs = 0;
      std::atomic<std::uint64_t> lastReceiveAgeUs = 0;
      std::atomic<std::uint64_t> lastDecodedFrameUs = 0;
      std::atomic<int> lastDecodeFrameNumber = 0;
    };

    /**
     * @brief Hold FFmpeg and SDL state used by the audio path.
     */
    struct AudioState {
      AVCodecContext *codecContext = nullptr;
      SwrContext *resampleContext = nullptr;
      AVFrame *decodedFrame = nullptr;
      AVPacket *packet = nullptr;
      SDL_AudioDeviceID deviceId = 0;
      SDL_AudioSpec obtainedSpec {};
      AlignedMediaBuffer outputBuffer;  ///< Aligned resampler output.
      int resampleInputSampleRate = 0;
      int resampleInputSampleFormat = -1;
      int resampleInputChannelCount = 0;
      std::atomic<bool> deviceStarted = false;
      std::atomic<std::uint64_t> queuedAudioBytes = 0;
    };

    VideoState video_ {};
    AudioState audio_ {};
    PvideoPresenter presenter_;  ///< Main-thread GPU presentation resources.
    VideoOverlay statisticsOverlay_;  ///< Shared cached panel for all presentation paths.
    VideoStatistics statistics_;  ///< Cumulative decode and presentation telemetry.
    SDL_Texture *statisticsTexture_ = nullptr;  ///< Cached panel texture for SDL's final fallback.
    bool statisticsBitmapChanged_ = false;  ///< A panel update requires refreshing a paused video frame.
    bool statisticsTextureChanged_ = false;  ///< SDL texture requires the current bitmap.
    VideoAccelerationReport acceleration_;  ///< Session diagnostics survive decoder cleanup.
    bool overlayFailed_ = false;  ///< Avoid repeating a known failing hardware path this session.
    std::atomic<bool> audioPlaybackEnabled_ = false;
  };

}  // namespace streaming
