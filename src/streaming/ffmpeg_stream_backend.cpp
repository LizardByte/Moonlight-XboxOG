/**
 * @file src/streaming/ffmpeg_stream_backend.cpp
 * @brief Implements the FFmpeg-backed streaming decode backend for Xbox sessions.
 */
#include "src/streaming/ffmpeg_stream_backend.h"

// standard includes
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// lib includes
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// local includes
#include "src/logging/logger.h"

namespace {

  constexpr Uint32 STREAM_OVERLAY_AUDIO_QUEUE_LIMIT_MS = 250U;
  constexpr std::uint64_t STREAM_VIDEO_SUBMISSION_LOG_INTERVAL = 120;

  streaming::FfmpegStreamBackend *g_active_video_backend = nullptr;
  streaming::FfmpegStreamBackend *g_active_audio_backend = nullptr;
  std::once_flag g_ffmpeg_logging_once;

  /**
   * @brief Convert an FFmpeg error code into readable text.
   *
   * @param errorCode Negative FFmpeg return code.
   * @return User-readable FFmpeg error text.
   */
  std::string describe_ffmpeg_error(int errorCode) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer {};
    av_strerror(errorCode, buffer.data(), buffer.size());
    return std::string(buffer.data());
  }

  /**
   * @brief Remove trailing CR and LF characters from an FFmpeg log line.
   *
   * @param message Candidate log line.
   * @return Trimmed log line.
   */
  std::string trim_ffmpeg_log_line(std::string message) {
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
      message.pop_back();
    }
    return message;
  }

  /**
   * @brief Map one FFmpeg log level to the project logger severity.
   *
   * @param ffmpegLevel FFmpeg log level constant.
   * @return Corresponding project log level.
   */
  logging::LogLevel map_ffmpeg_log_level(int ffmpegLevel) {
    if (ffmpegLevel <= AV_LOG_ERROR) {
      return logging::LogLevel::error;
    }
    if (ffmpegLevel <= AV_LOG_WARNING) {
      return logging::LogLevel::warning;
    }
    if (ffmpegLevel <= AV_LOG_INFO) {
      return logging::LogLevel::info;
    }
    if (ffmpegLevel <= AV_LOG_VERBOSE) {
      return logging::LogLevel::debug;
    }
    return logging::LogLevel::trace;
  }

  /**
   * @brief Forward FFmpeg's internal logs into the project logger.
   *
   * @param avClassContext FFmpeg component instance emitting the line.
   * @param level FFmpeg log level.
   * @param format `printf`-style format string.
   * @param arguments Variadic arguments matching `format`.
   */
  void ffmpeg_log_callback(void *avClassContext, int level, const char *format, va_list arguments) {
    std::array<char, 1024> buffer {};
    int printPrefix = 1;
    va_list argumentsCopy;
    va_copy(argumentsCopy, arguments);
    const int formattedLength = av_log_format_line2(avClassContext, level, format, argumentsCopy, buffer.data(), static_cast<int>(buffer.size()), &printPrefix);
    va_end(argumentsCopy);
    if (formattedLength < 0) {
      return;
    }

    std::string message;
    if (static_cast<std::size_t>(formattedLength) < buffer.size()) {
      message.assign(buffer.data(), static_cast<std::size_t>(formattedLength));
    } else {
      message.assign(buffer.data());
    }
    message = trim_ffmpeg_log_line(std::move(message));
    if (message.empty()) {
      return;
    }

    logging::log(map_ffmpeg_log_level(level), "ffmpeg", std::move(message));
  }

  /**
   * @brief Install the shared FFmpeg log callback once for the process.
   */
  void ensure_ffmpeg_logging_installed() {
    std::call_once(g_ffmpeg_logging_once, []() {
      av_log_set_level(AV_LOG_VERBOSE);
      av_log_set_callback(ffmpeg_log_callback);
    });
  }

  /**
   * @brief Return the number of bytes generated per second for the SDL audio format.
   *
   * @param audioSpec SDL audio format in use for playback.
   * @return Number of bytes generated per second.
   */
  Uint32 audio_bytes_per_second(const SDL_AudioSpec &audioSpec) {
    return static_cast<Uint32>(audioSpec.freq) * static_cast<Uint32>(audioSpec.channels) * (SDL_AUDIO_BITSIZE(audioSpec.format) / 8U);
  }

  /**
   * @brief Compute a centered destination rectangle that preserves aspect ratio.
   *
   * @param screenWidth Renderer output width.
   * @param screenHeight Renderer output height.
   * @param frameWidth Decoded video width.
   * @param frameHeight Decoded video height.
   * @return Letterboxed destination rectangle.
   */
  SDL_Rect build_letterboxed_destination(int screenWidth, int screenHeight, int frameWidth, int frameHeight) {
    if (screenWidth <= 0 || screenHeight <= 0 || frameWidth <= 0 || frameHeight <= 0) {
      return SDL_Rect {0, 0, 0, 0};
    }

    const double screenAspect = static_cast<double>(screenWidth) / static_cast<double>(screenHeight);
    const double frameAspect = static_cast<double>(frameWidth) / static_cast<double>(frameHeight);

    int destinationWidth = screenWidth;
    int destinationHeight = screenHeight;
    if (frameAspect > screenAspect) {
      destinationHeight = std::max(1, static_cast<int>(static_cast<double>(screenWidth) / frameAspect));
    } else {
      destinationWidth = std::max(1, static_cast<int>(static_cast<double>(screenHeight) * frameAspect));
    }

    return SDL_Rect {
      (screenWidth - destinationWidth) / 2,
      (screenHeight - destinationHeight) / 2,
      destinationWidth,
      destinationHeight,
    };
  }

  /**
   * @brief Create FFmpeg Opus extradata for the stereo Moonlight stream.
   *
   * @param channelCount Negotiated channel count.
   * @param sampleRate Negotiated sample rate.
   * @param codecContext Audio codec context that will own the extradata.
   * @return True when the extradata was allocated successfully.
   */
  bool configure_opus_extradata(int channelCount, int sampleRate, AVCodecContext *codecContext) {
    if (codecContext == nullptr || channelCount <= 0 || channelCount > 2) {
      return false;
    }

    std::array<std::uint8_t, 19> opusHead {
      static_cast<std::uint8_t>('O'),
      static_cast<std::uint8_t>('p'),
      static_cast<std::uint8_t>('u'),
      static_cast<std::uint8_t>('s'),
      static_cast<std::uint8_t>('H'),
      static_cast<std::uint8_t>('e'),
      static_cast<std::uint8_t>('a'),
      static_cast<std::uint8_t>('d'),
      1U,
      static_cast<std::uint8_t>(channelCount),
      0U,
      0U,
      static_cast<std::uint8_t>(sampleRate & 0xFF),
      static_cast<std::uint8_t>((sampleRate >> 8) & 0xFF),
      static_cast<std::uint8_t>((sampleRate >> 16) & 0xFF),
      static_cast<std::uint8_t>((sampleRate >> 24) & 0xFF),
      0U,
      0U,
      0U,
    };

    codecContext->extradata = static_cast<std::uint8_t *>(av_mallocz(opusHead.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (codecContext->extradata == nullptr) {
      return false;
    }

    codecContext->extradata_size = static_cast<int>(opusHead.size());
    std::memcpy(codecContext->extradata, opusHead.data(), opusHead.size());
    return true;
  }

  int on_video_setup(int videoFormat, int width, int height, int redrawRate, void *context, int drFlags) {
    auto *backend = static_cast<streaming::FfmpegStreamBackend *>(context);
    if (backend == nullptr) {
      return -1;
    }

    g_active_video_backend = backend;
    return backend->setup_video_decoder(videoFormat, width, height, redrawRate, context, drFlags);
  }

  void on_video_start() {
    if (g_active_video_backend != nullptr) {
      g_active_video_backend->start_video_decoder();
    }
  }

  void on_video_stop() {
    if (g_active_video_backend != nullptr) {
      g_active_video_backend->stop_video_decoder();
    }
  }

  void on_video_cleanup() {
    if (g_active_video_backend != nullptr) {
      g_active_video_backend->cleanup_video_decoder();
      g_active_video_backend = nullptr;
    }
  }

  int on_video_submit_decode_unit(PDECODE_UNIT decodeUnit) {
    if (g_active_video_backend == nullptr) {
      return DR_NEED_IDR;
    }

    return g_active_video_backend->submit_video_decode_unit(decodeUnit);
  }

  int on_audio_init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void *context, int arFlags) {
    auto *backend = static_cast<streaming::FfmpegStreamBackend *>(context);
    if (backend == nullptr) {
      return -1;
    }

    g_active_audio_backend = backend;
    return backend->initialize_audio_decoder(audioConfiguration, opusConfig, context, arFlags);
  }

  void on_audio_start() {
    if (g_active_audio_backend != nullptr) {
      g_active_audio_backend->start_audio_playback();
    }
  }

  void on_audio_stop() {
    if (g_active_audio_backend != nullptr) {
      g_active_audio_backend->stop_audio_playback();
    }
  }

  void on_audio_cleanup() {
    if (g_active_audio_backend != nullptr) {
      g_active_audio_backend->cleanup_audio_decoder();
      g_active_audio_backend = nullptr;
    }
  }

  void on_audio_decode_and_play_sample(char *sampleData, int sampleLength) {
    if (g_active_audio_backend != nullptr) {
      g_active_audio_backend->decode_and_play_audio_sample(sampleData, sampleLength);
    }
  }

}  // namespace

