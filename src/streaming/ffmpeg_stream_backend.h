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
#include "third-party/moonlight-common-c/src/Limelight.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
struct SwsContext;

namespace streaming {

  /**
   * @brief Owns the FFmpeg decode and SDL presentation state for one stream.
   *
   * The backend exposes Moonlight-compatible callback tables for video and audio,
   * decodes H.264 video and Opus stereo audio with FFmpeg, presents the most
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
    void initialize_callbacks(DECODER_RENDERER_CALLBACKS *videoCallbacks, AUDIO_RENDERER_CALLBACKS *audioCallbacks);

    /**
     * @brief Release all FFmpeg, SDL, and cached frame resources.
     */
    void shutdown();

    /**
     * @brief Render the latest decoded video frame to the supplied renderer.
     *
     * @param renderer SDL renderer used by the stream session.
     * @param screenWidth Current renderer output width.
     * @param screenHeight Current renderer output height.
     * @return True when a decoded frame was available and rendered.
     */
    bool render_latest_video_frame(SDL_Renderer *renderer, int screenWidth, int screenHeight);

    /**
     * @brief Report whether at least one decoded video frame is available.
     *
     * @return True when a decoded frame is ready for presentation.
     */
    bool has_decoded_video() const;

    /**
     * @brief Build a short user-visible media status line.
     *
     * @return Summary of decoded video and queued audio state.
     */
    std::string build_overlay_status_line() const;

    /**
     * @brief Initialize the FFmpeg H.264 decoder for Moonlight video callbacks.
     *
     * @param videoFormat Negotiated Moonlight video format.
     * @param width Negotiated stream width.
     * @param height Negotiated stream height.
     * @param redrawRate Negotiated redraw rate.
     * @param context Moonlight renderer context.
     * @param drFlags Moonlight decoder flags.
     * @return Zero on success.
     */
    int setup_video_decoder(int videoFormat, int width, int height, int redrawRate, void *context, int drFlags);

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
     * @brief Submit one Moonlight decode unit to FFmpeg.
     *
     * @param decodeUnit Moonlight Annex B frame payload.
     * @return Moonlight decoder status code.
     */
    int submit_video_decode_unit(PDECODE_UNIT decodeUnit);

    /**
     * @brief Initialize the FFmpeg Opus decoder and SDL playback device.
     *
     * @param audioConfiguration Negotiated Moonlight audio configuration.
     * @param opusConfig Negotiated Opus multistream parameters.
     * @param context Moonlight audio context.
     * @param arFlags Moonlight audio renderer flags.
     * @return Zero on success.
     */
    int initialize_audio_decoder(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void *context, int arFlags);

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
    void decode_and_play_audio_sample(char *sampleData, int sampleLength);

  private:
    /**
     * @brief Hold the latest IYUV video frame ready for SDL upload.
     */
    struct LatestVideoFrame {
      int width = 0;
      int height = 0;
      int yPitch = 0;
      int uPitch = 0;
      int vPitch = 0;
      std::vector<std::uint8_t> yPlane;
      std::vector<std::uint8_t> uPlane;
      std::vector<std::uint8_t> vPlane;
    };

    /**
     * @brief Hold FFmpeg and SDL state used by the video path.
     */
    struct VideoState {
      AVCodecContext *codecContext = nullptr;
      SwsContext *scaleContext = nullptr;
      AVFrame *decodedFrame = nullptr;
      AVFrame *convertedFrame = nullptr;
      AVPacket *packet = nullptr;
      SDL_Texture *texture = nullptr;
      int textureWidth = 0;
      int textureHeight = 0;
      std::vector<std::uint8_t> convertedBuffer;
      mutable std::mutex frameMutex;
      LatestVideoFrame latestFrame;
      std::atomic<bool> hasFrame = false;
      std::atomic<std::uint64_t> submittedDecodeUnitCount = 0;
      std::atomic<std::uint64_t> decodedFrameCount = 0;
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
      int resampleInputSampleRate = 0;
      int resampleInputSampleFormat = -1;
      int resampleInputChannelCount = 0;
      std::atomic<bool> deviceStarted = false;
      std::atomic<std::uint64_t> queuedAudioBytes = 0;
    };

    VideoState video_ {};
    AudioState audio_ {};
  };

}  // namespace streaming