namespace streaming {

  FfmpegStreamBackend::~FfmpegStreamBackend() {
    shutdown();
  }

  void FfmpegStreamBackend::initialize_callbacks(DECODER_RENDERER_CALLBACKS *videoCallbacks, AUDIO_RENDERER_CALLBACKS *audioCallbacks) {
    ensure_ffmpeg_logging_installed();

    if (videoCallbacks != nullptr) {
      LiInitializeVideoCallbacks(videoCallbacks);
      videoCallbacks->setup = on_video_setup;
      videoCallbacks->start = on_video_start;
      videoCallbacks->stop = on_video_stop;
      videoCallbacks->cleanup = on_video_cleanup;
      videoCallbacks->submitDecodeUnit = on_video_submit_decode_unit;
      videoCallbacks->capabilities = 0;
    }

    if (audioCallbacks != nullptr) {
      LiInitializeAudioCallbacks(audioCallbacks);
      audioCallbacks->init = on_audio_init;
      audioCallbacks->start = on_audio_start;
      audioCallbacks->stop = on_audio_stop;
      audioCallbacks->cleanup = on_audio_cleanup;
      audioCallbacks->decodeAndPlaySample = on_audio_decode_and_play_sample;
      audioCallbacks->capabilities = 0;
    }
  }

  void FfmpegStreamBackend::shutdown() {
    cleanup_audio_decoder();
    cleanup_video_decoder();
  }

  bool FfmpegStreamBackend::has_decoded_video() const {
    return video_.hasFrame.load();
  }

  std::string FfmpegStreamBackend::build_overlay_status_line() const {
    std::string audioState = audio_.deviceId != 0 ? std::to_string(SDL_GetQueuedAudioSize(audio_.deviceId)) + " queued audio bytes" : "audio idle";
    return std::string("Video units: ") + std::to_string(video_.submittedDecodeUnitCount.load()) + " submitted / " + std::to_string(video_.decodedFrameCount.load()) + " decoded | " + audioState;
  }

  bool FfmpegStreamBackend::render_latest_video_frame(SDL_Renderer *renderer, int screenWidth, int screenHeight) {
    if (renderer == nullptr || !video_.hasFrame.load()) {
      return false;
    }

    LatestVideoFrame frameSnapshot {};
    {
      std::lock_guard<std::mutex> lock(video_.frameMutex);
      if (video_.latestFrame.width <= 0 || video_.latestFrame.height <= 0) {
        return false;
      }
      frameSnapshot = video_.latestFrame;
    }

    if (video_.texture == nullptr || video_.textureWidth != frameSnapshot.width || video_.textureHeight != frameSnapshot.height) {
      if (video_.texture != nullptr) {
        SDL_DestroyTexture(video_.texture);
        video_.texture = nullptr;
      }

      video_.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, frameSnapshot.width, frameSnapshot.height);
      if (video_.texture == nullptr) {
        logging::error("stream", std::string("SDL_CreateTexture failed for video presentation: ") + SDL_GetError());
        return false;
      }

      video_.textureWidth = frameSnapshot.width;
      video_.textureHeight = frameSnapshot.height;
    }

    if (SDL_UpdateYUVTexture(
          video_.texture,
          nullptr,
          reinterpret_cast<const Uint8 *>(frameSnapshot.yPlane.data()),
          frameSnapshot.yPitch,
          reinterpret_cast<const Uint8 *>(frameSnapshot.uPlane.data()),
          frameSnapshot.uPitch,
          reinterpret_cast<const Uint8 *>(frameSnapshot.vPlane.data()),
          frameSnapshot.vPitch
        ) != 0) {
      logging::error("stream", std::string("SDL_UpdateYUVTexture failed during video presentation: ") + SDL_GetError());
      return false;
    }

    const SDL_Rect destination = build_letterboxed_destination(screenWidth, screenHeight, frameSnapshot.width, frameSnapshot.height);
    return SDL_RenderCopy(renderer, video_.texture, nullptr, &destination) == 0;
  }

  int FfmpegStreamBackend::setup_video_decoder(int videoFormat, int width, int height, int redrawRate, void *context, int drFlags) {
    (void) width;
    (void) height;
    (void) redrawRate;
    (void) context;
    (void) drFlags;

    cleanup_video_decoder();

    if ((videoFormat & VIDEO_FORMAT_MASK_H264) == 0) {
      logging::error("stream", "The FFmpeg backend currently supports only H.264 video streams");
      return -1;
    }

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
      logging::error("stream", "FFmpeg did not provide an H.264 decoder");
      return -1;
    }

    video_.codecContext = avcodec_alloc_context3(codec);
    video_.decodedFrame = av_frame_alloc();
    video_.convertedFrame = av_frame_alloc();
    video_.packet = av_packet_alloc();
    if (video_.codecContext == nullptr || video_.decodedFrame == nullptr || video_.convertedFrame == nullptr || video_.packet == nullptr) {
      logging::error("stream", "Failed to allocate FFmpeg video decoder resources");
      cleanup_video_decoder();
      return -1;
    }

    video_.codecContext->thread_count = 1;
    video_.codecContext->thread_type = 0;
    const int openResult = avcodec_open2(video_.codecContext, codec, nullptr);
    if (openResult < 0) {
      logging::error("stream", std::string("avcodec_open2 failed for H.264: ") + describe_ffmpeg_error(openResult));
      cleanup_video_decoder();
      return openResult;
    }

    return 0;
  }

  void FfmpegStreamBackend::start_video_decoder() {
  }

  void FfmpegStreamBackend::stop_video_decoder() {
  }

  void FfmpegStreamBackend::cleanup_video_decoder() {
    if (video_.texture != nullptr) {
      SDL_DestroyTexture(video_.texture);
      video_.texture = nullptr;
    }
    video_.textureWidth = 0;
    video_.textureHeight = 0;

    if (video_.packet != nullptr) {
      av_packet_free(&video_.packet);
    }
    if (video_.decodedFrame != nullptr) {
      av_frame_free(&video_.decodedFrame);
    }
    if (video_.convertedFrame != nullptr) {
      av_frame_free(&video_.convertedFrame);
    }
    if (video_.codecContext != nullptr) {
      avcodec_free_context(&video_.codecContext);
    }
    if (video_.scaleContext != nullptr) {
      sws_freeContext(video_.scaleContext);
      video_.scaleContext = nullptr;
    }

    video_.convertedBuffer.clear();
    {
      std::lock_guard<std::mutex> lock(video_.frameMutex);
      video_.latestFrame = LatestVideoFrame {};
    }
    video_.hasFrame.store(false);
    video_.submittedDecodeUnitCount.store(0);
    video_.decodedFrameCount.store(0);
  }

  int FfmpegStreamBackend::submit_video_decode_unit(PDECODE_UNIT decodeUnit) {
    if (decodeUnit == nullptr || video_.codecContext == nullptr || video_.packet == nullptr || video_.decodedFrame == nullptr || video_.convertedFrame == nullptr) {
      return DR_NEED_IDR;
    }

    const int packetResult = av_new_packet(video_.packet, decodeUnit->fullLength);
    if (packetResult < 0) {
      logging::error("stream", std::string("av_new_packet failed for video decode: ") + describe_ffmpeg_error(packetResult));
      return DR_NEED_IDR;
    }

    int offset = 0;
    for (PLENTRY buffer = decodeUnit->bufferList; buffer != nullptr; buffer = buffer->next) {
      if (buffer->length <= 0) {
        continue;
      }

      std::memcpy(video_.packet->data + offset, buffer->data, static_cast<std::size_t>(buffer->length));
      offset += buffer->length;
    }
    video_.packet->size = offset;

    const std::uint64_t submittedDecodeUnitCount = video_.submittedDecodeUnitCount.fetch_add(1) + 1;
    if (submittedDecodeUnitCount == 1 || (submittedDecodeUnitCount % STREAM_VIDEO_SUBMISSION_LOG_INTERVAL) == 0) {
      const std::uint64_t receiveWindowUs = decodeUnit->enqueueTimeUs >= decodeUnit->receiveTimeUs ? decodeUnit->enqueueTimeUs - decodeUnit->receiveTimeUs : 0;
      logging::debug(
        "stream",
        std::string("Submitted video decode unit ") + std::to_string(submittedDecodeUnitCount) + " frame=" + std::to_string(decodeUnit->frameNumber) + " bytes=" + std::to_string(decodeUnit->fullLength) + " queue_us=" + std::to_string(receiveWindowUs)
      );
    }

    const auto receive_available_frames = [&]() -> int {
      while (true) {
        const int receiveResult = avcodec_receive_frame(video_.codecContext, video_.decodedFrame);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
          return receiveResult;
        }
        if (receiveResult < 0) {
          logging::warn("stream", std::string("avcodec_receive_frame failed for H.264: ") + describe_ffmpeg_error(receiveResult));
          return receiveResult;
        }

        AVFrame *frameToPresent = video_.decodedFrame;
        if (video_.decodedFrame->format != AV_PIX_FMT_YUV420P) {
          video_.scaleContext = sws_getCachedContext(
            video_.scaleContext,
            video_.decodedFrame->width,
            video_.decodedFrame->height,
            static_cast<AVPixelFormat>(video_.decodedFrame->format),
            video_.decodedFrame->width,
            video_.decodedFrame->height,
            AV_PIX_FMT_YUV420P,
            SWS_FAST_BILINEAR,
            nullptr,
            nullptr,
            nullptr
          );
          if (video_.scaleContext == nullptr) {
            logging::warn("stream", "sws_getCachedContext failed for video conversion");
            av_frame_unref(video_.decodedFrame);
            return AVERROR(EINVAL);
          }

          const int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, video_.decodedFrame->width, video_.decodedFrame->height, 1);
          if (bufferSize <= 0) {
            logging::warn("stream", "av_image_get_buffer_size failed for converted video frame");
            av_frame_unref(video_.decodedFrame);
            return AVERROR(EINVAL);
          }

          video_.convertedBuffer.resize(static_cast<std::size_t>(bufferSize));
          av_frame_unref(video_.convertedFrame);
          video_.convertedFrame->format = AV_PIX_FMT_YUV420P;
          video_.convertedFrame->width = video_.decodedFrame->width;
          video_.convertedFrame->height = video_.decodedFrame->height;
          const int fillResult = av_image_fill_arrays(
            video_.convertedFrame->data,
            video_.convertedFrame->linesize,
            video_.convertedBuffer.data(),
            AV_PIX_FMT_YUV420P,
            video_.decodedFrame->width,
            video_.decodedFrame->height,
            1
          );
          if (fillResult < 0) {
            logging::warn("stream", std::string("av_image_fill_arrays failed for converted frame: ") + describe_ffmpeg_error(fillResult));
            av_frame_unref(video_.decodedFrame);
            return fillResult;
          }

          sws_scale(
            video_.scaleContext,
            video_.decodedFrame->data,
            video_.decodedFrame->linesize,
            0,
            video_.decodedFrame->height,
            video_.convertedFrame->data,
            video_.convertedFrame->linesize
          );
          frameToPresent = video_.convertedFrame;
        }

        LatestVideoFrame nextFrame {};
        nextFrame.width = frameToPresent->width;
        nextFrame.height = frameToPresent->height;
        nextFrame.yPitch = frameToPresent->linesize[0];
        nextFrame.uPitch = frameToPresent->linesize[1];
        nextFrame.vPitch = frameToPresent->linesize[2];
        nextFrame.yPlane.resize(static_cast<std::size_t>(frameToPresent->linesize[0] * frameToPresent->height));
        nextFrame.uPlane.resize(static_cast<std::size_t>(frameToPresent->linesize[1] * ((frameToPresent->height + 1) / 2)));
        nextFrame.vPlane.resize(static_cast<std::size_t>(frameToPresent->linesize[2] * ((frameToPresent->height + 1) / 2)));
        std::memcpy(nextFrame.yPlane.data(), frameToPresent->data[0], nextFrame.yPlane.size());
        std::memcpy(nextFrame.uPlane.data(), frameToPresent->data[1], nextFrame.uPlane.size());
        std::memcpy(nextFrame.vPlane.data(), frameToPresent->data[2], nextFrame.vPlane.size());

        {
          std::lock_guard<std::mutex> lock(video_.frameMutex);
          video_.latestFrame = std::move(nextFrame);
        }

        video_.hasFrame.store(true);
        video_.decodedFrameCount.fetch_add(1);
        av_frame_unref(video_.decodedFrame);
      }
    };

    int sendResult = avcodec_send_packet(video_.codecContext, video_.packet);
    if (sendResult == AVERROR(EAGAIN)) {
      const int drainResult = receive_available_frames();
      if (drainResult < 0 && drainResult != AVERROR(EAGAIN) && drainResult != AVERROR_EOF) {
        av_packet_unref(video_.packet);
        return DR_NEED_IDR;
      }
      sendResult = avcodec_send_packet(video_.codecContext, video_.packet);
    }
    av_packet_unref(video_.packet);
    if (sendResult < 0) {
      logging::warn("stream", std::string("avcodec_send_packet failed for H.264: ") + describe_ffmpeg_error(sendResult));
      return DR_NEED_IDR;
    }

    const int receiveResult = receive_available_frames();
    if (receiveResult < 0 && receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF) {
      return DR_NEED_IDR;
    }

    return DR_OK;
  }

  int FfmpegStreamBackend::initialize_audio_decoder(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void *context, int arFlags) {
    (void) context;
    (void) arFlags;

    cleanup_audio_decoder();

    if (opusConfig == nullptr) {
      logging::error("stream", "Moonlight did not provide an Opus configuration for audio startup");
      return -1;
    }

    const int channelCount = CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(audioConfiguration);
    if (channelCount <= 0 || channelCount > 2) {
      logging::error("stream", "The FFmpeg backend currently supports mono or stereo audio only");
      return -1;
    }

    SDL_AudioSpec desiredSpec {};
    desiredSpec.freq = opusConfig->sampleRate > 0 ? opusConfig->sampleRate : 48000;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.channels = static_cast<Uint8>(channelCount);
    desiredSpec.samples = 1024;
    desiredSpec.callback = nullptr;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
      if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        logging::error("stream", std::string("SDL_InitSubSystem(SDL_INIT_AUDIO) failed for streaming playback: ") + SDL_GetError());
        cleanup_audio_decoder();
        return -1;
      }
    }

    audio_.deviceId = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &audio_.obtainedSpec, 0);
    if (audio_.deviceId == 0) {
      logging::error("stream", std::string("SDL_OpenAudioDevice failed for streaming playback: ") + SDL_GetError());
      cleanup_audio_decoder();
      return -1;
    }

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (codec == nullptr) {
      logging::error("stream", "FFmpeg did not provide an Opus decoder");
      cleanup_audio_decoder();
      return -1;
    }

    audio_.codecContext = avcodec_alloc_context3(codec);
    audio_.decodedFrame = av_frame_alloc();
    audio_.packet = av_packet_alloc();
    if (audio_.codecContext == nullptr || audio_.decodedFrame == nullptr || audio_.packet == nullptr) {
      logging::error("stream", "Failed to allocate FFmpeg audio decoder resources");
      cleanup_audio_decoder();
      return -1;
    }

    audio_.codecContext->sample_rate = desiredSpec.freq;
    av_channel_layout_default(&audio_.codecContext->ch_layout, channelCount);
    if (!configure_opus_extradata(channelCount, desiredSpec.freq, audio_.codecContext)) {
      logging::error("stream", "Failed to allocate Opus decoder extradata for FFmpeg");
      cleanup_audio_decoder();
      return -1;
    }

    const int openResult = avcodec_open2(audio_.codecContext, codec, nullptr);
    if (openResult < 0) {
      logging::error("stream", std::string("avcodec_open2 failed for Opus: ") + describe_ffmpeg_error(openResult));
      cleanup_audio_decoder();
      return openResult;
    }

    return 0;
  }

  void FfmpegStreamBackend::start_audio_playback() {
    if (audio_.deviceId != 0) {
      SDL_ClearQueuedAudio(audio_.deviceId);
      SDL_PauseAudioDevice(audio_.deviceId, 0);
      audio_.deviceStarted.store(true);
    }
  }

  void FfmpegStreamBackend::stop_audio_playback() {
    if (audio_.deviceId != 0) {
      SDL_PauseAudioDevice(audio_.deviceId, 1);
      audio_.deviceStarted.store(false);
    }
  }

  void FfmpegStreamBackend::cleanup_audio_decoder() {
    stop_audio_playback();

    if (audio_.deviceId != 0) {
      SDL_ClearQueuedAudio(audio_.deviceId);
      SDL_CloseAudioDevice(audio_.deviceId);
      audio_.deviceId = 0;
    }

    if (audio_.packet != nullptr) {
      av_packet_free(&audio_.packet);
    }
    if (audio_.decodedFrame != nullptr) {
      av_frame_free(&audio_.decodedFrame);
    }
    if (audio_.codecContext != nullptr) {
      avcodec_free_context(&audio_.codecContext);
    }
    if (audio_.resampleContext != nullptr) {
      swr_free(&audio_.resampleContext);
    }

    audio_.obtainedSpec = SDL_AudioSpec {};
    audio_.resampleInputSampleRate = 0;
    audio_.resampleInputSampleFormat = -1;
    audio_.resampleInputChannelCount = 0;
    audio_.queuedAudioBytes.store(0);
  }

  bool FfmpegStreamBackend::ensure_audio_resampler() {
    if (audio_.decodedFrame == nullptr) {
      return false;
    }

    const int inputSampleRate = audio_.decodedFrame->sample_rate;
    const int inputSampleFormat = audio_.decodedFrame->format;
    const int inputChannelCount = audio_.decodedFrame->ch_layout.nb_channels;
    const bool needsReconfigure = audio_.resampleContext == nullptr || audio_.resampleInputSampleRate != inputSampleRate || audio_.resampleInputSampleFormat != inputSampleFormat || audio_.resampleInputChannelCount != inputChannelCount;
    if (!needsReconfigure) {
      return true;
    }

    if (audio_.resampleContext != nullptr) {
      swr_free(&audio_.resampleContext);
    }

    AVChannelLayout outputLayout {};
    av_channel_layout_default(&outputLayout, audio_.obtainedSpec.channels);
    const int resampleConfigResult = swr_alloc_set_opts2(
      &audio_.resampleContext,
      &outputLayout,
      AV_SAMPLE_FMT_S16,
      audio_.obtainedSpec.freq,
      &audio_.decodedFrame->ch_layout,
      static_cast<AVSampleFormat>(audio_.decodedFrame->format),
      audio_.decodedFrame->sample_rate,
      0,
      nullptr
    );
    av_channel_layout_uninit(&outputLayout);
    if (resampleConfigResult < 0) {
      logging::warn("stream", std::string("swr_alloc_set_opts2 failed for Opus: ") + describe_ffmpeg_error(resampleConfigResult));
      return false;
    }
    if (audio_.resampleContext == nullptr || swr_init(audio_.resampleContext) < 0) {
      logging::warn("stream", "swr_init failed for the streaming audio resampler");
      return false;
    }

    audio_.resampleInputSampleRate = inputSampleRate;
    audio_.resampleInputSampleFormat = inputSampleFormat;
    audio_.resampleInputChannelCount = inputChannelCount;
    return true;
  }

  void FfmpegStreamBackend::decode_and_play_audio_sample(char *sampleData, int sampleLength) {
    if (audio_.codecContext == nullptr || audio_.packet == nullptr || audio_.decodedFrame == nullptr || audio_.deviceId == 0) {
      return;
    }

    if (sampleData == nullptr || sampleLength <= 0) {
      return;
    }

    const int packetResult = av_new_packet(audio_.packet, sampleLength);
    if (packetResult < 0) {
      logging::warn("stream", std::string("av_new_packet failed for audio decode: ") + describe_ffmpeg_error(packetResult));
      return;
    }

    std::memcpy(audio_.packet->data, sampleData, static_cast<std::size_t>(sampleLength));
    audio_.packet->size = sampleLength;

    const int sendResult = avcodec_send_packet(audio_.codecContext, audio_.packet);
    av_packet_unref(audio_.packet);
    if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
      logging::warn("stream", std::string("avcodec_send_packet failed for Opus: ") + describe_ffmpeg_error(sendResult));
      return;
    }

    while (true) {
      const int receiveResult = avcodec_receive_frame(audio_.codecContext, audio_.decodedFrame);
      if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
        break;
      }
      if (receiveResult < 0) {
        logging::warn("stream", std::string("avcodec_receive_frame failed for Opus: ") + describe_ffmpeg_error(receiveResult));
        return;
      }

      if (!ensure_audio_resampler()) {
        av_frame_unref(audio_.decodedFrame);
        return;
      }

      const int outputSamples = av_rescale_rnd(
        swr_get_delay(audio_.resampleContext, audio_.decodedFrame->sample_rate) + audio_.decodedFrame->nb_samples,
        audio_.obtainedSpec.freq,
        audio_.decodedFrame->sample_rate,
        AV_ROUND_UP
      );
      const int outputBufferSize = av_samples_get_buffer_size(
        nullptr,
        audio_.obtainedSpec.channels,
        outputSamples,
        AV_SAMPLE_FMT_S16,
        1
      );
      if (outputBufferSize <= 0) {
        logging::warn("stream", "av_samples_get_buffer_size failed for decoded audio output");
        av_frame_unref(audio_.decodedFrame);
        return;
      }

      std::vector<std::uint8_t> outputBuffer(static_cast<std::size_t>(outputBufferSize));
      std::uint8_t *outputData[] = {outputBuffer.data()};
      const int convertedSamples = swr_convert(
        audio_.resampleContext,
        outputData,
        outputSamples,
        const_cast<const std::uint8_t **>(audio_.decodedFrame->extended_data),
        audio_.decodedFrame->nb_samples
      );
      if (convertedSamples < 0) {
        logging::warn("stream", std::string("swr_convert failed for Opus: ") + describe_ffmpeg_error(convertedSamples));
        av_frame_unref(audio_.decodedFrame);
        return;
      }

      const int convertedBytes = convertedSamples * audio_.obtainedSpec.channels * static_cast<int>(sizeof(std::int16_t));
      const Uint32 maxQueuedBytes = (audio_bytes_per_second(audio_.obtainedSpec) * STREAM_OVERLAY_AUDIO_QUEUE_LIMIT_MS) / 1000U;
      if (SDL_GetQueuedAudioSize(audio_.deviceId) > maxQueuedBytes) {
        SDL_ClearQueuedAudio(audio_.deviceId);
      }
      if (convertedBytes > 0 && SDL_QueueAudio(audio_.deviceId, outputBuffer.data(), static_cast<Uint32>(convertedBytes)) == 0) {
        audio_.queuedAudioBytes.fetch_add(static_cast<std::uint64_t>(convertedBytes));
      }

      av_frame_unref(audio_.decodedFrame);
    }
  }

}  // namespace streaming
