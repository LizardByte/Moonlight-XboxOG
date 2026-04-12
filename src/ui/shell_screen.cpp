// class header include
#include "src/ui/shell_screen.h"

// standard includes
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// nxdk includes
#include <hal/debug.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names

// local includes
#include "src/app/settings_storage.h"
#include "src/input/navigation_input.h"
#include "src/logging/global_logger.h"
#include "src/logging/log_file.h"
#include "src/network/host_pairing.h"
#include "src/network/runtime_network.h"
#include "src/os.h"
#include "src/platform/error_utils.h"
#include "src/platform/filesystem_utils.h"
#include "src/splash/splash_layout.h"
#include "src/startup/client_identity_storage.h"
#include "src/startup/cover_art_cache.h"
#include "src/startup/host_storage.h"
#include "src/startup/saved_files.h"
#include "src/ui/host_probe_result_queue.h"
#include "src/ui/shell_view.h"

namespace {

  constexpr std::size_t PAIRING_THREAD_STACK_SIZE = 1024U * 1024U;
  constexpr Uint32 HOST_PROBE_REFRESH_INTERVAL_MILLISECONDS = 10000U;
  constexpr Uint32 APP_LIST_REFRESH_INTERVAL_MILLISECONDS = 30000U;

  constexpr Uint8 BACKGROUND_RED = 0x10;
  constexpr Uint8 BACKGROUND_GREEN = 0x12;
  constexpr Uint8 BACKGROUND_BLUE = 0x16;
  constexpr Uint8 PANEL_RED = 0x24;
  constexpr Uint8 PANEL_GREEN = 0x25;
  constexpr Uint8 PANEL_BLUE = 0x27;
  constexpr Uint8 PANEL_ALT_RED = 0x1C;
  constexpr Uint8 PANEL_ALT_GREEN = 0x1D;
  constexpr Uint8 PANEL_ALT_BLUE = 0x20;
  constexpr Uint8 ACCENT_RED = 0x00;
  constexpr Uint8 ACCENT_GREEN = 0xF3;
  constexpr Uint8 ACCENT_BLUE = 0xD4;
  constexpr Uint8 TEXT_RED = 0xF2;
  constexpr Uint8 TEXT_GREEN = 0xF5;
  constexpr Uint8 TEXT_BLUE = 0xF8;
  constexpr Uint8 MUTED_RED = 0xA3;
  constexpr Uint8 MUTED_GREEN = 0xAB;
  constexpr Uint8 MUTED_BLUE = 0xB5;
  constexpr Sint16 TRIGGER_PAGE_SCROLL_THRESHOLD = 16000;
  constexpr Sint16 LEFT_STICK_NAVIGATION_THRESHOLD = 16000;
  constexpr Sint16 LEFT_STICK_NAVIGATION_RELEASE_THRESHOLD = 12000;
  constexpr Uint32 EXIT_COMBO_HOLD_MILLISECONDS = 900U;
  constexpr Uint32 CONTROLLER_NAVIGATION_INITIAL_REPEAT_MILLISECONDS = 150U;
  constexpr Uint32 CONTROLLER_NAVIGATION_REPEAT_MILLISECONDS = 45U;
  constexpr Uint32 LOG_VIEWER_SCROLL_REPEAT_MILLISECONDS = 110U;
  constexpr Uint32 LOG_VIEWER_FAST_SCROLL_REPEAT_MILLISECONDS = 45U;
  constexpr int SHELL_EVENT_WAIT_TIMEOUT_MILLISECONDS = 2;
  constexpr std::size_t LOG_VIEWER_MAX_LOADED_LINES = 512U;
  constexpr std::size_t LOG_VIEWER_MAX_RENDER_CHARACTERS = 320U;

  std::string build_asset_path(const char *relativePath) {
    return std::string(DATA_PATH) + "assets" + PATH_SEP + relativePath;
  }

  bool asset_path_uses_svg(const char *relativePath) {
    if (relativePath == nullptr) {
      return false;
    }

    const std::string path(relativePath);
    return path.size() >= 4U && path.substr(path.size() - 4U) == ".svg";
  }

  bool asset_path_is_svg_icon(const char *relativePath) {
    if (!asset_path_uses_svg(relativePath)) {
      return false;
    }

    const std::string path(relativePath);
    return path.rfind("icons\\", 0U) == 0U;
  }

  SDL_Surface *normalize_asset_surface(SDL_Surface *surface) {
    if (surface == nullptr) {
      return nullptr;
    }

    SDL_Surface *normalizedSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
    if (normalizedSurface == nullptr) {
      SDL_FreeSurface(surface);
      return nullptr;
    }

    SDL_FreeSurface(surface);
    if (SDL_SetSurfaceBlendMode(normalizedSurface, SDL_BLENDMODE_BLEND) != 0) {
      SDL_FreeSurface(normalizedSurface);
      return nullptr;
    }

    return normalizedSurface;
  }

  SDL_Surface *prepare_asset_surface(SDL_Surface *surface) {
    return normalize_asset_surface(surface);
  }

  SDL_Texture *create_texture_from_surface_with_scale_quality(SDL_Renderer *renderer, SDL_Surface *surface, const char *scaleQualityHint) {
    if (renderer == nullptr || surface == nullptr || scaleQualityHint == nullptr) {
      return nullptr;
    }

    const char *previousHint = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    const std::string previousHintValue = previousHint == nullptr ? std::string {} : std::string(previousHint);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scaleQualityHint);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, previousHintValue.empty() ? "0" : previousHintValue.c_str());
    return texture;
  }

  int report_shell_failure(const char *category, const std::string &message) {
    logging::logger.error(category, message);
    logging::logger.warn(category, "Holding the failure screen for 5 seconds before exit.");
    Sleep(5000);
    return 1;
  }

  void destroy_texture(SDL_Texture *texture) {
    if (texture != nullptr) {
      SDL_DestroyTexture(texture);
    }
  }

  bool render_surface_line(SDL_Renderer *renderer, SDL_Surface *surface, int x, int y, int *drawnHeight) {
    if (renderer == nullptr || surface == nullptr) {
      return false;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
      SDL_FreeSurface(surface);
      return false;
    }

    SDL_Rect destination {x, y, surface->w, surface->h};
    SDL_FreeSurface(surface);
    const int renderResult = SDL_RenderCopy(renderer, texture, nullptr, &destination);
    destroy_texture(texture);

    if (drawnHeight != nullptr) {
      *drawnHeight = destination.h;
    }

    return renderResult == 0;
  }

  /**
   * @brief Caches cover-art textures keyed by the app art cache key.
   */
  struct CoverArtTextureCache {
    std::unordered_map<std::string, SDL_Texture *> textures;  ///< Loaded cover-art textures by cache key.
    std::unordered_map<std::string, bool> failedKeys;  ///< Cache keys that already failed to load.
  };

  /**
   * @brief Caches UI asset textures keyed by asset path.
   */
  struct AssetTextureCache {
    std::unordered_map<std::string, SDL_Texture *> textures;  ///< Loaded asset textures by path.
    std::unordered_map<std::string, bool> failedKeys;  ///< Asset paths that already failed to load.
  };

  /**
   * @brief Stores a reusable rendered text texture and the inputs that produced it.
   */
  struct CachedTextTexture {
    SDL_Texture *texture = nullptr;  ///< Cached SDL texture for the rendered text.
    TTF_Font *font = nullptr;  ///< Font used to build the cached texture.
    std::string sourceText;  ///< Original uncropped text source.
    std::string renderedText;  ///< Sanitized or cropped text actually rendered into the texture.
    int maxWidth = 0;  ///< Maximum width used when generating the cached texture.
    int width = 0;  ///< Cached texture width in pixels.
    int height = 0;  ///< Cached texture height in pixels.
    SDL_Color color {0x00, 0x00, 0x00, 0x00};  ///< Text color used when rendering the cached texture.
    bool wrapped = false;  ///< True when the texture was generated with wrapped rendering.
  };

  /**
   * @brief Stores reusable layout and text textures for the add-host keypad modal.
   */
  struct KeypadModalLayoutCache {
    int modalInnerWidth = 0;  ///< Last modal content width used to build the cached line layout.
    int modalTextHeight = 0;  ///< Cached combined height of the modal body text.
    std::vector<std::string> lines;  ///< Last body lines used to populate the cache.
    CachedTextTexture titleTexture;  ///< Cached rendered title texture.
    std::vector<CachedTextTexture> lineTextures;  ///< Cached rendered body line textures.
    std::vector<CachedTextTexture> buttonLabelTextures;  ///< Cached rendered keypad button labels.
  };

  bool render_footer_actions(
    SDL_Renderer *renderer,
    TTF_Font *font,
    AssetTextureCache *assetCache,
    const std::vector<ui::ShellFooterAction> &actions,
    const SDL_Rect &footerRect
  );

  void clear_cover_art_texture_cache(CoverArtTextureCache *cache) {
    if (cache == nullptr) {
      return;
    }

    for (const auto &[cacheKey, texture] : cache->textures) {
      (void) cacheKey;
      destroy_texture(texture);
    }
    cache->textures.clear();
    cache->failedKeys.clear();
  }

  void clear_cover_art_texture(CoverArtTextureCache *cache, const std::string &cacheKey) {
    if (cache == nullptr || cacheKey.empty()) {
      return;
    }

    if (const auto textureIterator = cache->textures.find(cacheKey); textureIterator != cache->textures.end()) {
      destroy_texture(textureIterator->second);
      cache->textures.erase(textureIterator);
    }
    cache->failedKeys.erase(cacheKey);
  }

  void clear_asset_texture_cache(AssetTextureCache *cache) {
    if (cache == nullptr) {
      return;
    }

    for (const auto &[assetPath, texture] : cache->textures) {
      (void) assetPath;
      destroy_texture(texture);
    }
    cache->textures.clear();
    cache->failedKeys.clear();
  }

  /**
   * @brief Returns whether two SDL colors are identical.
   *
   * @param left First color to compare.
   * @param right Second color to compare.
   * @return True when all RGBA components match.
   */
  bool colors_match(SDL_Color left, SDL_Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
  }

  /**
   * @brief Releases a cached text texture and resets its metadata.
   *
   * @param cache Cached text entry to clear.
   */
  void clear_cached_text_texture(CachedTextTexture *cache) {
    if (cache == nullptr) {
      return;
    }

    destroy_texture(cache->texture);
    *cache = {};
  }

  /**
   * @brief Releases every cached text texture stored for the keypad modal.
   *
   * @param cache Keypad modal cache to clear.
   */
  void clear_keypad_modal_layout_cache(KeypadModalLayoutCache *cache) {
    if (cache == nullptr) {
      return;
    }

    clear_cached_text_texture(&cache->titleTexture);
    for (CachedTextTexture &lineTexture : cache->lineTextures) {
      clear_cached_text_texture(&lineTexture);
    }
    for (CachedTextTexture &buttonLabelTexture : cache->buttonLabelTextures) {
      clear_cached_text_texture(&buttonLabelTexture);
    }
    cache->lineTextures.clear();
    cache->buttonLabelTextures.clear();
    cache->modalInnerWidth = 0;
    cache->modalTextHeight = 0;
    cache->lines.clear();
  }

  Uint32 color_seed(std::string_view text) {
    Uint32 value = 2166136261U;
    for (char character : text) {
      value ^= static_cast<Uint8>(character);
      value *= 16777619U;
    }
    return value;
  }

  SDL_Color placeholder_color(std::string_view seedText) {
    const Uint32 seed = color_seed(seedText);
    return {
      static_cast<Uint8>(0x40 + (seed & 0x3F)),
      static_cast<Uint8>(0x50 + ((seed >> 8U) & 0x4F)),
      static_cast<Uint8>(0x70 + ((seed >> 16U) & 0x5F)),
      0xFF,
    };
  }

  std::string sanitize_text_for_render(std::string_view text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {  // NOSONAR(cpp:S886) UTF-8 validation needs explicit cursor control
      const auto character = static_cast<unsigned char>(text[index]);
      if (character == '\r' || character == '\n') {
        continue;
      }
      if (character == '\t') {
        sanitized.append("  ");
        continue;
      }
      if (character < 0x80) {
        if (character < 0x20) {
          sanitized.push_back('?');
          continue;
        }
        sanitized.push_back(static_cast<char>(character));
        continue;
      }

      std::size_t sequenceLength = 0U;
      if ((character & 0xE0U) == 0xC0U) {  // NOSONAR(cpp:S6022) UTF-8 parsing is byte-oriented by design
        sequenceLength = 2U;
      } else if ((character & 0xF0U) == 0xE0U) {  // NOSONAR(cpp:S6022) UTF-8 parsing is byte-oriented by design
        sequenceLength = 3U;
      } else if ((character & 0xF8U) == 0xF0U) {  // NOSONAR(cpp:S6022) UTF-8 parsing is byte-oriented by design
        sequenceLength = 4U;
      }

      const bool sequenceAvailable = sequenceLength > 0U && index + sequenceLength <= text.size();
      bool sequenceValid = sequenceAvailable;
      for (std::size_t continuationIndex = 1U; sequenceValid && continuationIndex < sequenceLength; ++continuationIndex) {  // NOSONAR(cpp:S886) UTF-8 validation needs explicit cursor control
        const auto continuation = static_cast<unsigned char>(text[index + continuationIndex]);
        sequenceValid = (continuation & 0xC0U) == 0x80U;  // NOSONAR(cpp:S6022) UTF-8 parsing is byte-oriented by design
      }

      if (sequenceValid) {
        sanitized.append(text.substr(index, sequenceLength));
        index += sequenceLength - 1U;
        continue;
      }

      sanitized.push_back('?');
    }
    return sanitized;
  }

  std::string truncate_text_for_render(std::string_view text, std::size_t maxCharacters) {
    if (maxCharacters == 0U || text.size() <= maxCharacters) {
      return std::string(text);
    }

    if (maxCharacters <= 3U) {
      return std::string(maxCharacters, '.');
    }

    return std::string(text.substr(0, maxCharacters - 3U)) + "...";
  }

  std::string sanitize_ascii_text_for_render(std::string_view text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (const unsigned char character : text) {
      if (character == '\r' || character == '\n') {
        continue;
      }
      if (character == '\t') {
        sanitized.append("  ");
        continue;
      }
      if (character >= 0x20 && character <= 0x7E) {
        sanitized.push_back(static_cast<char>(character));
        continue;
      }
      sanitized.push_back('?');
    }
    return sanitized;
  }

  std::string app_monogram(const ui::ShellAppTile &tile) {
    for (char character : tile.name) {
      if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) {
        return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
      }
    }

    return "?";
  }

  bool render_default_app_cover(
    SDL_Renderer *renderer,
    TTF_Font *labelFont,
    const ui::ShellAppTile &tile,
    const SDL_Rect &rect,
    const AssetTextureCache *assetCache
  );

  SDL_Texture *load_cover_art_texture(SDL_Renderer *renderer, CoverArtTextureCache *cache, const std::string &cacheKey) {
    if (renderer == nullptr || cache == nullptr || cacheKey.empty()) {
      return nullptr;
    }

    if (const auto existingTexture = cache->textures.find(cacheKey); existingTexture != cache->textures.end()) {
      return existingTexture->second;
    }
    if (cache->failedKeys.find(cacheKey) != cache->failedKeys.end()) {
      return nullptr;
    }

    const startup::LoadCoverArtResult loadResult = startup::load_cover_art(cacheKey);
    if (!loadResult.fileFound || loadResult.bytes.empty()) {
      cache->failedKeys[cacheKey] = true;
      return nullptr;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(loadResult.bytes.data(), static_cast<int>(loadResult.bytes.size()));
    if (rw == nullptr) {
      cache->failedKeys[cacheKey] = true;
      return nullptr;
    }

    SDL_Surface *surface = IMG_Load_RW(rw, 1);
    if (surface == nullptr) {
      cache->failedKeys[cacheKey] = true;
      return nullptr;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (texture == nullptr) {
      cache->failedKeys[cacheKey] = true;
      return nullptr;
    }

    cache->textures.try_emplace(cacheKey, texture);
    return texture;
  }

  SDL_Texture *load_texture_from_asset(SDL_Renderer *renderer, const char *relativePath) {
    if (renderer == nullptr || relativePath == nullptr) {
      return nullptr;
    }

    const std::string assetPath = build_asset_path(relativePath);
    SDL_Surface *surface = IMG_Load(assetPath.c_str());
    if (surface == nullptr) {
      return nullptr;
    }

    surface = prepare_asset_surface(surface);
    if (surface == nullptr) {
      return nullptr;
    }

    SDL_Texture *texture = create_texture_from_surface_with_scale_quality(renderer, surface, asset_path_is_svg_icon(relativePath) ? "0" : "1");
    SDL_FreeSurface(surface);
    return texture;
  }

  SDL_Texture *load_cached_asset_texture(SDL_Renderer *renderer, AssetTextureCache *cache, const std::string &relativePath) {
    if (renderer == nullptr || cache == nullptr || relativePath.empty()) {
      return nullptr;
    }

    if (const auto existingTexture = cache->textures.find(relativePath); existingTexture != cache->textures.end()) {
      return existingTexture->second;
    }
    if (cache->failedKeys.find(relativePath) != cache->failedKeys.end()) {
      return nullptr;
    }

    SDL_Texture *texture = load_texture_from_asset(renderer, relativePath.c_str());
    if (texture == nullptr) {
      cache->failedKeys[relativePath] = true;
      return nullptr;
    }

    cache->textures.try_emplace(relativePath, texture);
    return texture;
  }

  void fill_rect(SDL_Renderer *renderer, const SDL_Rect &rect, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 0xFF) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderFillRect(renderer, &rect);
  }

  void draw_rect(SDL_Renderer *renderer, const SDL_Rect &rect, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 0xFF) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderDrawRect(renderer, &rect);
  }

  bool render_text_line(  // NOSONAR(cpp:S107) helper keeps the SDL text rendering callsite contract explicit
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    int x,
    int y,
    int maxWidth,
    int *drawnHeight = nullptr
  ) {
    if (font == nullptr || maxWidth <= 0) {
      if (drawnHeight != nullptr) {
        *drawnHeight = 0;
      }
      return false;
    }

    const std::string renderText = sanitize_text_for_render(text);
    if (renderText.empty()) {
      if (drawnHeight != nullptr) {
        *drawnHeight = TTF_FontLineSkip(font);
      }
      return true;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, renderText.c_str(), color, static_cast<Uint32>(maxWidth));
    if (surface == nullptr) {
      return false;
    }

    return render_surface_line(renderer, surface, x, y, drawnHeight);
  }

  std::string fit_single_line_text(TTF_Font *font, const std::string &text, int maxWidth) {
    if (font == nullptr || maxWidth <= 0) {
      return {};
    }

    const std::string sanitized = sanitize_ascii_text_for_render(text);
    if (sanitized.empty()) {
      return {};
    }

    int textWidth = 0;
    int textHeight = 0;
    if (TTF_SizeText(font, sanitized.c_str(), &textWidth, &textHeight) == 0 && textWidth <= maxWidth) {
      return sanitized;
    }

    const std::string ellipsis = "...";
    for (std::size_t length = sanitized.size(); length > 0U; --length) {
      const std::string candidate = sanitize_ascii_text_for_render(std::string_view(sanitized).substr(0, length)) + ellipsis;
      if (TTF_SizeText(font, candidate.c_str(), &textWidth, &textHeight) == 0 && textWidth <= maxWidth) {
        return candidate;
      }
    }

    return ellipsis;
  }

  bool render_text_line_simple(  // NOSONAR(cpp:S107) helper keeps the SDL text rendering callsite contract explicit
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    int x,
    int y,
    int maxWidth,
    int *drawnHeight = nullptr
  ) {
    if (font == nullptr || maxWidth <= 0) {
      if (drawnHeight != nullptr) {
        *drawnHeight = 0;
      }
      return false;
    }

    const std::string renderText = fit_single_line_text(font, text, maxWidth);
    if (renderText.empty()) {
      if (drawnHeight != nullptr) {
        *drawnHeight = TTF_FontLineSkip(font);
      }
      return true;
    }

    SDL_Surface *surface = TTF_RenderText_Blended(font, renderText.c_str(), color);
    if (surface == nullptr) {
      return false;
    }

    return render_surface_line(renderer, surface, x, y, drawnHeight);
  }

  /**
   * @brief Builds or reuses a cached wrapped text texture for keypad modal content.
   *
   * @param renderer SDL renderer used to create textures.
   * @param font Font used to render the text.
   * @param text Source text to cache.
   * @param color Text color used for rendering.
   * @param maxWidth Maximum wrapped width in pixels.
   * @param cache Cache entry to populate or reuse.
   * @return True when the wrapped texture is ready for use.
   */
  bool ensure_wrapped_text_texture(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    int maxWidth,
    CachedTextTexture *cache
  ) {
    if (font == nullptr || maxWidth <= 0 || cache == nullptr) {
      return false;
    }

    const std::string renderText = sanitize_text_for_render(text);
    if (
      cache->texture != nullptr && cache->wrapped && cache->font == font && cache->sourceText == text && cache->renderedText == renderText && cache->maxWidth == maxWidth &&
      colors_match(cache->color, color)
    ) {
      return true;
    }

    clear_cached_text_texture(cache);
    cache->font = font;
    cache->sourceText = text;
    cache->renderedText = renderText;
    cache->maxWidth = maxWidth;
    cache->color = color;
    cache->wrapped = true;
    cache->height = renderText.empty() ? TTF_FontLineSkip(font) : 0;

    if (renderText.empty()) {
      return true;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, renderText.c_str(), color, static_cast<Uint32>(maxWidth));
    if (surface == nullptr) {
      cache->height = TTF_FontLineSkip(font);
      return false;
    }

    cache->width = surface->w;
    cache->height = surface->h;
    cache->texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (cache->texture == nullptr) {
      cache->width = 0;
      cache->height = TTF_FontLineSkip(font);
      return false;
    }

    return true;
  }

  /**
   * @brief Builds or reuses a cached single-line text texture for keypad button labels.
   *
   * @param renderer SDL renderer used to create textures.
   * @param font Font used to render the text.
   * @param text Source text to cache.
   * @param color Text color used for rendering.
   * @param maxWidth Maximum label width in pixels.
   * @param cache Cache entry to populate or reuse.
   * @return True when the single-line texture is ready for use.
   */
  bool ensure_single_line_text_texture(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    int maxWidth,
    CachedTextTexture *cache
  ) {
    if (font == nullptr || maxWidth <= 0 || cache == nullptr) {
      return false;
    }

    const std::string renderText = fit_single_line_text(font, text, maxWidth);
    if (
      cache->texture != nullptr && !cache->wrapped && cache->font == font && cache->sourceText == text && cache->renderedText == renderText && cache->maxWidth == maxWidth &&
      colors_match(cache->color, color)
    ) {
      return true;
    }

    clear_cached_text_texture(cache);
    cache->font = font;
    cache->sourceText = text;
    cache->renderedText = renderText;
    cache->maxWidth = maxWidth;
    cache->color = color;
    cache->wrapped = false;
    cache->height = renderText.empty() ? TTF_FontLineSkip(font) : 0;

    if (renderText.empty()) {
      return true;
    }

    SDL_Surface *surface = TTF_RenderText_Blended(font, renderText.c_str(), color);
    if (surface == nullptr) {
      cache->height = TTF_FontLineSkip(font);
      return false;
    }

    cache->width = surface->w;
    cache->height = surface->h;
    cache->texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (cache->texture == nullptr) {
      cache->width = 0;
      cache->height = TTF_FontLineSkip(font);
      return false;
    }

    return true;
  }

  /**
   * @brief Draws a cached text texture at a fixed point, using stored height for blank lines.
   *
   * @param renderer SDL renderer used for drawing.
   * @param cache Cached text texture to render.
   * @param x Left destination coordinate.
   * @param y Top destination coordinate.
   * @param drawnHeight Receives the rendered or reserved text height.
   * @return True when drawing succeeded or no texture was required.
   */
  bool render_cached_text_texture(SDL_Renderer *renderer, const CachedTextTexture &cache, int x, int y, int *drawnHeight = nullptr) {
    if (cache.texture == nullptr) {
      if (drawnHeight != nullptr) {
        *drawnHeight = cache.height;
      }
      return true;
    }

    const SDL_Rect destination {x, y, cache.width, cache.height};
    const bool rendered = SDL_RenderCopy(renderer, cache.texture, nullptr, &destination) == 0;
    if (drawnHeight != nullptr) {
      *drawnHeight = cache.height;
    }
    return rendered;
  }

  /**
   * @brief Draws a cached single-line text texture centered within a button rect.
   *
   * @param renderer SDL renderer used for drawing.
   * @param cache Cached text texture to render.
   * @param rect Button rectangle that should contain the rendered text.
   * @param drawnHeight Receives the rendered or reserved text height.
   * @return True when drawing succeeded or no texture was required.
   */
  bool render_cached_centered_text_texture(SDL_Renderer *renderer, const CachedTextTexture &cache, const SDL_Rect &rect, int *drawnHeight = nullptr) {
    if (cache.texture == nullptr) {
      if (drawnHeight != nullptr) {
        *drawnHeight = cache.height;
      }
      return true;
    }

    const SDL_Rect destination {
      rect.x + std::max(0, (rect.w - cache.width) / 2),
      rect.y + std::max(0, (rect.h - cache.height) / 2),
      cache.width,
      cache.height,
    };
    const bool rendered = SDL_RenderCopy(renderer, cache.texture, nullptr, &destination) == 0;
    if (drawnHeight != nullptr) {
      *drawnHeight = cache.height;
    }
    return rendered;
  }

  bool render_text_centered_simple(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    const SDL_Rect &rect,
    int *drawnHeight = nullptr
  ) {
    const std::string renderText = fit_single_line_text(font, text, rect.w);
    if (renderText.empty()) {
      if (drawnHeight != nullptr) {
        *drawnHeight = TTF_FontLineSkip(font);
      }
      return true;
    }

    int textWidth = 0;
    int textHeight = 0;
    if (TTF_SizeText(font, renderText.c_str(), &textWidth, &textHeight) != 0) {
      return render_text_line_simple(renderer, font, renderText, color, rect.x + 8, rect.y + 8, std::max(1, rect.w - 16), drawnHeight);
    }

    const int x = rect.x + std::max(0, (rect.w - textWidth) / 2);
    const int y = rect.y + std::max(0, (rect.h - textHeight) / 2);
    return render_text_line_simple(renderer, font, renderText, color, x, y, rect.w, drawnHeight);
  }

  int measure_wrapped_text_height(TTF_Font *font, const std::string &text, int maxWidth) {
    if (font == nullptr || maxWidth <= 0) {
      return 0;
    }

    const std::string renderText = sanitize_text_for_render(text);
    if (renderText.empty()) {
      return TTF_FontLineSkip(font);
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, renderText.c_str(), {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, static_cast<Uint32>(maxWidth));
    if (surface == nullptr) {
      return TTF_FontLineSkip(font);
    }

    const int height = surface->h;
    SDL_FreeSurface(surface);
    return height;
  }

  /**
   * @brief Reuses keypad modal text measurements until the content or width changes.
   *
   * @param renderer SDL renderer used when cached text textures must be rebuilt.
   * @param font Font used to measure and render the keypad body text.
   * @param viewModel Shell view model containing the keypad modal content.
   * @param modalInnerWidth Available wrapped-text width inside the modal.
   * @param cache Cache entry that stores the last measured keypad modal layout.
   * @return Total pixel height required for the keypad modal body text.
   */
  int keypad_modal_text_height(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const ui::ShellViewModel &viewModel,
    int modalInnerWidth,
    KeypadModalLayoutCache *cache
  ) {
    if (cache != nullptr && cache->modalInnerWidth == modalInnerWidth && cache->lines == viewModel.keypadModalLines) {
      return cache->modalTextHeight;
    }

    if (cache != nullptr) {
      if (cache->lineTextures.size() > viewModel.keypadModalLines.size()) {
        for (std::size_t index = viewModel.keypadModalLines.size(); index < cache->lineTextures.size(); ++index) {
          clear_cached_text_texture(&cache->lineTextures[index]);
        }
      }
      cache->lineTextures.resize(viewModel.keypadModalLines.size());
    }

    int modalTextHeight = 0;
    for (std::size_t index = 0; index < viewModel.keypadModalLines.size(); ++index) {
      const std::string &line = viewModel.keypadModalLines[index];
      if (cache != nullptr) {
        if (!ensure_wrapped_text_texture(renderer, font, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, modalInnerWidth, &cache->lineTextures[index])) {
          modalTextHeight += measure_wrapped_text_height(font, line, modalInnerWidth) + 6;
          continue;
        }
        modalTextHeight += cache->lineTextures[index].height + 6;
        continue;
      }

      modalTextHeight += measure_wrapped_text_height(font, line, modalInnerWidth) + 6;
    }

    if (cache != nullptr) {
      cache->modalInnerWidth = modalInnerWidth;
      cache->modalTextHeight = modalTextHeight;
      cache->lines = viewModel.keypadModalLines;
    }

    return modalTextHeight;
  }

  bool render_text_centered(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::string &text,
    SDL_Color color,
    const SDL_Rect &rect,
    int *drawnHeight = nullptr
  ) {
    int textWidth = 0;
    int textHeight = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &textWidth, &textHeight) != 0) {
      return render_text_line(renderer, font, text, color, rect.x + 8, rect.y + 8, rect.w - 16, drawnHeight);
    }

    const int x = rect.x + std::max(0, (rect.w - textWidth) / 2);
    const int y = rect.y + std::max(0, (rect.h - textHeight) / 2);
    return render_text_line(renderer, font, text, color, x, y, rect.w, drawnHeight);
  }

  bool render_texture_fit(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect &rect) {
    if (renderer == nullptr || texture == nullptr || rect.w <= 0 || rect.h <= 0) {
      return false;
    }

    int textureWidth = 0;
    int textureHeight = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &textureWidth, &textureHeight) != 0 || textureWidth <= 0 || textureHeight <= 0) {
      return false;
    }

    SDL_Rect destination = rect;
    if ((textureWidth * rect.h) > (textureHeight * rect.w)) {
      destination.h = std::max(1, (textureHeight * rect.w) / textureWidth);
      destination.y = rect.y + std::max(0, (rect.h - destination.h) / 2);
    } else {
      destination.w = std::max(1, (textureWidth * rect.h) / textureHeight);
      destination.x = rect.x + std::max(0, (rect.w - destination.w) / 2);
    }

    return SDL_RenderCopy(renderer, texture, nullptr, &destination) == 0;
  }

  bool render_texture_fill(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect &rect) {
    if (renderer == nullptr || texture == nullptr || rect.w <= 0 || rect.h <= 0) {
      return false;
    }

    int textureWidth = 0;
    int textureHeight = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &textureWidth, &textureHeight) != 0 || textureWidth <= 0 || textureHeight <= 0) {
      return false;
    }

    SDL_Rect source {0, 0, textureWidth, textureHeight};
    if ((textureWidth * rect.h) > (textureHeight * rect.w)) {
      source.w = std::max(1, (textureHeight * rect.w) / rect.h);
      source.x = std::max(0, (textureWidth - source.w) / 2);
    } else {
      source.h = std::max(1, (textureWidth * rect.h) / rect.w);
      source.y = std::max(0, (textureHeight - source.h) / 2);
    }

    return SDL_RenderCopy(renderer, texture, &source, &rect) == 0;
  }

  bool render_asset_icon(SDL_Renderer *renderer, AssetTextureCache *cache, const std::string &relativePath, const SDL_Rect &rect) {
    SDL_Texture *texture = load_cached_asset_texture(renderer, cache, relativePath);
    if (texture == nullptr) {
      return false;
    }

    return render_texture_fit(renderer, texture, rect);
  }

  bool render_default_app_cover(
    SDL_Renderer *renderer,
    TTF_Font *labelFont,
    const ui::ShellAppTile &tile,
    const SDL_Rect &rect,
    const AssetTextureCache *assetCache
  ) {
    (void) assetCache;
    const SDL_Color seedColor = placeholder_color(tile.name);
    fill_rect(renderer, rect, seedColor.r / 2, seedColor.g / 2, seedColor.b / 2, 0xFF);
    fill_rect(renderer, {rect.x + std::max(4, rect.w / 18), rect.y, std::max(6, rect.w / 14), rect.h}, seedColor.r, seedColor.g, seedColor.b, 0xFF);

    const SDL_Rect innerRect {
      rect.x + std::max(10, rect.w / 10),
      rect.y + std::max(10, rect.h / 12),
      std::max(1, rect.w - std::max(20, rect.w / 5)),
      std::max(1, rect.h - std::max(20, rect.h / 6)),
    };
    fill_rect(renderer, innerRect, PANEL_ALT_RED, PANEL_ALT_GREEN, PANEL_ALT_BLUE, 0xD8);

    const int logoSize = std::max(28, std::min(innerRect.w - 18, (innerRect.h * 2) / 5));
    const SDL_Rect logoRect {
      innerRect.x + std::max(0, (innerRect.w - logoSize) / 2),
      innerRect.y + std::max(8, innerRect.h / 9),
      logoSize,
      logoSize,
    };
    render_text_centered_simple(renderer, labelFont, app_monogram(tile), {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, logoRect);

    render_text_centered_simple(
      renderer,
      labelFont,
      "Moonlight",
      {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xD8},
      {
        innerRect.x + 8,
        innerRect.y + innerRect.h - std::max(40, innerRect.h / 5),
        std::max(1, innerRect.w - 16),
        std::max(20, innerRect.h / 8),
      }
    );
    return true;
  }

  void draw_line(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 0xFF) {  // NOSONAR(cpp:S107) low-level draw helper intentionally mirrors SDL primitive arguments
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
  }

  bool render_app_cover(
    SDL_Renderer *renderer,
    TTF_Font *labelFont,
    const ui::ShellAppTile &tile,
    const SDL_Rect &rect,
    CoverArtTextureCache *textureCache,
    AssetTextureCache *assetCache
  ) {
    fill_rect(renderer, rect, PANEL_ALT_RED, PANEL_ALT_GREEN, PANEL_ALT_BLUE, 0xFF);
    if (SDL_Texture *texture = tile.boxArtCached ? load_cover_art_texture(renderer, textureCache, tile.boxArtCacheKey) : nullptr; texture != nullptr) {
      if (!render_texture_fill(renderer, texture, rect)) {
        return false;
      }
    } else {
      if (!render_default_app_cover(renderer, labelFont, tile, rect, assetCache)) {
        return false;
      }
    }

    const int overlayTextWidth = std::max(1, rect.w - 16);
    const int overlayHeight = std::max(28, (TTF_FontLineSkip(labelFont) * 2) + 8);
    const SDL_Rect overlayRect {rect.x, rect.y + rect.h - overlayHeight, rect.w, overlayHeight};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    fill_rect(renderer, overlayRect, 0x00, 0x00, 0x00, 0x96);
    render_text_line_simple(
      renderer,
      labelFont,
      truncate_text_for_render(tile.name, 72U),
      {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF},
      overlayRect.x + 8,
      overlayRect.y + std::max(4, (overlayHeight - TTF_FontLineSkip(labelFont)) / 2),
      overlayTextWidth
    );

    if (tile.selected) {
      draw_rect(renderer, rect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
    } else {
      draw_rect(renderer, rect, TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0x28);
    }
    return true;
  }

  SDL_Rect log_viewer_rect(const ui::ShellViewModel &viewModel, int screenWidth, int screenHeight, int outerMargin) {
    const int fullWidth = screenWidth - (outerMargin * 2);
    const int dockedWidth = std::max(420, (screenWidth - (outerMargin * 3)) / 2);
    const int height = screenHeight - (outerMargin * 2);

    switch (viewModel.logViewerPlacement) {
      case app::LogViewerPlacement::left:
        return {outerMargin, outerMargin, dockedWidth, height};
      case app::LogViewerPlacement::right:
        return {screenWidth - outerMargin - dockedWidth, outerMargin, dockedWidth, height};
      case app::LogViewerPlacement::full:
        return {outerMargin, outerMargin, fullWidth, height};
    }

    return {outerMargin, outerMargin, fullWidth, height};
  }

  void render_vertical_scrollbar(SDL_Renderer *renderer, const SDL_Rect &trackRect, int totalItemCount, int visibleItemCount, int startItemIndex) {
    if (
      renderer == nullptr || trackRect.w <= 0 || trackRect.h <= 0 || totalItemCount <= visibleItemCount || visibleItemCount <= 0
    ) {
      return;
    }

    fill_rect(renderer, trackRect, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xA0);
    draw_rect(renderer, trackRect, TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0x30);

    const int thumbHeight = std::max(24, (trackRect.h * visibleItemCount) / totalItemCount);
    const int maxThumbTravel = std::max(0, trackRect.h - thumbHeight);
    const int maxStartItem = std::max(1, totalItemCount - visibleItemCount);
    const int clampedStartItem = std::clamp(startItemIndex, 0, std::max(0, totalItemCount - visibleItemCount));
    const int thumbY = trackRect.y + ((maxThumbTravel * clampedStartItem) / maxStartItem);
    const SDL_Rect thumbRect {trackRect.x + 1, thumbY, std::max(1, trackRect.w - 2), thumbHeight};
    fill_rect(renderer, thumbRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xD0);
  }

  bool render_log_viewer_modal(  // NOSONAR(cpp:S3776,cpp:S107) modal rendering stays centralized to keep layout behavior consistent
    SDL_Renderer *renderer,
    TTF_Font *bodyFont,
    TTF_Font *smallFont,
    AssetTextureCache *assetCache,
    const ui::ShellViewModel &viewModel,
    int screenWidth,
    int screenHeight,
    int outerMargin
  ) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    fill_rect(renderer, {0, 0, screenWidth, screenHeight}, 0x00, 0x00, 0x00, 0xA6);

    const SDL_Rect modalRect = log_viewer_rect(viewModel, screenWidth, screenHeight, outerMargin);
    fill_rect(renderer, modalRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE, 0xF4);
    draw_rect(renderer, modalRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);

    if (!render_text_line_simple(renderer, bodyFont, viewModel.modalTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, modalRect.x + 18, modalRect.y + 16, modalRect.w - 36)) {
      return false;
    }

    int pathHeight = 0;
    if (!render_text_line_simple(renderer, smallFont, "Path: " + viewModel.logViewerPath, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, modalRect.x + 18, modalRect.y + 56, modalRect.w - 36, &pathHeight)) {
      return false;
    }

    const int hintHeight = std::max(30, TTF_FontLineSkip(smallFont) + 10);
    const SDL_Rect hintRect {modalRect.x + 18, modalRect.y + 56 + pathHeight + 8, modalRect.w - 36, hintHeight};
    if (const std::vector<ui::ShellFooterAction> logViewerActions = {
          {"change-view", "Change View", "icons\\button-x.svg", "icons\\button-y.svg", false},
          {"scroll", "Scroll", "icons\\button-lb.svg", "icons\\button-rb.svg", false},
          {"fast-scroll", "Fast Scroll", "icons\\button-lt.svg", "icons\\button-rt.svg", false},
          {"close", "Close", "icons\\button-a.svg", "icons\\button-b.svg", false},
        };
        !render_footer_actions(renderer, smallFont, assetCache, logViewerActions, hintRect)) {
      return false;
    }

    const int contentBottom = modalRect.y + modalRect.h - 18;
    const int requestedContentY = modalRect.y + 56 + pathHeight + hintHeight + 18;
    const int contentAreaY = std::min(requestedContentY, std::max(modalRect.y + 72, contentBottom - 1));
    const SDL_Rect contentRect {
      modalRect.x + 18,
      contentAreaY,
      std::max(1, modalRect.w - 36),
      std::max(1, contentBottom - contentAreaY),
    };
    fill_rect(renderer, contentRect, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0x70);

    struct LogViewerLayout {
      std::vector<const std::string *> visibleLines;
      std::size_t firstVisibleIndex = 0U;
    };

    const std::size_t maxOffset = viewModel.logViewerLines.size() > 1U ? viewModel.logViewerLines.size() - 1U : 0U;
    const std::size_t clampedOffset = std::min(viewModel.logViewerScrollOffset, maxOffset);
    auto build_log_viewer_layout = [&](int availableWidth) {  // NOSONAR(cpp:S1188) kept adjacent to modal layout state for readability
      LogViewerLayout layout {};
      if (viewModel.logViewerLines.empty()) {
        layout.visibleLines.push_back(nullptr);
        return layout;
      }

      int usedHeight = 0;
      std::size_t endIndex = viewModel.logViewerLines.size() > clampedOffset ? viewModel.logViewerLines.size() - clampedOffset : 0U;
      layout.firstVisibleIndex = endIndex;
      while (endIndex > 0U) {
        const std::string renderedLine = truncate_text_for_render(viewModel.logViewerLines[endIndex - 1U], LOG_VIEWER_MAX_RENDER_CHARACTERS);
        const int lineHeight = measure_wrapped_text_height(smallFont, renderedLine, std::max(1, availableWidth - 12)) + 4;
        if (!layout.visibleLines.empty() && usedHeight + lineHeight > contentRect.h - 8) {
          break;
        }
        layout.visibleLines.push_back(&viewModel.logViewerLines[endIndex - 1U]);
        usedHeight += lineHeight;
        --endIndex;
      }
      layout.firstVisibleIndex = endIndex;
      std::reverse(layout.visibleLines.begin(), layout.visibleLines.end());
      return layout;
    };

    constexpr int logViewerScrollbarWidth = 10;
    constexpr int logViewerScrollbarGap = 12;
    LogViewerLayout logViewerLayout = build_log_viewer_layout(contentRect.w);
    const bool overflow = !viewModel.logViewerLines.empty() && viewModel.logViewerLines.size() > logViewerLayout.visibleLines.size();
    if (overflow) {
      logViewerLayout = build_log_viewer_layout(std::max(1, contentRect.w - logViewerScrollbarWidth - logViewerScrollbarGap));
    }

    const SDL_Rect textRect {
      contentRect.x,
      contentRect.y,
      std::max(1, contentRect.w - (overflow ? logViewerScrollbarWidth + logViewerScrollbarGap : 0)),
      contentRect.h,
    };
    int contentCursorY = textRect.y + 6;
    if (const bool olderLinesAvailable = logViewerLayout.firstVisibleIndex > 0U; olderLinesAvailable) {
      if (!render_text_line_simple(renderer, smallFont, "Earlier lines above", {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, textRect.x + 6, contentCursorY, std::max(1, textRect.w - 12))) {
        return false;
      }
      contentCursorY += TTF_FontLineSkip(smallFont) + 4;
    }

    if (logViewerLayout.visibleLines.size() == 1U && logViewerLayout.visibleLines.front() == nullptr) {
      if (!render_text_line_simple(renderer, smallFont, "The log file is empty.", {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, textRect.x + 6, contentCursorY, std::max(1, textRect.w - 12))) {
        return false;
      }
    } else {
      for (const std::string *line : logViewerLayout.visibleLines) {
        int drawnHeight = 0;
        if (!render_text_line_simple(renderer, smallFont, truncate_text_for_render(*line, LOG_VIEWER_MAX_RENDER_CHARACTERS), {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, textRect.x + 6, contentCursorY, std::max(1, textRect.w - 12), &drawnHeight)) {
          return false;
        }
        contentCursorY += drawnHeight + 4;
      }
    }

    if (viewModel.logViewerScrollOffset > 0U && !render_text_line_simple(renderer, smallFont, "Newer lines below", {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, textRect.x + 6, std::max(textRect.y + 6, textRect.y + textRect.h - TTF_FontLineSkip(smallFont) - 6), std::max(1, textRect.w - 12))) {
      return false;
    }

    if (overflow) {
      render_vertical_scrollbar(
        renderer,
        {contentRect.x + contentRect.w - logViewerScrollbarWidth, contentRect.y, logViewerScrollbarWidth, contentRect.h},
        static_cast<int>(viewModel.logViewerLines.size()),
        static_cast<int>(std::max<std::size_t>(1U, logViewerLayout.visibleLines.size())),
        static_cast<int>(logViewerLayout.firstVisibleIndex)
      );
    }

    return true;
  }

  bool render_action_rows(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const std::vector<ui::ShellActionRow> &rows,
    const SDL_Rect &rect,
    int rowHeight
  ) {
    if (rows.empty()) {
      return true;
    }

    const int rowSpacing = 6;
    const int rowStep = rowHeight + rowSpacing;
    const auto visibleRowCount = static_cast<std::size_t>(std::max(1, (rect.h + rowSpacing) / std::max(1, rowStep)));
    std::size_t selectedIndex = 0U;
    bool selectedIndexFound = false;
    for (std::size_t index = 0; index < rows.size(); ++index) {
      if (rows[index].selected) {
        selectedIndex = index;
        selectedIndexFound = true;
        break;
      }
    }

    std::size_t startIndex = 0U;
    if (rows.size() > visibleRowCount && selectedIndexFound) {
      const std::size_t centeredOffset = visibleRowCount / 2U;
      startIndex = selectedIndex > centeredOffset ? selectedIndex - centeredOffset : 0U;
      const std::size_t maxStartIndex = rows.size() - visibleRowCount;
      if (startIndex > maxStartIndex) {
        startIndex = maxStartIndex;
      }
    }
    const std::size_t endIndex = std::min(rows.size(), startIndex + visibleRowCount);
    const bool overflow = rows.size() > visibleRowCount;
    const int scrollBarWidth = overflow ? 8 : 0;
    const int contentWidth = std::max(1, rect.w - (overflow ? scrollBarWidth + 10 : 0));

    int y = rect.y;
    for (std::size_t index = startIndex; index < endIndex; ++index) {
      const ui::ShellActionRow &row = rows[index];
      const SDL_Rect rowRect {rect.x, y, contentWidth, rowHeight};
      if (row.selected) {
        fill_rect(renderer, rowRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0x55);
        draw_rect(renderer, rowRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
      }

      const std::string label = row.checked ? "[x] " + row.label : row.label;
      if (const SDL_Color color = row.enabled ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF} : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}; !render_text_line_simple(renderer, font, label, color, rowRect.x + 12, rowRect.y + 8, rowRect.w - 24)) {
        return false;
      }

      y += rowStep;
    }

    if (overflow) {
      render_vertical_scrollbar(
        renderer,
        {rect.x + rect.w - scrollBarWidth, rect.y, scrollBarWidth, rect.h},
        static_cast<int>(rows.size()),
        static_cast<int>(visibleRowCount),
        static_cast<int>(startIndex)
      );
    }

    return true;
  }

  bool render_toolbar_button(
    SDL_Renderer *renderer,
    TTF_Font *font,
    TTF_Font *smallFont,
    AssetTextureCache *assetCache,
    const ui::ShellToolbarButton &button,
    const SDL_Rect &buttonRect
  ) {
    if (button.selected) {
      fill_rect(renderer, buttonRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0x55);
      draw_rect(renderer, buttonRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
    } else {
      fill_rect(renderer, buttonRect, PANEL_ALT_RED, PANEL_ALT_GREEN, PANEL_ALT_BLUE, 0xC8);
      draw_rect(renderer, buttonRect, TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0x50);
    }

    const int iconSize = std::max(18, buttonRect.h - 16);
    const SDL_Rect iconRect {buttonRect.x + 10, buttonRect.y + (buttonRect.h - iconSize) / 2, iconSize, iconSize};
    if (const bool renderedIcon = !button.iconAssetPath.empty() && render_asset_icon(renderer, assetCache, button.iconAssetPath, iconRect); !renderedIcon && !render_text_centered(renderer, font, button.glyph, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, iconRect)) {
      return false;
    }

    return render_text_line(renderer, smallFont, button.label, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, iconRect.x + iconRect.w + 8, buttonRect.y + std::max(6, (buttonRect.h - TTF_FontLineSkip(smallFont)) / 2), buttonRect.w - (iconRect.w + 26));
  }

  bool render_footer_actions(  // NOSONAR(cpp:S3776) footer chip layout is intentionally centralized for all shell surfaces
    SDL_Renderer *renderer,
    TTF_Font *font,
    AssetTextureCache *assetCache,
    const std::vector<ui::ShellFooterAction> &actions,
    const SDL_Rect &footerRect
  ) {
    int cursorX = footerRect.x + 16;
    const int availableRight = footerRect.x + footerRect.w - 16;
    const int chipHeight = std::max(30, footerRect.h - 18);
    const int chipY = footerRect.y + (footerRect.h - chipHeight) / 2;

    for (const ui::ShellFooterAction &action : actions) {
      int labelWidth = 0;
      if (int labelHeight = 0; TTF_SizeUTF8(font, action.label.c_str(), &labelWidth, &labelHeight) != 0) {
        labelWidth = static_cast<int>(action.label.size()) * 8;
      }

      const int iconSize = (action.iconAssetPath.empty() && action.secondaryIconAssetPath.empty()) ? 0 : std::max(18, chipHeight - 14);
      const int iconCount = (action.iconAssetPath.empty() ? 0 : 1) + (action.secondaryIconAssetPath.empty() ? 0 : 1);
      const int iconBlockWidth = iconCount == 0 ? 0 : (iconSize * iconCount) + ((iconCount - 1) * 4);
      const int chipWidth = 18 + iconBlockWidth + (iconBlockWidth > 0 ? 8 : 0) + labelWidth + 18;
      if (cursorX + chipWidth > availableRight) {
        break;
      }

      const SDL_Rect chipRect {cursorX, chipY, chipWidth, chipHeight};

      int contentX = chipRect.x + 10;
      if (iconSize > 0) {
        if (!action.iconAssetPath.empty()) {
          const SDL_Rect iconRect {contentX, chipRect.y + (chipRect.h - iconSize) / 2, iconSize, iconSize};
          render_asset_icon(renderer, assetCache, action.iconAssetPath, iconRect);
          contentX += iconSize + 4;
        }
        if (!action.secondaryIconAssetPath.empty()) {
          const SDL_Rect iconRect {contentX, chipRect.y + (chipRect.h - iconSize) / 2, iconSize, iconSize};
          render_asset_icon(renderer, assetCache, action.secondaryIconAssetPath, iconRect);
          contentX += iconSize + 4;
        }
        contentX += 4;
      }

      if (!render_text_line(renderer, font, action.label, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, contentX, chipRect.y + std::max(6, (chipRect.h - TTF_FontLineSkip(font)) / 2), chipRect.w - (contentX - chipRect.x) - 10)) {
        return false;
      }

      cursorX += chipWidth + 12;
    }

    return true;
  }

  bool render_notification(  // NOSONAR(cpp:S107) notification layout helper keeps the render inputs explicit
    SDL_Renderer *renderer,
    TTF_Font *titleFont,
    TTF_Font *bodyFont,
    AssetTextureCache *assetCache,
    const ui::ShellNotification &notification,
    int screenWidth,
    int footerTop,
    int outerMargin
  ) {
    if (notification.message.empty()) {
      return true;
    }

    const int notificationWidth = std::min(420, std::max(300, screenWidth / 3));
    const int innerWidth = notificationWidth - 28;
    const int titleHeight = notification.title.empty() ? 0 : TTF_FontLineSkip(titleFont);
    const int messageHeight = measure_wrapped_text_height(bodyFont, notification.message, innerWidth);
    const int actionHeight = notification.actions.empty() ? 0 : std::max(30, TTF_FontLineSkip(bodyFont) + 10);
    const int notificationHeight = 14 + titleHeight + (titleHeight > 0 ? 8 : 0) + messageHeight + (actionHeight > 0 ? 14 + actionHeight : 0) + 14;
    const SDL_Rect notificationRect {
      screenWidth - outerMargin - notificationWidth,
      std::max(outerMargin, footerTop - notificationHeight - 14),
      notificationWidth,
      notificationHeight,
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    fill_rect(renderer, notificationRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE, 0xF0);
    draw_rect(renderer, notificationRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);

    int cursorY = notificationRect.y + 12;
    if (!notification.title.empty()) {
      if (!render_text_line_simple(renderer, titleFont, notification.title, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, notificationRect.x + 14, cursorY, innerWidth)) {
        return false;
      }
      cursorY += titleHeight + 8;
    }

    int messageDrawnHeight = 0;
    if (!render_text_line(renderer, bodyFont, notification.message, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, notificationRect.x + 14, cursorY, innerWidth, &messageDrawnHeight)) {
      return false;
    }
    cursorY += messageDrawnHeight;

    if (!notification.actions.empty()) {
      cursorY += 10;
      if (!render_footer_actions(renderer, bodyFont, assetCache, notification.actions, {notificationRect.x + 10, cursorY, notificationRect.w - 20, actionHeight})) {
        return false;
      }
    }

    return true;
  }

  struct GridViewport {
    int totalRowCount = 0;
    int visibleRowCount = 0;
    int startRow = 0;
    int scrollbarWidth = 0;
  };

  GridViewport calculate_grid_viewport(std::size_t itemCount, std::size_t columnCount, std::size_t selectedIndex, int availableHeight, int preferredRowHeight, int tileGap) {
    GridViewport viewport {};
    if (itemCount == 0U || columnCount == 0U || availableHeight <= 0) {
      return viewport;
    }

    viewport.totalRowCount = static_cast<int>((itemCount + columnCount - 1U) / columnCount);
    viewport.visibleRowCount = std::max(1, (availableHeight + tileGap) / std::max(1, preferredRowHeight + tileGap));
    viewport.visibleRowCount = std::min(viewport.visibleRowCount, viewport.totalRowCount);
    const auto selectedRow = static_cast<int>(std::min(selectedIndex, itemCount - 1U) / columnCount);
    viewport.startRow = std::clamp(selectedRow + 1 - viewport.visibleRowCount, 0, viewport.totalRowCount - viewport.visibleRowCount);
    viewport.scrollbarWidth = viewport.totalRowCount > viewport.visibleRowCount ? 10 : 0;
    return viewport;
  }

  void render_grid_scrollbar(SDL_Renderer *renderer, const SDL_Rect &trackRect, const GridViewport &viewport) {
    render_vertical_scrollbar(renderer, trackRect, viewport.totalRowCount, viewport.visibleRowCount, viewport.startRow);
  }

  std::size_t selected_host_tile_index(const std::vector<ui::ShellHostTile> &tiles) {
    for (std::size_t index = 0; index < tiles.size(); ++index) {
      if (tiles[index].selected) {
        return index;
      }
    }

    return 0U;
  }

  std::size_t selected_app_tile_index(const std::vector<ui::ShellAppTile> &tiles) {
    for (std::size_t index = 0; index < tiles.size(); ++index) {
      if (tiles[index].selected) {
        return index;
      }
    }

    return 0U;
  }

  input::UiCommand translate_controller_button(Uint8 button) {
    switch (button) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_up);
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_down);
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_left);
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::dpad_right);
      case SDL_CONTROLLER_BUTTON_A:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::a);
      case SDL_CONTROLLER_BUTTON_B:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::b);
      case SDL_CONTROLLER_BUTTON_X:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::x);
      case SDL_CONTROLLER_BUTTON_Y:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::y);
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::left_shoulder);
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::right_shoulder);
      case SDL_CONTROLLER_BUTTON_START:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::start);
      case SDL_CONTROLLER_BUTTON_BACK:
        return input::map_gamepad_button_to_ui_command(input::GamepadButton::back);
      default:
        return input::UiCommand::none;
    }
  }

  input::UiCommand translate_keyboard_key(SDL_Keycode key, Uint16 modifiers) {
    const bool shiftPressed = (modifiers & KMOD_SHIFT) != 0;

    switch (key) {
      case SDLK_UP:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::up, shiftPressed);
      case SDLK_DOWN:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::down, shiftPressed);
      case SDLK_LEFT:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::left, shiftPressed);
      case SDLK_RIGHT:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::right, shiftPressed);
      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::enter, shiftPressed);
      case SDLK_ESCAPE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::escape, shiftPressed);
      case SDLK_BACKSPACE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::backspace, shiftPressed);
      case SDLK_DELETE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::delete_key, shiftPressed);
      case SDLK_SPACE:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::space, shiftPressed);
      case SDLK_TAB:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::tab, shiftPressed);
      case SDLK_PAGEUP:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::page_up, shiftPressed);
      case SDLK_PAGEDOWN:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::page_down, shiftPressed);
      case SDLK_i:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::i, shiftPressed);
      case SDLK_m:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::m, shiftPressed);
      case SDLK_F3:
        return input::map_keyboard_key_to_ui_command(input::KeyboardKey::f3, shiftPressed);
      default:
        return input::UiCommand::none;
    }
  }

  input::UiCommand translate_trigger_axis(const SDL_ControllerAxisEvent &event, bool *leftTriggerPressed, bool *rightTriggerPressed) {
    if (leftTriggerPressed == nullptr || rightTriggerPressed == nullptr) {
      return input::UiCommand::none;
    }

    const bool thresholdCrossed = event.value >= TRIGGER_PAGE_SCROLL_THRESHOLD;

    switch (event.axis) {
      case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        if (thresholdCrossed && !*leftTriggerPressed) {
          *leftTriggerPressed = true;
          return input::UiCommand::fast_previous_page;
        }
        if (!thresholdCrossed) {
          *leftTriggerPressed = false;
        }
        break;
      case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        if (thresholdCrossed && !*rightTriggerPressed) {
          *rightTriggerPressed = true;
          return input::UiCommand::fast_next_page;
        }
        if (!thresholdCrossed) {
          *rightTriggerPressed = false;
        }
        break;
      default:
        break;
    }

    return input::UiCommand::none;
  }

  /**
   * @brief Tracks the repeat timing for one controller navigation direction.
   */
  struct ControllerNavigationHoldState {
    bool active = false;  ///< True while the direction remains held past activation.
    Uint32 activatedTick = 0U;  ///< Tick count when the direction first became active.
    Uint32 lastRepeatTick = 0U;  ///< Tick count when the last repeat command fired.
  };

  /**
   * @brief Clears the held-repeat bookkeeping for every controller navigation direction.
   *
   * @param upState Repeat state for up navigation.
   * @param downState Repeat state for down navigation.
   * @param leftState Repeat state for left navigation.
   * @param rightState Repeat state for right navigation.
   */
  void reset_controller_navigation_hold_states(
    ControllerNavigationHoldState *upState,
    ControllerNavigationHoldState *downState,
    ControllerNavigationHoldState *leftState,
    ControllerNavigationHoldState *rightState
  ) {
    if (upState != nullptr) {
      *upState = {};
    }
    if (downState != nullptr) {
      *downState = {};
    }
    if (leftState != nullptr) {
      *leftState = {};
    }
    if (rightState != nullptr) {
      *rightState = {};
    }
  }

  /**
   * @brief Returns whether a UI command is part of directional navigation and should keep repeat state alive.
   *
   * @param command UI command to classify.
   * @return True when the command represents directional navigation.
   */
  bool is_navigation_command(input::UiCommand command) {
    switch (command) {
      case input::UiCommand::move_up:
      case input::UiCommand::move_down:
      case input::UiCommand::move_left:
      case input::UiCommand::move_right:
        return true;
      case input::UiCommand::activate:
      case input::UiCommand::confirm:
      case input::UiCommand::back:
      case input::UiCommand::delete_character:
      case input::UiCommand::open_context_menu:
      case input::UiCommand::previous_page:
      case input::UiCommand::next_page:
      case input::UiCommand::fast_previous_page:
      case input::UiCommand::fast_next_page:
      case input::UiCommand::toggle_overlay:
      case input::UiCommand::none:
        return false;
    }

    return false;
  }

  /**
   * @brief Returns whether a negative stick direction remains active, using hysteresis to avoid jitter near center.
   *
   * @param value Current stick axis value.
   * @param state Existing hold state for the direction.
   * @return True when the negative navigation direction should be treated as active.
   */
  bool axis_value_is_negative_navigation_active(Sint16 value, const ControllerNavigationHoldState *state) {
    const Sint16 threshold = state != nullptr && state->active ? LEFT_STICK_NAVIGATION_RELEASE_THRESHOLD : LEFT_STICK_NAVIGATION_THRESHOLD;
    return value <= -threshold;
  }

  /**
   * @brief Returns whether a positive stick direction remains active, using hysteresis to avoid jitter near center.
   *
   * @param value Current stick axis value.
   * @param state Existing hold state for the direction.
   * @return True when the positive navigation direction should be treated as active.
   */
  bool axis_value_is_positive_navigation_active(Sint16 value, const ControllerNavigationHoldState *state) {
    const Sint16 threshold = state != nullptr && state->active ? LEFT_STICK_NAVIGATION_RELEASE_THRESHOLD : LEFT_STICK_NAVIGATION_THRESHOLD;
    return value >= threshold;
  }

  /**
   * @brief Returns the held-repeat state associated with a directional UI command.
   *
   * @param command UI command whose hold state should be returned.
   * @param upState Repeat state for up navigation.
   * @param downState Repeat state for down navigation.
   * @param leftState Repeat state for left navigation.
   * @param rightState Repeat state for right navigation.
   * @return Pointer to the matching hold state, or null when the command is not directional.
   */
  ControllerNavigationHoldState *controller_navigation_hold_state_for_command(
    input::UiCommand command,
    ControllerNavigationHoldState *upState,
    ControllerNavigationHoldState *downState,
    ControllerNavigationHoldState *leftState,
    ControllerNavigationHoldState *rightState
  ) {
    switch (command) {
      case input::UiCommand::move_up:
        return upState;
      case input::UiCommand::move_down:
        return downState;
      case input::UiCommand::move_left:
        return leftState;
      case input::UiCommand::move_right:
        return rightState;
      case input::UiCommand::activate:
      case input::UiCommand::confirm:
      case input::UiCommand::back:
      case input::UiCommand::delete_character:
      case input::UiCommand::open_context_menu:
      case input::UiCommand::previous_page:
      case input::UiCommand::next_page:
      case input::UiCommand::fast_previous_page:
      case input::UiCommand::fast_next_page:
      case input::UiCommand::toggle_overlay:
      case input::UiCommand::none:
        return nullptr;
    }

    return nullptr;
  }

  /**
   * @brief Arms held-repeat timing for a freshly pressed navigation direction.
   *
   * @param now Current SDL tick count.
   * @param command Directional command that was just pressed.
   * @param upState Repeat state for up navigation.
   * @param downState Repeat state for down navigation.
   * @param leftState Repeat state for left navigation.
   * @param rightState Repeat state for right navigation.
   */
  void seed_controller_navigation_hold_state(
    Uint32 now,
    input::UiCommand command,
    ControllerNavigationHoldState *upState,
    ControllerNavigationHoldState *downState,
    ControllerNavigationHoldState *leftState,
    ControllerNavigationHoldState *rightState
  ) {
    if (command == input::UiCommand::move_up && downState != nullptr) {
      *downState = {};
    } else if (command == input::UiCommand::move_down && upState != nullptr) {
      *upState = {};
    } else if (command == input::UiCommand::move_left && rightState != nullptr) {
      *rightState = {};
    } else if (command == input::UiCommand::move_right && leftState != nullptr) {
      *leftState = {};
    }

    if (ControllerNavigationHoldState *state = controller_navigation_hold_state_for_command(command, upState, downState, leftState, rightState); state != nullptr) {
      state->active = true;
      state->activatedTick = now;
      state->lastRepeatTick = now;
    }
  }

  /**
   * @brief Clears held-repeat timing for a released navigation direction.
   *
   * @param command Directional command that was just released.
   * @param upState Repeat state for up navigation.
   * @param downState Repeat state for down navigation.
   * @param leftState Repeat state for left navigation.
   * @param rightState Repeat state for right navigation.
   */
  void release_controller_navigation_hold_state(
    input::UiCommand command,
    ControllerNavigationHoldState *upState,
    ControllerNavigationHoldState *downState,
    ControllerNavigationHoldState *leftState,
    ControllerNavigationHoldState *rightState
  ) {
    if (ControllerNavigationHoldState *state = controller_navigation_hold_state_for_command(command, upState, downState, leftState, rightState); state != nullptr) {
      *state = {};
    }
  }

  /**
   * @brief Returns whether the controller is still holding any D-pad or left-stick navigation input.
   *
   * @param controller SDL game controller to inspect.
   * @return True when any navigation direction is still held.
   */
  bool is_controller_navigation_active(SDL_GameController *controller) {
    if (controller == nullptr) {
      return false;
    }

    const Sint16 leftStickX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 leftStickY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    return SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0 || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0 ||
           SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0 || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0 ||
           leftStickY <= -LEFT_STICK_NAVIGATION_RELEASE_THRESHOLD || leftStickY >= LEFT_STICK_NAVIGATION_RELEASE_THRESHOLD || leftStickX <= -LEFT_STICK_NAVIGATION_RELEASE_THRESHOLD ||
           leftStickX >= LEFT_STICK_NAVIGATION_RELEASE_THRESHOLD;
  }

  input::UiCommand update_controller_navigation_hold_state(bool active, Uint32 now, input::UiCommand command, ControllerNavigationHoldState *state) {
    if (state == nullptr || command == input::UiCommand::none) {
      return input::UiCommand::none;
    }

    if (!active) {
      *state = {};
      return input::UiCommand::none;
    }

    if (!state->active) {
      state->active = true;
      state->activatedTick = now;
      state->lastRepeatTick = now;
      return command;
    }

    if (now - state->activatedTick < CONTROLLER_NAVIGATION_INITIAL_REPEAT_MILLISECONDS || now - state->lastRepeatTick < CONTROLLER_NAVIGATION_REPEAT_MILLISECONDS) {
      return input::UiCommand::none;
    }

    state->lastRepeatTick = now;
    return command;
  }

  input::UiCommand poll_controller_navigation(
    SDL_GameController *controller,
    Uint32 now,
    ControllerNavigationHoldState *upState,
    ControllerNavigationHoldState *downState,
    ControllerNavigationHoldState *leftState,
    ControllerNavigationHoldState *rightState
  ) {
    if (controller == nullptr) {
      reset_controller_navigation_hold_states(upState, downState, leftState, rightState);
      return input::UiCommand::none;
    }

    const Sint16 leftStickX = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 leftStickY = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    const bool moveUpActive = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0 || axis_value_is_negative_navigation_active(leftStickY, upState);
    const bool moveDownActive = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0 || axis_value_is_positive_navigation_active(leftStickY, downState);
    const bool moveLeftActive = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0 || axis_value_is_negative_navigation_active(leftStickX, leftState);
    const bool moveRightActive = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0 || axis_value_is_positive_navigation_active(leftStickX, rightState);

    if (const input::UiCommand command = update_controller_navigation_hold_state(moveUpActive, now, input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_up), upState); command != input::UiCommand::none) {
      return command;
    }
    if (const input::UiCommand command = update_controller_navigation_hold_state(moveDownActive, now, input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_down), downState); command != input::UiCommand::none) {
      return command;
    }
    if (const input::UiCommand command = update_controller_navigation_hold_state(moveLeftActive, now, input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_left), leftState); command != input::UiCommand::none) {
      return command;
    }
    return update_controller_navigation_hold_state(moveRightActive, now, input::map_gamepad_axis_direction_to_ui_command(input::GamepadAxisDirection::left_stick_right), rightState);
  }

  void log_app_update(const app::ClientState &state, const app::AppUpdate &update) {
    if (!update.activatedItemId.empty()) {
      logging::logger.info("ui", "Activated menu item: " + update.activatedItemId);
    }
    if (update.screenChanged) {
      logging::logger.info("ui", std::string("Switched screen to ") + app::to_string(state.activeScreen));
    }
    if (update.overlayVisibilityChanged) {
      logging::logger.info("overlay", state.overlayVisible ? "Overlay enabled" : "Overlay disabled");
    }
    if (update.exitRequested) {
      logging::logger.info("app", "Exit requested from shell");
    }
  }

  app::HostRecord *find_persisted_host_record(std::vector<app::HostRecord> &hosts, const std::string &address, uint16_t port) {
    const auto iterator = std::find_if(hosts.begin(), hosts.end(), [&address, port](const app::HostRecord &host) {
      return app::host_matches_endpoint(host, address, port);
    });
    return iterator == hosts.end() ? nullptr : &(*iterator);
  }

  void merge_host_for_persistence(app::HostRecord *targetHost, const app::HostRecord &sourceHost) {
    if (targetHost == nullptr) {
      return;
    }

    targetHost->displayName = sourceHost.displayName;
    targetHost->address = sourceHost.address;
    targetHost->port = sourceHost.port;
    targetHost->pairingState = sourceHost.pairingState;
    targetHost->reachability = sourceHost.reachability;
    targetHost->activeAddress = sourceHost.activeAddress;
    targetHost->uuid = sourceHost.uuid;
    targetHost->localAddress = sourceHost.localAddress;
    targetHost->remoteAddress = sourceHost.remoteAddress;
    targetHost->ipv6Address = sourceHost.ipv6Address;
    targetHost->manualAddress = sourceHost.manualAddress;
    targetHost->macAddress = sourceHost.macAddress;
    targetHost->httpsPort = sourceHost.httpsPort;
    targetHost->runningGameId = sourceHost.runningGameId;
    targetHost->apps = sourceHost.apps;
    targetHost->appListState = sourceHost.appListState;
    targetHost->appListStatusMessage = sourceHost.appListStatusMessage;
    targetHost->resolvedHttpPort = sourceHost.resolvedHttpPort;
    targetHost->appListContentHash = sourceHost.appListContentHash;
    targetHost->lastAppListRefreshTick = sourceHost.lastAppListRefreshTick;
  }

  bool ensure_hosts_loaded_for_active_screen(app::ClientState &state) {
    if (state.activeScreen != app::ScreenId::hosts || state.hostsLoaded) {
      return true;
    }

    const startup::LoadSavedHostsResult loadedHosts = startup::load_saved_hosts();
    for (const std::string &warning : loadedHosts.warnings) {
      logging::logger.warn("hosts", warning);
    }
    app::replace_hosts(state, loadedHosts.hosts, state.statusMessage);
    return true;
  }

  bool persist_hosts(app::ClientState &state) {
    std::vector<app::HostRecord> hostsToSave;
    if (state.hostsLoaded) {
      hostsToSave = state.hosts;
    } else if (state.activeHostLoaded) {
      const startup::LoadSavedHostsResult loadedHosts = startup::load_saved_hosts();
      for (const std::string &warning : loadedHosts.warnings) {
        logging::logger.warn("hosts", warning);
      }
      hostsToSave = loadedHosts.hosts;
      if (app::HostRecord *host = find_persisted_host_record(hostsToSave, state.activeHost.address, state.activeHost.port); host != nullptr) {
        merge_host_for_persistence(host, state.activeHost);
      } else {
        hostsToSave.push_back(state.activeHost);
      }
    } else {
      hostsToSave = state.hosts;
    }

    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(hostsToSave);
    if (saveResult.success) {
      state.hostsDirty = false;
      logging::logger.info("hosts", "Saved host records");
      return true;
    }

    logging::logger.error("hosts", saveResult.errorMessage);
    return false;
  }

  void persist_hosts_if_needed(app::ClientState &state, const app::AppUpdate &update) {
    if (!update.hostsChanged) {
      return;
    }

    persist_hosts(state);
  }

  app::AppSettings persistent_settings_from_state(const app::ClientState &state) {
    return {
      state.loggingLevel,
      state.xemuConsoleLoggingLevel,
      state.logViewerPlacement,
    };
  }

  void persist_settings_if_needed(app::ClientState &state, const app::AppUpdate &update) {
    if (!update.settingsChanged || !state.settingsDirty) {
      return;
    }

    const app::SaveAppSettingsResult saveResult = app::save_app_settings(persistent_settings_from_state(state));
    if (saveResult.success) {
      state.settingsDirty = false;
      logging::logger.info("settings", "Saved Moonlight settings");
      return;
    }

    logging::logger.error("settings", saveResult.errorMessage);
  }

  void apply_server_info_to_host(app::ClientState &state, const std::string &address, uint16_t port, const network::HostPairingServerInfo &serverInfo) {  // NOSONAR(cpp:S3776) host metadata updates intentionally stay grouped with pairing-state transitions
    auto apply_to_host = [&](app::HostRecord &host) {
      bool persistedMetadataChanged = false;
      const bool hostRequiresManualPairing = app::host_requires_manual_pairing(state, address, port);
      if (!serverInfo.hostName.empty()) {
        persistedMetadataChanged = persistedMetadataChanged || host.displayName != serverInfo.hostName;
        host.displayName = serverInfo.hostName;
      }
      host.reachability = app::HostReachability::online;
      host.activeAddress = network::resolve_reachable_address(address, serverInfo);
      host.uuid = serverInfo.uuid;
      host.localAddress = serverInfo.localAddress;
      host.remoteAddress = serverInfo.remoteAddress;
      host.ipv6Address = serverInfo.ipv6Address;
      host.manualAddress = address;
      host.macAddress = serverInfo.macAddress;
      host.resolvedHttpPort = serverInfo.httpPort;
      host.httpsPort = serverInfo.httpsPort;
      host.runningGameId = serverInfo.runningGameId;
      if (serverInfo.pairingStatusCurrentClientKnown) {
        const bool clientIsEffectivelyPaired = serverInfo.pairingStatusCurrentClient && !hostRequiresManualPairing;
        const app::PairingState resolvedPairingState = clientIsEffectivelyPaired ? app::PairingState::paired : app::PairingState::not_paired;
        persistedMetadataChanged = persistedMetadataChanged || host.pairingState != resolvedPairingState;
        host.pairingState = resolvedPairingState;
        if (!clientIsEffectivelyPaired) {
          host.apps.clear();
          host.appListState = hostRequiresManualPairing ? app::HostAppListState::idle : app::HostAppListState::failed;
          host.appListStatusMessage = hostRequiresManualPairing ? "This host was removed locally. Pair it again to restore apps and authorization." : "The host reports that this client is no longer paired. Pair the host again.";
          host.appListContentHash = 0;
          host.lastAppListRefreshTick = 0;
          state.selectedAppIndex = 0U;
          if (state.activeScreen == app::ScreenId::apps && state.activeHostLoaded && &host == &state.activeHost) {
            state.statusMessage = host.appListStatusMessage;
          }
        }
      }
      state.hostsDirty = state.hostsDirty || persistedMetadataChanged;
    };

    for (app::HostRecord &host : state.hosts) {
      if (!app::host_matches_endpoint(host, address, port)) {
        continue;
      }
      apply_to_host(host);
      return;
    }

    if (state.activeHostLoaded && app::host_matches_endpoint(state.activeHost, address, port)) {
      apply_to_host(state.activeHost);
    }
  }

  std::string display_name_for_saved_file(const app::ClientState &state, const std::string &path) {
    for (const startup::SavedFileEntry &savedFile : state.savedFiles) {
      if (savedFile.path == path) {
        return savedFile.displayName;
      }
    }
    return path;
  }

  std::string cover_art_cache_key_from_path(const std::string &path) {
    if (const std::string coverArtRoot = startup::default_cover_art_cache_root(); coverArtRoot.empty() || path.size() <= coverArtRoot.size() || path.rfind(coverArtRoot, 0U) != 0U) {
      return {};
    }

    const std::string fileName = platform::file_name_from_path(path);
    if (fileName.empty()) {
      return {};
    }

    if (fileName.size() <= 4U || fileName.substr(fileName.size() - 4U) != ".bin") {
      return {};
    }
    return fileName.substr(0, fileName.size() - 4U);
  }

  void clear_deleted_cover_art_flag(app::ClientState &state, std::string_view cacheKey) {
    if (cacheKey.empty()) {
      return;
    }

    for (app::HostRecord &host : state.hosts) {
      for (app::HostAppRecord &appRecord : host.apps) {
        if (appRecord.boxArtCacheKey == cacheKey) {
          appRecord.boxArtCached = false;
        }
      }
    }
  }

  void refresh_saved_files_if_needed(app::ClientState &state) {
    if (state.activeScreen != app::ScreenId::settings || !state.savedFilesDirty) {
      return;
    }

    const startup::ListSavedFilesResult savedFiles = startup::list_saved_files();
    for (const std::string &warning : savedFiles.warnings) {
      logging::logger.warn("storage", warning);
    }
    app::replace_saved_files(state, savedFiles.files);
  }

  void release_page_resources_for_screen(app::ScreenId previousScreen, app::ScreenId nextScreen, CoverArtTextureCache *coverArtTextureCache, KeypadModalLayoutCache *keypadModalLayoutCache) {
    if (previousScreen != nextScreen && previousScreen == app::ScreenId::apps) {
      clear_cover_art_texture_cache(coverArtTextureCache);
    }
    if (previousScreen != nextScreen && previousScreen == app::ScreenId::add_host) {
      clear_keypad_modal_layout_cache(keypadModalLayoutCache);
    }
  }

  void delete_saved_file_if_requested(app::ClientState &state, const app::AppUpdate &update, CoverArtTextureCache *coverArtTextureCache) {
    if (!update.savedFileDeleteRequested) {
      return;
    }

    if (std::string errorMessage; !startup::delete_saved_file(update.savedFileDeletePath, &errorMessage)) {
      state.statusMessage = errorMessage;
      logging::logger.warn("storage", errorMessage);
      return;
    }

    const std::string deletedDisplayName = display_name_for_saved_file(state, update.savedFileDeletePath);
    const std::string deletedCoverArtCacheKey = cover_art_cache_key_from_path(update.savedFileDeletePath);
    clear_deleted_cover_art_flag(state, deletedCoverArtCacheKey);
    clear_cover_art_texture(coverArtTextureCache, deletedCoverArtCacheKey);
    state.savedFilesDirty = true;
    state.statusMessage = "Deleted saved file " + deletedDisplayName;
    logging::logger.info("storage", state.statusMessage);
  }

  void delete_host_data_if_requested(app::ClientState &state, const app::AppUpdate &update, CoverArtTextureCache *coverArtTextureCache) {
    if (!update.hostDeleteCleanupRequested) {
      return;
    }

    std::size_t deletedCoverArtCount = 0U;
    for (const std::string &cacheKey : update.deletedHostCoverArtCacheKeys) {
      if (std::string errorMessage; !startup::delete_cover_art(cacheKey, &errorMessage)) {
        logging::logger.warn("storage", errorMessage);
      } else {
        ++deletedCoverArtCount;
      }
      clear_cover_art_texture(coverArtTextureCache, cacheKey);
    }

    bool deletedClientIdentity = false;
    if (update.deletedHostWasPaired) {
      const bool pairedHostsRemain = std::any_of(state.hosts.begin(), state.hosts.end(), [](const app::HostRecord &host) {
        return host.pairingState == app::PairingState::paired;
      });
      if (!pairedHostsRemain) {
        std::string errorMessage;
        if (!startup::delete_client_identity(&errorMessage)) {
          logging::logger.warn("storage", errorMessage);
        } else {
          deletedClientIdentity = true;
        }
      } else {
        logging::logger.info("storage", "Retained the shared pairing identity because other paired hosts still exist");
      }
    }

    state.statusMessage = "Deleted saved host";
    if (deletedCoverArtCount > 0U) {
      state.statusMessage += " and cleared " + std::to_string(deletedCoverArtCount) + " cached asset" + (deletedCoverArtCount == 1U ? std::string {} : "s");
    }
    if (deletedClientIdentity) {
      state.statusMessage += " and reset local pairing identity";
    }
    logging::logger.info("storage", state.statusMessage);
  }

  void factory_reset_if_requested(app::ClientState &state, const app::AppUpdate &update, CoverArtTextureCache *coverArtTextureCache) {
    if (!update.factoryResetRequested) {
      return;
    }

    if (std::string errorMessage; !startup::delete_all_saved_files(&errorMessage)) {
      state.statusMessage = errorMessage;
      logging::logger.warn("storage", errorMessage);
      return;
    }

    state.hosts.clear();
    state = app::create_initial_state();
    state.savedFiles.clear();
    state.savedFilesDirty = true;
    state.statusMessage = "Factory reset completed";
    clear_cover_art_texture_cache(coverArtTextureCache);
    app::set_log_file_path(state, logging::default_log_file_path());
    logging::logger.info("storage", state.statusMessage);
  }

  bool try_load_saved_pairing_identity(network::PairingIdentity *identity) {
    const startup::LoadClientIdentityResult loadedIdentity = startup::load_client_identity();
    if (!loadedIdentity.fileFound || !network::is_valid_pairing_identity(loadedIdentity.identity)) {
      return false;
    }

    if (identity != nullptr) {
      *identity = loadedIdentity.identity;
    }
    return true;
  }

  bool load_saved_pairing_identity_for_streaming(network::PairingIdentity *identity, std::string *errorMessage) {
    const startup::LoadClientIdentityResult loadedIdentity = startup::load_client_identity();
    if (!loadedIdentity.fileFound || !network::is_valid_pairing_identity(loadedIdentity.identity)) {
      return platform::append_error(errorMessage, "No valid paired client identity is available. Pair the host again before browsing apps.");
    }

    if (identity != nullptr) {
      *identity = loadedIdentity.identity;
    }
    return true;
  }

  bool test_tcp_host_connection(
    const std::string &address,
    uint16_t port,
    const network::PairingIdentity *clientIdentity,
    std::string *message,
    network::HostPairingServerInfo *serverInfoResult = nullptr
  ) {
    if (!network::runtime_network_ready()) {
      if (message != nullptr) {
        *message = network::runtime_network_status().summary;
      }
      return false;
    }

    network::HostPairingServerInfo serverInfo {};
    if (std::string errorMessage; !network::query_server_info(address, port, clientIdentity, &serverInfo, &errorMessage)) {
      if (message != nullptr) {
        *message = std::move(errorMessage);
      }
      return false;
    }

    if (serverInfoResult != nullptr) {
      *serverInfoResult = serverInfo;
    }

    if (message != nullptr) {
      *message = "Received /serverinfo from " + address + ":" + std::to_string(serverInfo.httpPort) + " and discovered HTTPS pairing on port " + std::to_string(serverInfo.httpsPort);
      if (serverInfo.pairingStatusCurrentClientKnown) {
        *message += serverInfo.pairingStatusCurrentClient ? "; the current client is paired and authorized" : "; the current client is no longer paired or authorized";
      }
    }
    return true;
  }

  struct PairingAttemptState {
    SDL_Thread *thread;
    std::atomic<bool> completed;
    std::atomic<bool> discardResult;
    std::atomic<bool> cancelRequested;
    network::HostPairingRequest request;
    network::HostPairingResult result;

    struct DeferredLogEntry {
      logging::LogLevel level;
      std::string message;
    };

    std::vector<DeferredLogEntry> deferredLogs;
  };

  struct PairingTaskState {
    std::unique_ptr<PairingAttemptState> activeAttempt;
    std::vector<std::unique_ptr<PairingAttemptState>> retiredAttempts;
  };

  struct AppListTaskState {
    SDL_Thread *thread = nullptr;
    std::atomic<bool> completed = false;
    std::string address;
    uint16_t port = 0;
    bool serverInfoAvailable = false;
    bool success = false;
    uint64_t appListContentHash = 0;
    std::string message;
    network::HostPairingServerInfo serverInfo;
    std::vector<app::HostAppRecord> apps;
  };

  struct AppArtTaskState {
    SDL_Thread *thread = nullptr;
    std::atomic<bool> completed = false;
    std::string address;
    uint16_t port = 0;
    std::vector<app::HostAppRecord> apps;
    std::vector<int> cachedAppIds;
    std::size_t failureCount = 0;
  };

  struct HostProbeTaskState {
    struct ProbeWorkerState {
      SDL_Thread *thread = nullptr;
      std::atomic<bool> completed = false;
      std::string address;
      uint16_t port = 0;
      bool success = false;
      network::HostPairingServerInfo serverInfo;
      const network::PairingIdentity *clientIdentity = nullptr;
      ui::HostProbeResultQueue *resultQueue = nullptr;
    };

    std::vector<std::unique_ptr<ProbeWorkerState>> workers;
    network::PairingIdentity clientIdentity;
    bool clientIdentityAvailable = false;
    ui::HostProbeResultQueue resultQueue;
    std::size_t onlineCount = 0U;
    std::size_t offlineCount = 0U;
    bool metadataChanged = false;
  };

  void reset_pairing_attempt(PairingAttemptState *attempt) {
    if (attempt == nullptr) {
      return;
    }

    attempt->thread = nullptr;
    attempt->completed.store(false);
    attempt->discardResult.store(false);
    attempt->cancelRequested.store(false);
    attempt->request = {};
    attempt->result = {false, false, {}};
    attempt->deferredLogs.clear();
  }

  void reset_pairing_task(PairingTaskState *task) {
    if (task == nullptr) {
      return;
    }

    task->activeAttempt.reset();
    task->retiredAttempts.clear();
  }

  bool pairing_task_is_active(const PairingTaskState &task) {
    if (task.activeAttempt == nullptr) {
      return false;
    }
    return task.activeAttempt->thread != nullptr && !task.activeAttempt->completed.load();
  }

  bool pairing_attempt_is_ready(const PairingAttemptState *attempt) {
    return attempt != nullptr && attempt->thread != nullptr && attempt->completed.load();
  }

  void finalize_pairing_attempt(app::ClientState *state, std::unique_ptr<PairingAttemptState> attempt) {
    if (attempt == nullptr || attempt->thread == nullptr) {
      return;
    }

    int threadResult = 0;
    SDL_WaitThread(attempt->thread, &threadResult);
    (void) threadResult;

    const network::HostPairingRequest request = attempt->request;
    const network::HostPairingResult result = attempt->result;
    const bool discardResult = attempt->discardResult.load();
    const std::vector<PairingAttemptState::DeferredLogEntry> deferredLogs = attempt->deferredLogs;
    reset_pairing_attempt(attempt.get());

    for (const PairingAttemptState::DeferredLogEntry &entry : deferredLogs) {
      logging::logger.log(entry.level, "pairing", entry.message);
    }

    if (discardResult || state == nullptr) {
      logging::logger.info("pairing", "Ignored a completed pairing result after leaving the pairing screen or starting a new attempt");
      return;
    }

    const bool hostsChanged = app::apply_pairing_result(
      *state,
      request.address,
      request.httpPort,
      result.success || result.alreadyPaired,
      result.message
    );

    logging::logger.log(result.success || result.alreadyPaired ? logging::LogLevel::info : logging::LogLevel::warning, "pairing", result.message);
    if (hostsChanged) {
      persist_hosts(*state);
    }
  }

  void retire_active_pairing_attempt(PairingTaskState *task, bool discardResult) {
    if (task == nullptr || task->activeAttempt == nullptr) {
      return;
    }

    if (discardResult) {
      task->activeAttempt->discardResult.store(true);
    }
    task->activeAttempt->cancelRequested.store(true);
    task->retiredAttempts.push_back(std::move(task->activeAttempt));
  }

  void reap_retired_pairing_attempts(PairingTaskState *task) {
    if (task == nullptr) {
      return;
    }

    auto iterator = task->retiredAttempts.begin();
    while (iterator != task->retiredAttempts.end()) {
      if (!pairing_attempt_is_ready(iterator->get())) {
        ++iterator;
        continue;
      }

      finalize_pairing_attempt(nullptr, std::move(*iterator));
      iterator = task->retiredAttempts.erase(iterator);
    }
  }

  void reset_app_list_task(AppListTaskState *task) {
    if (task == nullptr) {
      return;
    }

    task->thread = nullptr;
    task->completed.store(false);
    task->address.clear();
    task->port = 0;
    task->serverInfoAvailable = false;
    task->success = false;
    task->appListContentHash = 0;
    task->message.clear();
    task->serverInfo = {};
    task->apps.clear();
  }

  bool app_list_task_is_active(const AppListTaskState &task) {
    return task.thread != nullptr && !task.completed.load();
  }

  void reset_app_art_task(AppArtTaskState *task) {
    if (task == nullptr) {
      return;
    }

    task->thread = nullptr;
    task->completed.store(false);
    task->address.clear();
    task->port = 0;
    task->apps.clear();
    task->cachedAppIds.clear();
    task->failureCount = 0;
  }

  bool app_art_task_is_active(const AppArtTaskState &task) {
    return task.thread != nullptr && !task.completed.load();
  }

  void reset_host_probe_task(HostProbeTaskState *task) {
    if (task == nullptr) {
      return;
    }

    task->workers.clear();
    task->clientIdentity = {};
    task->clientIdentityAvailable = false;
    ui::reset_host_probe_result_queue(&task->resultQueue);
    task->onlineCount = 0U;
    task->offlineCount = 0U;
    task->metadataChanged = false;
  }

  bool host_probe_task_is_active(const HostProbeTaskState &task) {
    return !task.workers.empty();
  }

  bool mark_host_offline(app::ClientState &state, const std::string &address, uint16_t port) {
    for (app::HostRecord &host : state.hosts) {
      if (!app::host_matches_endpoint(host, address, port)) {
        continue;
      }

      host.reachability = app::HostReachability::offline;
      host.manualAddress = address;
      return true;
    }

    return false;
  }

  int run_pairing_task(void *context) {  // NOSONAR(cpp:S5008) SDL_CreateThread requires void* callback signature
    auto *task = static_cast<PairingAttemptState *>(context);
    if (task == nullptr) {
      return -1;
    }

    const startup::LoadClientIdentityResult loadedIdentity = startup::load_client_identity();
    for (const std::string &warning : loadedIdentity.warnings) {
      task->deferredLogs.push_back({logging::LogLevel::warning, warning});
    }

    network::PairingIdentity identity = loadedIdentity.identity;
    if (!loadedIdentity.fileFound || !network::is_valid_pairing_identity(identity)) {
      if (loadedIdentity.fileFound) {
        task->deferredLogs.push_back({logging::LogLevel::warning, "Stored pairing identity was invalid. Generating a new one."});
      }

      std::string identityError;
      identity = network::create_pairing_identity(&identityError);
      if (!network::is_valid_pairing_identity(identity)) {
        task->result = {
          false,
          false,
          identityError.empty() ? "Failed to generate a valid client pairing identity" : "Failed to generate a valid client pairing identity: " + identityError,
        };
        task->completed.store(true);
        return 0;
      }

      if (const startup::SaveClientIdentityResult saveResult = startup::save_client_identity(identity); !saveResult.success) {
        task->result = {false, false, saveResult.errorMessage};
        task->completed.store(true);
        return 0;
      }

      task->deferredLogs.push_back({logging::LogLevel::info, "Saved pairing identity"});
    }

    task->request.identity = std::move(identity);
    task->result = network::pair_host(task->request, &task->cancelRequested);
    task->completed.store(true, std::memory_order_release);
    return 0;
  }

  void finish_pairing_task_if_ready(app::ClientState &state, PairingTaskState *task) {
    if (task == nullptr) {
      return;
    }

    reap_retired_pairing_attempts(task);
    if (!pairing_attempt_is_ready(task->activeAttempt.get())) {
      return;
    }

    finalize_pairing_attempt(&state, std::move(task->activeAttempt));
  }

  void cancel_pairing_if_requested(app::ClientState &state, const app::AppUpdate &update, PairingTaskState *task) {
    if (task == nullptr || !update.pairingCancelledRequested || task->activeAttempt == nullptr || task->activeAttempt->thread == nullptr) {
      return;
    }

    task->activeAttempt->discardResult.store(true);
    task->activeAttempt->cancelRequested.store(true);
    retire_active_pairing_attempt(task, true);
    state.statusMessage.clear();
    logging::logger.info("pairing", "Cancelled the in-flight pairing attempt after leaving the pairing screen");
  }

  void test_host_connection_if_requested(app::ClientState &state, const app::AppUpdate &update) {
    if (!update.connectionTestRequested) {
      return;
    }

    const std::string address = update.connectionTestAddress;
    const uint16_t port = update.connectionTestPort == 0 ? app::DEFAULT_HOST_PORT : update.connectionTestPort;

    if (address.empty()) {
      app::apply_connection_test_result(state, false, "Connection test failed because the host address is invalid");
      logging::logger.warn("hosts", state.statusMessage);
      return;
    }

    network::PairingIdentity clientIdentity {};
    const network::PairingIdentity *clientIdentityPointer = try_load_saved_pairing_identity(&clientIdentity) ? &clientIdentity : nullptr;

    std::string resultMessage;
    network::HostPairingServerInfo serverInfo {};
    const bool success = test_tcp_host_connection(address, port, clientIdentityPointer, &resultMessage, &serverInfo);
    if (success) {
      apply_server_info_to_host(state, address, port, serverInfo);
    } else {
      for (app::HostRecord &host : state.hosts) {
        if (host.address == address && app::effective_host_port(host.port) == port) {
          host.reachability = app::HostReachability::offline;
          host.manualAddress = address;
          break;
        }
      }
    }
    app::apply_connection_test_result(state, success, resultMessage);
    logging::logger.log(success ? logging::LogLevel::info : logging::LogLevel::warning, "hosts", resultMessage);
    if (state.hostsDirty) {
      persist_hosts(state);
    }
  }

  void browse_host_apps_if_requested(app::ClientState &state, const app::AppUpdate &update) {
    if (!update.appsBrowseRequested) {
      return;
    }

    const app::HostRecord *host = app::selected_host(state);
    if (host == nullptr) {
      return;
    }

    const std::string address = host->address;
    const uint16_t port = host->resolvedHttpPort == 0 ? app::effective_host_port(host->port) : host->resolvedHttpPort;
    network::PairingIdentity clientIdentity {};
    const network::PairingIdentity *clientIdentityPointer = try_load_saved_pairing_identity(&clientIdentity) ? &clientIdentity : nullptr;

    std::string resultMessage;
    network::HostPairingServerInfo serverInfo {};
    if (!test_tcp_host_connection(address, port, clientIdentityPointer, &resultMessage, &serverInfo)) {
      for (app::HostRecord &mutableHost : state.hosts) {
        if (mutableHost.address == address && app::effective_host_port(mutableHost.port) == app::effective_host_port(port)) {
          mutableHost.reachability = app::HostReachability::offline;
          mutableHost.manualAddress = address;
          break;
        }
      }
      state.statusMessage = resultMessage;
      logging::logger.warn("apps", resultMessage);
      return;
    }

    apply_server_info_to_host(state, address, port, serverInfo);
    if (state.hostsDirty) {
      persist_hosts(state);
    }

    host = app::selected_host(state);
    if (host == nullptr || host->pairingState != app::PairingState::paired) {
      state.statusMessage = host != nullptr && !host->appListStatusMessage.empty() ? host->appListStatusMessage : "This host is no longer paired. Pair it again before opening apps.";
      logging::logger.warn("apps", state.statusMessage);
      return;
    }

    if (app::begin_selected_host_app_browse(state, update.appsBrowseShowHidden)) {
      logging::logger.info("apps", "Authorized host browse for " + host->displayName);
      return;
    }

    logging::logger.warn("apps", state.statusMessage.empty() ? "Failed to enter the apps screen" : state.statusMessage);
  }

  int run_host_probe_task(void *context) {  // NOSONAR(cpp:S5008) SDL_CreateThread requires void* callback signature
    auto *worker = static_cast<HostProbeTaskState::ProbeWorkerState *>(context);
    if (worker == nullptr) {
      return -1;
    }

    worker->success = test_tcp_host_connection(worker->address, worker->port, worker->clientIdentity, nullptr, &worker->serverInfo);
    ui::publish_host_probe_result(
      worker->resultQueue,
      {
        worker->address,
        worker->port,
        worker->success,
        worker->serverInfo,
      }
    );
    worker->completed.store(true, std::memory_order_release);
    return 0;
  }

  void apply_published_host_probe_results(app::ClientState &state, HostProbeTaskState *task) {
    if (task == nullptr) {
      return;
    }

    const std::vector<ui::HostProbeResult> results = ui::drain_host_probe_results(&task->resultQueue);
    for (const ui::HostProbeResult &result : results) {
      if (result.success) {
        if (state.activeScreen == app::ScreenId::hosts && state.hostsLoaded) {
          apply_server_info_to_host(state, result.address, result.port, result.serverInfo);
          task->metadataChanged = task->metadataChanged || state.hostsDirty;
        }
        ++task->onlineCount;
        continue;
      }

      if (state.activeScreen == app::ScreenId::hosts && state.hostsLoaded) {
        mark_host_offline(state, result.address, result.port);
      }
      ++task->offlineCount;
    }
  }

  void reap_completed_host_probe_workers(HostProbeTaskState *task) {
    if (task == nullptr) {
      return;
    }

    auto iterator = task->workers.begin();
    while (iterator != task->workers.end()) {
      if ((*iterator)->thread == nullptr || !(*iterator)->completed.load(std::memory_order_acquire)) {
        ++iterator;
        continue;
      }

      int threadResult = 0;
      SDL_WaitThread((*iterator)->thread, &threadResult);
      (void) threadResult;
      iterator = task->workers.erase(iterator);
    }
  }

  void finish_host_probe_task_if_ready(app::ClientState &state, HostProbeTaskState *task) {
    if (task == nullptr) {
      return;
    }

    apply_published_host_probe_results(state, task);
    reap_completed_host_probe_workers(task);
    apply_published_host_probe_results(state, task);
    if (host_probe_task_is_active(*task) || !ui::host_probe_result_round_complete(task->resultQueue)) {
      return;
    }

    logging::logger.debug(
      "hosts",
      "Refreshed " + std::to_string(task->onlineCount + task->offlineCount) + " saved host(s): " + std::to_string(task->onlineCount) + " online, " + std::to_string(task->offlineCount) + " offline"
    );
    if (task->metadataChanged) {
      persist_hosts(state);
    }
    reset_host_probe_task(task);
  }

  void start_host_probe_task_if_needed(const app::ClientState &state, HostProbeTaskState *task, Uint32 now, Uint32 *nextHostProbeTick) {
    if (task == nullptr || host_probe_task_is_active(*task) || state.activeScreen != app::ScreenId::hosts || !state.hostsLoaded || !network::runtime_network_ready()) {
      return;
    }
    if (nextHostProbeTick != nullptr && *nextHostProbeTick != 0U && now < *nextHostProbeTick) {
      return;
    }

    reset_host_probe_task(task);
    task->clientIdentityAvailable = try_load_saved_pairing_identity(&task->clientIdentity);
    ui::begin_host_probe_result_round(&task->resultQueue, state.hosts.size());
    for (const app::HostRecord &host : state.hosts) {
      auto worker = std::make_unique<HostProbeTaskState::ProbeWorkerState>();
      worker->address = host.address;
      worker->port = app::effective_host_port(host.port);
      worker->clientIdentity = task->clientIdentityAvailable ? &task->clientIdentity : nullptr;
      worker->resultQueue = &task->resultQueue;
      worker->thread = SDL_CreateThread(run_host_probe_task, "probe-saved-host", worker.get());
      if (worker->thread == nullptr) {
        logging::logger.error("hosts", "Failed to start the saved-host refresh worker for " + host.address + ": " + SDL_GetError());
        ui::skip_host_probe_result_target(&task->resultQueue);
        continue;
      }

      task->workers.push_back(std::move(worker));
    }
    if (task->workers.empty()) {
      reset_host_probe_task(task);
      return;
    }

    if (nextHostProbeTick != nullptr) {
      *nextHostProbeTick = now + HOST_PROBE_REFRESH_INTERVAL_MILLISECONDS;
    }
  }

  void pair_host_if_requested(app::ClientState &state, const app::AppUpdate &update, PairingTaskState *task) {
    if (!update.pairingRequested || task == nullptr) {
      return;
    }

    finish_pairing_task_if_ready(state, task);

    if (pairing_task_is_active(*task)) {
      retire_active_pairing_attempt(task, true);
      logging::logger.info("pairing", "Discarded the previous background pairing attempt and started a fresh one");
    }

    std::string reachabilityMessage;
    network::HostPairingServerInfo serverInfo {};
    network::PairingIdentity clientIdentity {};
    if (const network::PairingIdentity *clientIdentityPointer = try_load_saved_pairing_identity(&clientIdentity) ? &clientIdentity : nullptr; !test_tcp_host_connection(update.pairingAddress, update.pairingPort, clientIdentityPointer, &reachabilityMessage, &serverInfo)) {
      for (app::HostRecord &host : state.hosts) {
        if (host.address == update.pairingAddress && app::effective_host_port(host.port) == app::effective_host_port(update.pairingPort)) {
          host.reachability = app::HostReachability::offline;
          host.manualAddress = update.pairingAddress;
          break;
        }
      }
      if (state.activeHostLoaded && app::host_matches_endpoint(state.activeHost, update.pairingAddress, update.pairingPort)) {
        state.activeHost.reachability = app::HostReachability::offline;
        state.activeHost.manualAddress = update.pairingAddress;
      }
      state.pairingDraft.stage = app::PairingStage::failed;
      state.pairingDraft.generatedPin.clear();
      state.pairingDraft.statusMessage = reachabilityMessage.empty() ? "The host could not be reached for pairing." : reachabilityMessage;
      state.statusMessage = state.pairingDraft.statusMessage;
      logging::logger.warn("pairing", state.pairingDraft.statusMessage);
      return;
    }

    apply_server_info_to_host(state, update.pairingAddress, update.pairingPort, serverInfo);
    if (state.hostsDirty) {
      persist_hosts(state);
    }

    auto attempt = std::make_unique<PairingAttemptState>();
    reset_pairing_attempt(attempt.get());
    attempt->request = {
      update.pairingAddress,
      update.pairingPort,
      update.pairingPin,
      "MoonlightXboxOG",
      {},
    };

    attempt->thread = SDL_CreateThreadWithStackSize(run_pairing_task, "pair-host", PAIRING_THREAD_STACK_SIZE, attempt.get());
    if (attempt->thread == nullptr) {
      reset_pairing_attempt(attempt.get());
      const std::string createThreadError = std::string("Failed to start the background pairing task: ") + SDL_GetError();
      app::apply_pairing_result(state, update.pairingAddress, update.pairingPort, false, createThreadError);
      state.pairingDraft.generatedPin.clear();
      logging::logger.error("pairing", createThreadError);
      return;
    }

    task->activeAttempt = std::move(attempt);

    state.pairingDraft.stage = app::PairingStage::in_progress;
    state.pairingDraft.statusMessage = "The host is reachable. Enter the code shown below on the host and keep this screen open for the result.";
    state.statusMessage.clear();
    logging::logger.info("pairing", "Started background pairing with " + update.pairingAddress + ":" + std::to_string(update.pairingPort));
  }

  int run_app_list_task(void *context) {  // NOSONAR(cpp:S5008) SDL_CreateThread requires void* callback signature
    auto *task = static_cast<AppListTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    network::PairingIdentity clientIdentity {};
    std::string errorMessage;
    if (!load_saved_pairing_identity_for_streaming(&clientIdentity, &errorMessage)) {
      task->success = false;
      task->message = errorMessage;
      task->completed.store(true);
      return 0;
    }

    std::vector<network::HostAppEntry> fetchedApps;
    errorMessage.clear();
    task->success = network::query_app_list(task->address, task->port, &clientIdentity, &fetchedApps, &task->serverInfo, &errorMessage);
    task->serverInfoAvailable = task->serverInfo.httpPort != 0 || task->serverInfo.httpsPort != 0 || !task->serverInfo.hostName.empty() || !task->serverInfo.uuid.empty();
    if (!task->success) {
      task->message = errorMessage.empty() ? "Failed to fetch the host app list" : errorMessage;
      task->completed.store(true);
      return 0;
    }

    task->appListContentHash = network::hash_app_list_entries(fetchedApps);

    task->apps.clear();
    task->apps.reserve(fetchedApps.size());
    const std::string hostIdentity = !task->serverInfo.uuid.empty() ? task->serverInfo.uuid : task->address;
    for (const network::HostAppEntry &entry : fetchedApps) {
      const std::string cacheKey = startup::build_cover_art_cache_key(hostIdentity, task->address, entry.id);
      task->apps.push_back({
        entry.name,
        entry.id,
        entry.hdrSupported,
        entry.hidden,
        false,
        cacheKey,
        startup::cover_art_exists(cacheKey),
        false,
      });
    }

    task->message = task->apps.empty() ? "Host returned no launchable apps for this host" : "Loaded " + std::to_string(task->apps.size()) + " Host app(s)";
    task->completed.store(true);
    return 0;
  }

  void finish_app_list_task_if_ready(app::ClientState &state, AppListTaskState *task) {
    if (task == nullptr || task->thread == nullptr || !task->completed.load()) {
      return;
    }

    SDL_Thread *thread = task->thread;
    task->thread = nullptr;
    int threadResult = 0;
    SDL_WaitThread(thread, &threadResult);
    (void) threadResult;

    const std::string address = task->address;
    const uint16_t port = task->port;
    const bool success = task->success;
    const uint64_t appListContentHash = task->appListContentHash;
    const std::string message = task->message;
    const bool serverInfoAvailable = task->serverInfoAvailable;
    const network::HostPairingServerInfo serverInfo = task->serverInfo;
    std::vector<app::HostAppRecord> apps = std::move(task->apps);
    reset_app_list_task(task);

    if (serverInfoAvailable) {
      apply_server_info_to_host(state, address, port, serverInfo);
    }

    if (success) {
      app::apply_app_list_result(state, address, port, std::move(apps), appListContentHash, true, message);
      logging::logger.info("apps", "Fetched app list from " + address + ":" + std::to_string(serverInfo.httpPort));
      if (state.hostsDirty) {
        persist_hosts(state);
      }
      return;
    }

    app::apply_app_list_result(state, address, port, {}, 0, false, message);
    logging::logger.warn("apps", message);
  }

  void start_app_list_task_if_needed(app::ClientState &state, AppListTaskState *task, Uint32 now) {
    if (task == nullptr || app_list_task_is_active(*task) || state.activeScreen != app::ScreenId::apps) {
      return;
    }

    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr || host->pairingState != app::PairingState::paired || host->reachability == app::HostReachability::offline) {
      return;
    }

    if (host->appListState != app::HostAppListState::loading) {
      if (host->lastAppListRefreshTick != 0U && now - host->lastAppListRefreshTick < APP_LIST_REFRESH_INTERVAL_MILLISECONDS) {
        return;
      }

      if (state.activeHostLoaded) {
        app::HostRecord &mutableHost = state.activeHost;
        mutableHost.appListState = app::HostAppListState::loading;
        mutableHost.appListStatusMessage = (mutableHost.apps.empty() ? "Loading apps for " : "Refreshing apps for ") + mutableHost.displayName + "...";
        state.statusMessage.clear();
      }
    }

    reset_app_list_task(task);
    task->address = host->address;
    task->port = host->resolvedHttpPort == 0 ? app::effective_host_port(host->port) : host->resolvedHttpPort;
    task->thread = SDL_CreateThread(run_app_list_task, "fetch-app-list", task);
    if (task->thread == nullptr) {
      const std::string errorMessage = std::string("Failed to start the app-list fetch task: ") + SDL_GetError();
      logging::logger.error("apps", errorMessage);
      if (state.activeHostLoaded) {
        state.activeHost.appListState = app::HostAppListState::failed;
        state.activeHost.appListStatusMessage = errorMessage;
        state.statusMessage = errorMessage;
      }
      reset_app_list_task(task);
      return;
    }

    if (state.activeHostLoaded) {
      state.activeHost.lastAppListRefreshTick = now;
    }
  }

  int run_app_art_task(void *context) {  // NOSONAR(cpp:S5008) SDL_CreateThread requires void* callback signature
    auto *task = static_cast<AppArtTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    network::PairingIdentity clientIdentity {};
    if (std::string identityError; !load_saved_pairing_identity_for_streaming(&clientIdentity, &identityError)) {
      task->failureCount = task->apps.size();
      task->completed.store(true);
      return 0;
    }

    for (const app::HostAppRecord &appRecord : task->apps) {
      if (appRecord.boxArtCached || appRecord.boxArtCacheKey.empty()) {
        continue;
      }

      std::vector<unsigned char> assetBytes;
      if (std::string errorMessage; !network::query_app_asset(task->address, task->port, &clientIdentity, appRecord.id, &assetBytes, &errorMessage)) {
        ++task->failureCount;
        continue;
      }

      if (const startup::SaveCoverArtResult saveResult = startup::save_cover_art(appRecord.boxArtCacheKey, assetBytes); !saveResult.success) {
        ++task->failureCount;
        continue;
      }

      task->cachedAppIds.push_back(appRecord.id);
    }

    task->completed.store(true);
    return 0;
  }

  void finish_app_art_task_if_ready(app::ClientState &state, AppArtTaskState *task, CoverArtTextureCache *textureCache) {
    if (task == nullptr || task->thread == nullptr || !task->completed.load()) {
      return;
    }

    SDL_Thread *thread = task->thread;
    task->thread = nullptr;
    int threadResult = 0;
    SDL_WaitThread(thread, &threadResult);
    (void) threadResult;

    const std::string address = task->address;
    const uint16_t port = task->port;
    const std::vector<int> cachedAppIds = task->cachedAppIds;
    const std::size_t failureCount = task->failureCount;
    reset_app_art_task(task);

    for (int appId : cachedAppIds) {
      app::mark_cover_art_cached(state, address, port, appId);
    }

    if (textureCache != nullptr) {
      textureCache->failedKeys.clear();
    }

    if (!cachedAppIds.empty()) {
      logging::logger.info("apps", "Cached cover art for " + std::to_string(cachedAppIds.size()) + " app(s)");
    }
    if (failureCount > 0U) {
      logging::logger.warn("apps", std::to_string(failureCount) + " app artwork request(s) fell back to placeholders");
    }
  }

  void start_app_art_task_if_needed(const app::ClientState &state, AppArtTaskState *task) {
    if (task == nullptr || app_art_task_is_active(*task) || state.activeScreen != app::ScreenId::apps) {
      return;
    }

    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr || host->appListState != app::HostAppListState::ready || host->apps.empty()) {
      return;
    }

    if (const bool missingArt = std::any_of(host->apps.begin(), host->apps.end(), [](const app::HostAppRecord &appRecord) {
          return !appRecord.boxArtCached && !appRecord.boxArtCacheKey.empty();
        });
        !missingArt) {
      return;
    }

    reset_app_art_task(task);
    task->address = host->address;
    task->port = host->httpsPort == 0 ? app::effective_host_port(host->port) : host->httpsPort;
    task->apps = host->apps;
    task->thread = SDL_CreateThread(run_app_art_task, "fetch-app-art", task);
    if (task->thread == nullptr) {
      logging::logger.error("apps", std::string("Failed to start the cover-art fetch task: ") + SDL_GetError());
      reset_app_art_task(task);
    }
  }

  void show_log_file_if_requested(app::ClientState &state, const app::AppUpdate &update) {
    if (!update.logViewRequested) {
      return;
    }

    const std::string filePath = state.logFilePath.empty() ? logging::default_log_file_path() : state.logFilePath;
    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, LOG_VIEWER_MAX_LOADED_LINES);
    app::set_log_file_path(state, loadedLog.filePath);
    if (!loadedLog.errorMessage.empty()) {
      app::apply_log_viewer_contents(state, {loadedLog.errorMessage}, loadedLog.errorMessage);
      logging::logger.warn("logging", loadedLog.errorMessage);
      return;
    }

    std::vector<std::string> lines = loadedLog.lines;
    if (!loadedLog.fileFound) {
      lines = {"The log file does not exist yet."};
    } else if (lines.empty()) {
      lines = {"The log file is empty."};
    }

    const std::string statusMessage = loadedLog.fileFound ? "Loaded recent log file lines" : "No log file has been written yet";
    app::apply_log_viewer_contents(state, std::move(lines), statusMessage);
    logging::logger.info("logging", statusMessage + ": " + loadedLog.filePath);
  }

  bool draw_shell(  // NOSONAR(cpp:S3776,cpp:S107) one-frame shell rendering is intentionally centralized to keep layout and failure handling consistent
    SDL_Renderer *renderer,
    const VIDEO_MODE &videoMode,
    unsigned long encoderSettings,
    SDL_Texture *titleLogoTexture,
    TTF_Font *titleFont,
    TTF_Font *bodyFont,
    TTF_Font *smallFont,
    const ui::ShellViewModel &viewModel,
    CoverArtTextureCache *textureCache,
    AssetTextureCache *assetCache,
    KeypadModalLayoutCache *keypadModalLayoutCache
  ) {
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    if (SDL_GetRendererOutputSize(renderer, &framebufferWidth, &framebufferHeight) != 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
      return false;
    }

    const int screenHeight = framebufferHeight;
    const int screenWidth = splash::calculate_display_width(screenHeight, videoMode, encoderSettings);
    const float horizontalScale = static_cast<float>(framebufferWidth) / static_cast<float>(screenWidth);

    SDL_RenderSetScale(renderer, 1.0f, 1.0f);

    const int outerMargin = std::max(18, screenHeight / 24);
    const int panelGap = std::max(14, screenWidth / 48);
    const int headerHeight = std::max(76, screenHeight / 8);
    const int footerHeight = std::max(46, screenHeight / 11);
    const SDL_Rect headerRect {outerMargin, outerMargin, screenWidth - (outerMargin * 2), headerHeight};
    const SDL_Rect contentRect {outerMargin, outerMargin + headerHeight + 10, screenWidth - (outerMargin * 2), screenHeight - ((outerMargin * 2) + headerHeight + footerHeight + 20)};
    const SDL_Rect footerRect {outerMargin, screenHeight - outerMargin - footerHeight, screenWidth - (outerMargin * 2), footerHeight};

    SDL_SetRenderDrawColor(renderer, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xFF);
    if (SDL_RenderClear(renderer) != 0) {
      return false;
    }

    if (SDL_RenderSetScale(renderer, horizontalScale, 1.0f) != 0) {
      return false;
    }

    fill_rect(renderer, headerRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE);
    fill_rect(renderer, contentRect, PANEL_ALT_RED, PANEL_ALT_GREEN, PANEL_ALT_BLUE);
    fill_rect(renderer, footerRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE);
    draw_line(renderer, headerRect.x, headerRect.y + headerRect.h - 1, headerRect.x + headerRect.w, headerRect.y + headerRect.h - 1, TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0x90);
    draw_line(renderer, footerRect.x, footerRect.y, footerRect.x + footerRect.w, footerRect.y, TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0x90);

    int titleTextX = headerRect.x + 16;
    const int titleTextY = headerRect.y + 12;
    int titleTextWidth = headerRect.w - 32;

    if (titleLogoTexture != nullptr) {
      int logoWidth = 0;
      int logoHeight = 0;
      if (SDL_QueryTexture(titleLogoTexture, nullptr, nullptr, &logoWidth, &logoHeight) == 0 && logoWidth > 0 && logoHeight > 0) {
        const int targetLogoHeight = std::max(32, TTF_FontLineSkip(titleFont));
        const int targetLogoWidth = std::max(32, (logoWidth * targetLogoHeight) / logoHeight);
        const SDL_Rect logoRect {
          headerRect.x + 16,
          headerRect.y + 10,
          targetLogoWidth,
          targetLogoHeight,
        };
        if (SDL_RenderCopy(renderer, titleLogoTexture, nullptr, &logoRect) != 0) {
          return false;
        }

        titleTextX = logoRect.x + logoRect.w + 12;
        titleTextWidth = (headerRect.w / 3) - (titleTextX - headerRect.x);
      }
    }

    if (!render_text_line(renderer, titleFont, viewModel.title, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, titleTextX, titleTextY, titleTextWidth)) {
      return false;
    }

    const int pageTitleX = headerRect.x + (headerRect.w / 3);
    const int pageTitleY = headerRect.y + 18;
    if (const bool renderedPageTitle = viewModel.screen == app::ScreenId::apps ? render_text_line_simple(renderer, bodyFont, viewModel.pageTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, pageTitleX, pageTitleY, headerRect.w / 3) : render_text_line(renderer, bodyFont, viewModel.pageTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, pageTitleX, pageTitleY, headerRect.w / 3); !viewModel.pageTitle.empty() && !renderedPageTitle) {
      return false;
    }

    if (viewModel.screen == app::ScreenId::hosts) {
      const int buttonWidth = std::max(132, headerRect.w / 7);
      const int buttonHeight = std::max(40, headerRect.h / 2);
      int buttonX = headerRect.x + headerRect.w - 16 - ((buttonWidth + 12) * static_cast<int>(viewModel.toolbarButtons.size()));
      for (const ui::ShellToolbarButton &button : viewModel.toolbarButtons) {
        if (const SDL_Rect buttonRect {buttonX, headerRect.y + 18, buttonWidth, buttonHeight}; !render_toolbar_button(renderer, bodyFont, smallFont, assetCache, button, buttonRect)) {
          return false;
        }
        buttonX += buttonWidth + 12;
      }
    }

    int infoY = contentRect.y + 16;
    if (viewModel.screen == app::ScreenId::hosts) {
      for (const std::string &line : viewModel.bodyLines) {
        int drawnHeight = 0;
        if (!render_text_line(renderer, smallFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, contentRect.x + 16, infoY, contentRect.w - 32, &drawnHeight)) {
          return false;
        }
        infoY += drawnHeight + 6;
      }
    }

    if (viewModel.screen == app::ScreenId::hosts) {
      const int gridTop = infoY + 8;
      const int gridHeight = std::max(1, (contentRect.y + contentRect.h - gridTop) - 12);
      const int columnCount = std::max(1, static_cast<int>(viewModel.hostColumnCount));
      const int tileGap = 16;
      const SDL_Rect gridRect {contentRect.x + 16, gridTop, contentRect.w - 32, gridHeight};
      const GridViewport viewport = calculate_grid_viewport(viewModel.hostTiles.size(), viewModel.hostColumnCount, selected_host_tile_index(viewModel.hostTiles), gridRect.h, 188, tileGap);
      const int scrollbarGap = viewport.scrollbarWidth > 0 ? 12 : 0;
      const int gridInnerWidth = std::max(1, gridRect.w - viewport.scrollbarWidth - scrollbarGap);
      const int tileWidth = std::max(1, (gridInnerWidth - (tileGap * (columnCount - 1))) / columnCount);
      const int tileHeight = std::max(1, (gridRect.h - (tileGap * std::max(0, viewport.visibleRowCount - 1))) / std::max(1, viewport.visibleRowCount));
      const std::size_t startIndex = static_cast<std::size_t>(viewport.startRow) * viewModel.hostColumnCount;
      const std::size_t endIndex = std::min(viewModel.hostTiles.size(), static_cast<std::size_t>(viewport.startRow + viewport.visibleRowCount) * viewModel.hostColumnCount);
      for (std::size_t index = startIndex; index < endIndex; ++index) {
        const int row = static_cast<int>(index / viewModel.hostColumnCount) - viewport.startRow;
        const auto column = static_cast<int>(index % viewModel.hostColumnCount);
        const SDL_Rect tileRect {
          gridRect.x + (column * (tileWidth + tileGap)),
          gridRect.y + (row * (tileHeight + tileGap)),
          tileWidth,
          tileHeight,
        };
        const ui::ShellHostTile &tile = viewModel.hostTiles[index];
        const bool online = tile.reachability == app::HostReachability::online;
        fill_rect(renderer, tileRect, online ? PANEL_RED + 8 : PANEL_RED, online ? PANEL_GREEN + 8 : PANEL_GREEN, online ? PANEL_BLUE + 8 : PANEL_BLUE);
        draw_rect(renderer, tileRect, tile.selected ? ACCENT_RED : MUTED_RED, tile.selected ? ACCENT_GREEN : MUTED_GREEN, tile.selected ? ACCENT_BLUE : MUTED_BLUE);
        const int statusHeight = std::max(20, tileRect.h / 8);
        const int nameHeight = std::max(24, tileRect.h / 7);
        const int textBlockHeight = statusHeight + nameHeight + 18;
        const SDL_Rect hostIconRect {
          tileRect.x + 12,
          tileRect.y + 8,
          tileRect.w - 24,
          std::max(18, tileRect.h - textBlockHeight - 16),
        };
        if (!tile.iconAssetPath.empty()) {
          render_asset_icon(renderer, assetCache, tile.iconAssetPath, hostIconRect);
        }
        if (const SDL_Rect nameRect {
              tileRect.x + 8,
              tileRect.y + tileRect.h - statusHeight - nameHeight - 10,
              tileRect.w - 16,
              nameHeight,
            };
            !render_text_centered_simple(renderer, bodyFont, tile.displayName, online ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF} : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, nameRect)) {
          return false;
        }
        const SDL_Rect statusRect {
          tileRect.x + 8,
          tileRect.y + tileRect.h - statusHeight - 8,
          tileRect.w - 16,
          statusHeight,
        };
        if (!render_text_centered_simple(renderer, smallFont, tile.statusLabel, online ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF} : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, statusRect)) {
          return false;
        }
      }

      if (viewport.scrollbarWidth > 0) {
        render_grid_scrollbar(renderer, {gridRect.x + gridRect.w - viewport.scrollbarWidth, gridRect.y, viewport.scrollbarWidth, gridRect.h}, viewport);
      }
    } else if (viewModel.screen == app::ScreenId::apps) {
      const SDL_Rect gridRect {
        contentRect.x + 16,
        contentRect.y + 16,
        contentRect.w - 32,
        contentRect.h - 28,
      };

      if (!viewModel.appTiles.empty()) {
        const int columnCount = std::max(1, static_cast<int>(viewModel.appColumnCount));
        const int tileGap = 16;
        const int gridPadding = 10;
        const GridViewport viewport = calculate_grid_viewport(viewModel.appTiles.size(), viewModel.appColumnCount, selected_app_tile_index(viewModel.appTiles), std::max(1, gridRect.h - (gridPadding * 2)), 220, tileGap);
        const int scrollbarGap = viewport.scrollbarWidth > 0 ? 12 : 0;
        const int gridInnerWidth = std::max(1, gridRect.w - (gridPadding * 2) - viewport.scrollbarWidth - scrollbarGap);
        const int cellWidth = std::max(1, (gridInnerWidth - (tileGap * (columnCount - 1))) / columnCount);
        const int cellHeight = std::max(1, (gridRect.h - (gridPadding * 2) - (tileGap * std::max(0, viewport.visibleRowCount - 1))) / std::max(1, viewport.visibleRowCount));
        const int tileWidth = std::max(1, std::min(cellWidth, (cellHeight * 2) / 3));
        const int tileHeight = std::max(1, std::min(cellHeight, (tileWidth * 3) / 2));
        const std::size_t startIndex = static_cast<std::size_t>(viewport.startRow) * viewModel.appColumnCount;
        const std::size_t endIndex = std::min(viewModel.appTiles.size(), static_cast<std::size_t>(viewport.startRow + viewport.visibleRowCount) * viewModel.appColumnCount);
        for (std::size_t index = startIndex; index < endIndex; ++index) {
          const int row = static_cast<int>(index / viewModel.appColumnCount) - viewport.startRow;
          const auto column = static_cast<int>(index % viewModel.appColumnCount);
          const SDL_Rect tileRect {
            gridRect.x + gridPadding + (column * (cellWidth + tileGap)) + std::max(0, (cellWidth - tileWidth) / 2),
            gridRect.y + gridPadding + (row * (cellHeight + tileGap)) + std::max(0, (cellHeight - tileHeight) / 2),
            tileWidth,
            tileHeight,
          };
          const ui::ShellAppTile &tile = viewModel.appTiles[index];
          if (!render_app_cover(renderer, smallFont, tile, tileRect, textureCache, assetCache)) {  // NOSONAR(cpp:S134) app-grid rendering keeps per-tile failure handling inline with layout
            return false;
          }
        }

        if (viewport.scrollbarWidth > 0) {
          render_grid_scrollbar(
            renderer,
            {gridRect.x + gridRect.w - viewport.scrollbarWidth, gridRect.y + gridPadding, viewport.scrollbarWidth, std::max(1, gridRect.h - (gridPadding * 2))},
            viewport
          );
        }
      } else if (!viewModel.bodyLines.empty()) {
        const int lineGap = 8;
        int textHeight = 0;
        for (std::size_t index = 0; index < viewModel.bodyLines.size(); ++index) {
          textHeight += measure_wrapped_text_height(smallFont, viewModel.bodyLines[index], gridRect.w - 48);
          if (index + 1U < viewModel.bodyLines.size()) {  // NOSONAR(cpp:S134) empty-state text height is accumulated inline with layout calculation
            textHeight += lineGap;
          }
        }

        int messageY = gridRect.y + std::max(16, (gridRect.h - textHeight) / 2);
        for (const std::string &line : viewModel.bodyLines) {
          int drawnHeight = 0;
          if (!render_text_line(renderer, smallFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, gridRect.x + 24, messageY, gridRect.w - 48, &drawnHeight)) {
            return false;
          }
          messageY += drawnHeight + lineGap;
        }
      }
    } else {
      const bool settingsScreen = viewModel.screen == app::ScreenId::settings;
      const bool hasDetailMenu = settingsScreen && !viewModel.detailMenuRows.empty();
      const int panelInset = std::max(12, screenWidth / 96);
      const int panelPadding = std::max(14, screenWidth / 96);
      const SDL_Rect panelArea {
        contentRect.x + panelInset,
        contentRect.y + panelInset,
        std::max(1, contentRect.w - (panelInset * 2)),
        std::max(1, contentRect.h - (panelInset * 2)),
      };
      const int menuPanelWidth = std::max(232, (panelArea.w * 31) / 100);
      const SDL_Rect menuPanel {panelArea.x, panelArea.y, menuPanelWidth, panelArea.h};
      const SDL_Rect bodyPanel {panelArea.x + menuPanelWidth + panelGap, panelArea.y, panelArea.w - menuPanelWidth - panelGap, panelArea.h};
      fill_rect(renderer, menuPanel, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xC8);
      fill_rect(renderer, bodyPanel, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0x88);
      draw_rect(
        renderer,
        menuPanel,
        viewModel.leftPanelActive ? ACCENT_RED : TEXT_RED,
        viewModel.leftPanelActive ? ACCENT_GREEN : TEXT_GREEN,
        viewModel.leftPanelActive ? ACCENT_BLUE : TEXT_BLUE,
        viewModel.leftPanelActive ? 0xD8 : 0x48
      );
      draw_rect(
        renderer,
        bodyPanel,
        viewModel.rightPanelActive ? ACCENT_RED : TEXT_RED,
        viewModel.rightPanelActive ? ACCENT_GREEN : TEXT_GREEN,
        viewModel.rightPanelActive ? ACCENT_BLUE : TEXT_BLUE,
        viewModel.rightPanelActive ? 0xD8 : 0x48
      );

      const SDL_Rect menuHeaderRect {menuPanel.x + panelPadding, menuPanel.y + panelPadding, menuPanel.w - (panelPadding * 2), std::max(34, TTF_FontLineSkip(smallFont) + 10)};
      fill_rect(renderer, menuHeaderRect, PANEL_ALT_RED, PANEL_ALT_GREEN, PANEL_ALT_BLUE, 0xD8);
      if (!render_text_line_simple(renderer, smallFont, settingsScreen ? "Categories" : "Actions", {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, menuHeaderRect.x + 10, menuHeaderRect.y + std::max(6, (menuHeaderRect.h - TTF_FontLineSkip(smallFont)) / 2), menuHeaderRect.w - 20)) {
        return false;
      }

      if (!render_action_rows(
            renderer,
            bodyFont,
            viewModel.menuRows,
            {menuPanel.x + panelPadding, menuHeaderRect.y + menuHeaderRect.h + 12, menuPanel.w - (panelPadding * 2), menuPanel.h - (menuHeaderRect.h + (panelPadding * 2) + 12)},
            std::max(36, screenHeight / 13)
          )) {
        return false;
      }

      if (hasDetailMenu) {
        const int optionsHeaderY = bodyPanel.y + panelPadding;
        const int optionsTopY = optionsHeaderY + 28;
        const int descriptionGap = 16;
        const int descriptionHeaderHeight = std::max(26, TTF_FontLineSkip(smallFont));
        const int minimumDescriptionHeight = std::max(96, (TTF_FontLineSkip(smallFont) * 3) + descriptionHeaderHeight + 20);
        const int availableOptionsHeight = bodyPanel.h - ((panelPadding * 2) + 28 + descriptionGap + descriptionHeaderHeight + minimumDescriptionHeight);
        const int optionsHeight = std::max(std::max(120, bodyPanel.h / 2), availableOptionsHeight);
        const SDL_Rect optionsRect {bodyPanel.x + panelPadding, optionsTopY, bodyPanel.w - (panelPadding * 2), std::max(96, optionsHeight)};
        const int descriptionTopY = optionsRect.y + optionsRect.h + descriptionGap;
        const SDL_Rect descriptionRect {
          bodyPanel.x + panelPadding,
          descriptionTopY,
          bodyPanel.w - (panelPadding * 2),
          std::max(72, bodyPanel.y + bodyPanel.h - panelPadding - descriptionTopY)
        };

        if (!render_text_line_simple(renderer, smallFont, "Options", {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, bodyPanel.x + panelPadding, optionsHeaderY, bodyPanel.w - (panelPadding * 2))) {
          return false;
        }
        if (!render_action_rows(
              renderer,
              bodyFont,
              viewModel.detailMenuRows,
              optionsRect,
              std::max(34, TTF_FontLineSkip(bodyFont) + 12)
            )) {
          return false;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        fill_rect(renderer, descriptionRect, PANEL_ALT_RED, PANEL_ALT_GREEN, PANEL_ALT_BLUE, 0x88);
        draw_rect(renderer, descriptionRect, TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0x40);

        if (!render_text_line_simple(renderer, smallFont, "Description", {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, descriptionRect.x + 10, descriptionRect.y + 8, descriptionRect.w - 20)) {
          return false;
        }

        int descriptionY = descriptionRect.y + descriptionHeaderHeight + 10;
        if (!viewModel.selectedMenuRowLabel.empty()) {
          int drawnHeight = 0;
          if (!render_text_line(renderer, bodyFont, viewModel.selectedMenuRowLabel, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, descriptionRect.x + 10, descriptionY, descriptionRect.w - 20, &drawnHeight)) {
            return false;
          }
          descriptionY += drawnHeight + 6;
        }

        const std::string descriptionText = viewModel.selectedMenuRowDescription.empty() ? std::string("No description is available for the selected setting.") : viewModel.selectedMenuRowDescription;
        if (!render_text_line(renderer, smallFont, descriptionText, {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, descriptionRect.x + 10, descriptionY, descriptionRect.w - 20)) {
          return false;
        }
      } else {
        int bodyY = bodyPanel.y + panelPadding;
        for (const std::string &line : viewModel.bodyLines) {
          int drawnHeight = 0;
          if (!render_text_line(renderer, bodyFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, bodyPanel.x + panelPadding, bodyY, bodyPanel.w - (panelPadding * 2), &drawnHeight)) {  // NOSONAR(cpp:S134) settings-body rendering keeps layout failure handling local
            return false;
          }
          bodyY += drawnHeight + 8;
        }
      }
    }

    if (!render_footer_actions(renderer, smallFont, assetCache, viewModel.footerActions, footerRect)) {
      return false;
    }

    if (viewModel.notificationVisible && !viewModel.notification.message.empty() && !render_notification(renderer, bodyFont, smallFont, assetCache, viewModel.notification, screenWidth, footerRect.y, outerMargin)) {
      return false;
    }

    if (viewModel.overlayVisible) {
      const int overlayX = (screenWidth / 2) + (panelGap / 2);
      const SDL_Rect overlayRect {
        overlayX,
        outerMargin + 28,
        screenWidth - overlayX - outerMargin - 8,
        screenHeight - ((outerMargin + 28) * 2)
      };
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fill_rect(renderer, overlayRect, 0x00, 0x00, 0x00, 0xD8);
      draw_rect(renderer, overlayRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF);

      if (!render_text_line(renderer, bodyFont, viewModel.overlayTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, overlayRect.x + 16, overlayRect.y + 16, overlayRect.w - 32)) {
        return false;
      }

      int overlayY = overlayRect.y + 54;
      for (const std::string &line : viewModel.overlayLines) {
        int drawnHeight = 0;
        if (!render_text_line(renderer, smallFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, overlayRect.x + 16, overlayY, overlayRect.w - 32, &drawnHeight)) {
          return false;
        }
        overlayY += drawnHeight + 6;
      }
    }

    if (viewModel.modalVisible && viewModel.logViewerVisible) {
      if (!render_log_viewer_modal(renderer, bodyFont, smallFont, assetCache, viewModel, screenWidth, screenHeight, outerMargin)) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        fill_rect(renderer, {0, 0, screenWidth, screenHeight}, 0x00, 0x00, 0x00, 0xA6);
        const SDL_Rect modalRect {
          screenWidth / 6,
          screenHeight / 6,
          (screenWidth * 2) / 3,
          (screenHeight * 2) / 3,
        };
        fill_rect(renderer, modalRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE, 0xF2);
        draw_rect(renderer, modalRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
        render_text_line_simple(renderer, bodyFont, "Log File", {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, modalRect.x + 16, modalRect.y + 16, modalRect.w - 32);
        render_text_line_simple(renderer, smallFont, "The full log viewer could not be rendered safely.", {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, modalRect.x + 16, modalRect.y + 54, modalRect.w - 32);
        render_text_line_simple(renderer, smallFont, "Press B to close.", {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, modalRect.x + 16, modalRect.y + 54 + TTF_FontLineSkip(smallFont) + 8, modalRect.w - 32);
      }
    } else if (viewModel.modalVisible) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      fill_rect(renderer, {0, 0, screenWidth, screenHeight}, 0x00, 0x00, 0x00, 0xA6);
      const SDL_Rect modalRect {
        screenWidth / 6,
        screenHeight / 6,
        (screenWidth * 2) / 3,
        (screenHeight * 2) / 3,
      };
      fill_rect(renderer, modalRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE, 0xF2);
      draw_rect(renderer, modalRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
      if (!render_text_line(renderer, bodyFont, viewModel.modalTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, modalRect.x + 16, modalRect.y + 16, modalRect.w - 32)) {
        return false;
      }

      int modalY = modalRect.y + 54;
      for (const std::string &line : viewModel.modalLines) {
        int drawnHeight = 0;
        if (!render_text_line(renderer, smallFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, modalRect.x + 16, modalY, modalRect.w - 32, &drawnHeight)) {
          return false;
        }
        modalY += drawnHeight + 6;
      }

      if (!viewModel.modalActions.empty()) {
        if (!render_action_rows(renderer, bodyFont, viewModel.modalActions, {modalRect.x + 16, modalY + 8, modalRect.w - 32, modalRect.h - (modalY - modalRect.y) - 24}, std::max(34, TTF_FontLineSkip(bodyFont) + 12))) {
          return false;
        }
      } else if (!viewModel.modalFooterActions.empty()) {
        const SDL_Rect modalFooterRect {modalRect.x + 16, modalRect.y + modalRect.h - 56, modalRect.w - 32, 40};
        if (!render_footer_actions(renderer, smallFont, assetCache, viewModel.modalFooterActions, modalFooterRect)) {
          return false;
        }
      }
    }

    if (viewModel.keypadModalVisible) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      const SDL_Rect scrimRect {0, 0, screenWidth, screenHeight};
      fill_rect(renderer, scrimRect, 0x00, 0x00, 0x00, 0x9C);

      const int modalWidth = std::min(screenWidth - (outerMargin * 2), std::max(360, screenWidth / 2));
      const int buttonGap = 10;
      const int buttonColumnCount = std::max(1, static_cast<int>(viewModel.keypadModalColumnCount));
      const int buttonRowCount = std::max(1, static_cast<int>((viewModel.keypadModalButtons.size() + viewModel.keypadModalColumnCount - 1) / viewModel.keypadModalColumnCount));
      const int preferredButtonHeight = std::max(40, TTF_FontLineSkip(bodyFont) + 16);
      const int modalInnerWidth = modalWidth - 32;
      const int modalTextHeight = keypad_modal_text_height(renderer, smallFont, viewModel, modalInnerWidth, keypadModalLayoutCache);
      const int desiredButtonAreaHeight = (buttonRowCount * preferredButtonHeight) + (buttonGap * std::max(0, buttonRowCount - 1));
      const int desiredModalHeight = 52 + modalTextHeight + 16 + desiredButtonAreaHeight + 28;
      const int modalHeight = std::min(screenHeight - (outerMargin * 2), std::max(300, desiredModalHeight));
      const SDL_Rect modalRect {
        (screenWidth - modalWidth) / 2,
        (screenHeight - modalHeight) / 2,
        modalWidth,
        modalHeight,
      };

      fill_rect(renderer, modalRect, PANEL_RED, PANEL_GREEN, PANEL_BLUE, 0xF0);
      draw_rect(renderer, modalRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);

      if (
        !ensure_wrapped_text_texture(renderer, bodyFont, viewModel.keypadModalTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, modalRect.w - 32, &keypadModalLayoutCache->titleTexture) ||
        !render_cached_text_texture(renderer, keypadModalLayoutCache->titleTexture, modalRect.x + 16, modalRect.y + 16)
      ) {
        return false;
      }

      int modalY = modalRect.y + 52;
      keypadModalLayoutCache->lineTextures.resize(viewModel.keypadModalLines.size());
      for (std::size_t index = 0; index < viewModel.keypadModalLines.size(); ++index) {
        int drawnHeight = 0;
        if (!render_cached_text_texture(renderer, keypadModalLayoutCache->lineTextures[index], modalRect.x + 16, modalY, &drawnHeight)) {
          return false;
        }
        modalY += drawnHeight + 6;
      }

      const int buttonAreaTop = modalY + 16;
      const int buttonAreaHeight = (modalRect.y + modalRect.h) - buttonAreaTop - 24;
      const int buttonWidth = (modalRect.w - 32 - (buttonGap * (buttonColumnCount - 1))) / buttonColumnCount;
      const int buttonHeight = std::max(34, (buttonAreaHeight - (buttonGap * std::max(0, buttonRowCount - 1))) / buttonRowCount);

      if (keypadModalLayoutCache->buttonLabelTextures.size() > viewModel.keypadModalButtons.size()) {
        for (std::size_t index = viewModel.keypadModalButtons.size(); index < keypadModalLayoutCache->buttonLabelTextures.size(); ++index) {
          clear_cached_text_texture(&keypadModalLayoutCache->buttonLabelTextures[index]);
        }
      }
      keypadModalLayoutCache->buttonLabelTextures.resize(viewModel.keypadModalButtons.size());

      for (std::size_t index = 0; index < viewModel.keypadModalButtons.size(); ++index) {
        const auto row = static_cast<int>(index / viewModel.keypadModalColumnCount);
        const auto column = static_cast<int>(index % viewModel.keypadModalColumnCount);
        const SDL_Rect buttonRect {
          modalRect.x + 16 + (column * (buttonWidth + buttonGap)),
          buttonAreaTop + (row * (buttonHeight + buttonGap)),
          buttonWidth,
          buttonHeight,
        };
        const ui::ShellModalButton &button = viewModel.keypadModalButtons[index];

        if (button.selected) {
          fill_rect(renderer, buttonRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0x55);
        } else {
          fill_rect(renderer, buttonRect, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xE0);
        }
        draw_rect(renderer, buttonRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);

        const SDL_Color buttonColor = button.enabled ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF} : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF};
        if (
          !ensure_single_line_text_texture(renderer, bodyFont, button.label, buttonColor, buttonRect.w, &keypadModalLayoutCache->buttonLabelTextures[index]) ||
          !render_cached_centered_text_texture(renderer, keypadModalLayoutCache->buttonLabelTextures[index], buttonRect)
        ) {
          return false;
        }
      }
    }

    SDL_RenderPresent(renderer);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    return true;
  }

  void close_controller(SDL_GameController *controller) {
    if (controller != nullptr) {
      SDL_GameControllerClose(controller);
    }
  }

}  // namespace

namespace ui {

  int run_shell(  // NOSONAR(cpp:S3776) shell loop owns all frame/update orchestration in one place by design
    SDL_Window *window,
    const VIDEO_MODE &videoMode,
    app::ClientState &state
  ) {
    logging::Logger *logger = logging::global_logger();
    if (window == nullptr) {
      return report_shell_failure("sdl", "Shell requires a valid SDL window");
    }

    if (TTF_Init() != 0) {
      return report_shell_failure("ttf", std::string("TTF_Init failed: ") + TTF_GetError());
    }

    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == nullptr) {
      IMG_Quit();
      TTF_Quit();
      return report_shell_failure("sdl", std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xFF);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    const std::string fontPath = build_asset_path("fonts\\vegur-regular.ttf");
    TTF_Font *titleFont = TTF_OpenFont(fontPath.c_str(), std::max(24, videoMode.height / 16));
    TTF_Font *bodyFont = TTF_OpenFont(fontPath.c_str(), std::max(18, videoMode.height / 24));
    TTF_Font *smallFont = TTF_OpenFont(fontPath.c_str(), std::max(14, videoMode.height / 34));
    if (titleFont == nullptr || bodyFont == nullptr || smallFont == nullptr) {
      if (titleFont != nullptr) {
        TTF_CloseFont(titleFont);
      }
      if (bodyFont != nullptr) {
        TTF_CloseFont(bodyFont);
      }
      if (smallFont != nullptr) {
        TTF_CloseFont(smallFont);
      }
      SDL_DestroyRenderer(renderer);
      IMG_Quit();
      TTF_Quit();
      return report_shell_failure("ttf", std::string("Failed to load shell font from ") + fontPath + ": " + TTF_GetError());
    }

    SDL_Texture *titleLogoTexture = load_texture_from_asset(renderer, "moonlight-logo.svg");

    SDL_GameController *controller = nullptr;
    for (int joystickIndex = 0; joystickIndex < SDL_NumJoysticks(); ++joystickIndex) {
      if (SDL_IsGameController(joystickIndex)) {
        controller = SDL_GameControllerOpen(joystickIndex);
        if (controller != nullptr) {
          logging::logger.info("input", "Opened primary controller");
          break;
        }
      }
    }

    bool running = true;
    bool leftTriggerPressed = false;
    bool rightTriggerPressed = false;
    bool leftShoulderPressed = false;
    bool rightShoulderPressed = false;
    bool controllerStartPressed = false;
    bool controllerBackPressed = false;
    bool controllerExitComboArmed = false;
    bool controllerExitComboTriggered = false;
    Uint32 controllerStartDownTick = 0;
    Uint32 controllerBackDownTick = 0;
    Uint32 nextHostProbeTick = 0;
    Uint32 leftShoulderRepeatTick = 0;
    Uint32 rightShoulderRepeatTick = 0;
    Uint32 leftTriggerRepeatTick = 0;
    Uint32 rightTriggerRepeatTick = 0;
    ControllerNavigationHoldState moveUpHoldState {};
    ControllerNavigationHoldState moveDownHoldState {};
    ControllerNavigationHoldState moveLeftHoldState {};
    ControllerNavigationHoldState moveRightHoldState {};
    bool controllerNavigationNeutralRequired = false;
    PairingTaskState pairingTask {};
    AppListTaskState appListTask {};
    AppArtTaskState appArtTask {};
    HostProbeTaskState hostProbeTask {};
    CoverArtTextureCache coverArtTextureCache {};
    AssetTextureCache assetTextureCache {};
    KeypadModalLayoutCache keypadModalLayoutCache {};
    const unsigned long encoderSettings = XVideoGetEncoderSettings();
    reset_pairing_task(&pairingTask);
    reset_app_list_task(&appListTask);
    reset_app_art_task(&appArtTask);
    reset_host_probe_task(&hostProbeTask);
    if (logger != nullptr) {
      logger->set_minimum_level(logging::LogLevel::trace);
      logger->set_file_minimum_level(state.loggingLevel);
      logger->set_debugger_console_minimum_level(state.xemuConsoleLoggingLevel);
    }
    logging::logger.info("app", "Entered interactive shell");
    bool keypadRedrawRequested = true;

    const auto draw_current_shell = [&]() {
      const std::vector<logging::LogEntry> retainedEntries = logger == nullptr ? std::vector<logging::LogEntry> {} : logger->snapshot(logging::LogLevel::info);
      if (const auto viewModel = build_shell_view_model(state, retainedEntries); draw_shell(renderer, videoMode, encoderSettings, titleLogoTexture, titleFont, bodyFont, smallFont, viewModel, &coverArtTextureCache, &assetTextureCache, &keypadModalLayoutCache)) {
        keypadRedrawRequested = false;
        return true;
      }

      report_shell_failure("render", std::string("Shell render failed: ") + SDL_GetError());
      running = false;
      state.shouldExit = true;
      return false;
    };

    const auto process_command = [&](input::UiCommand command) {  // NOSONAR(cpp:S1188) inline command pipeline keeps one-frame side effects adjacent to the shell loop
      if (command == input::UiCommand::none) {
        return;
      }

      keypadRedrawRequested = true;

      const app::ScreenId previousScreen = state.activeScreen;
      const app::AppUpdate update = app::handle_command(state, command);
      if (logger != nullptr) {
        logger->set_file_minimum_level(state.loggingLevel);
        logger->set_debugger_console_minimum_level(state.xemuConsoleLoggingLevel);
      }
      log_app_update(state, update);
      show_log_file_if_requested(state, update);
      cancel_pairing_if_requested(state, update, &pairingTask);
      test_host_connection_if_requested(state, update);
      browse_host_apps_if_requested(state, update);
      pair_host_if_requested(state, update, &pairingTask);
      delete_host_data_if_requested(state, update, &coverArtTextureCache);
      delete_saved_file_if_requested(state, update, &coverArtTextureCache);
      factory_reset_if_requested(state, update, &coverArtTextureCache);
      refresh_saved_files_if_needed(state);
      persist_settings_if_needed(state, update);
      persist_hosts_if_needed(state, update);

      if (previousScreen != state.activeScreen) {
        release_page_resources_for_screen(previousScreen, state.activeScreen, &coverArtTextureCache, &keypadModalLayoutCache);
        ensure_hosts_loaded_for_active_screen(state);
      }
      if ((previousScreen != state.activeScreen || update.screenChanged) && !draw_current_shell()) {
        return;
      }
      if (state.activeScreen != app::ScreenId::add_host || !state.addHostDraft.keypad.visible) {
        clear_keypad_modal_layout_cache(&keypadModalLayoutCache);
      }
    };

    while (running && !state.shouldExit) {
      ensure_hosts_loaded_for_active_screen(state);
      finish_pairing_task_if_ready(state, &pairingTask);
      finish_app_list_task_if_ready(state, &appListTask);
      finish_app_art_task_if_ready(state, &appArtTask, &coverArtTextureCache);
      finish_host_probe_task_if_ready(state, &hostProbeTask);
      refresh_saved_files_if_needed(state);
      start_host_probe_task_if_needed(state, &hostProbeTask, SDL_GetTicks(), &nextHostProbeTick);
      start_app_list_task_if_needed(state, &appListTask, SDL_GetTicks());
      start_app_art_task_if_needed(state, &appArtTask);

      if (
        !controllerExitComboTriggered && controllerStartPressed && controllerBackPressed && (state.activeScreen == app::ScreenId::home || state.activeScreen == app::ScreenId::hosts)
      ) {
        controllerExitComboArmed = true;
        const Uint32 comboStartTick = controllerStartDownTick > controllerBackDownTick ? controllerStartDownTick : controllerBackDownTick;
        if (SDL_GetTicks() - comboStartTick >= EXIT_COMBO_HOLD_MILLISECONDS) {
          controllerExitComboTriggered = true;
          state.shouldExit = true;
          logging::logger.info("app", "Exit requested from held Start+Back on the hosts screen");
        }
      }

      if (state.modal.id == app::ModalId::log_viewer) {
        const Uint32 now = SDL_GetTicks();
        if (leftShoulderPressed && now - leftShoulderRepeatTick >= LOG_VIEWER_SCROLL_REPEAT_MILLISECONDS) {
          leftShoulderRepeatTick = now;
          process_command(input::UiCommand::previous_page);
        }
        if (rightShoulderPressed && now - rightShoulderRepeatTick >= LOG_VIEWER_SCROLL_REPEAT_MILLISECONDS) {
          rightShoulderRepeatTick = now;
          process_command(input::UiCommand::next_page);
        }
        if (leftTriggerPressed && now - leftTriggerRepeatTick >= LOG_VIEWER_FAST_SCROLL_REPEAT_MILLISECONDS) {
          leftTriggerRepeatTick = now;
          process_command(input::UiCommand::fast_previous_page);
        }
        if (rightTriggerPressed && now - rightTriggerRepeatTick >= LOG_VIEWER_FAST_SCROLL_REPEAT_MILLISECONDS) {
          rightTriggerRepeatTick = now;
          process_command(input::UiCommand::fast_next_page);
        }
      }

      SDL_Event event;
      bool skipPolledControllerNavigation = false;
      if (SDL_WaitEventTimeout(&event, SHELL_EVENT_WAIT_TIMEOUT_MILLISECONDS) != 0) {
        do {
          input::UiCommand command = input::UiCommand::none;

          switch (event.type) {
            case SDL_QUIT:
              state.shouldExit = true;
              break;
            case SDL_CONTROLLERDEVICEADDED:
              if (controller == nullptr && SDL_IsGameController(event.cdevice.which)) {  // NOSONAR(cpp:S134) controller lifecycle handling stays inline with SDL event routing
                controller = SDL_GameControllerOpen(event.cdevice.which);
                if (controller != nullptr) {
                  logging::logger.info("input", "Controller connected");
                }
              }
              break;
            case SDL_CONTROLLERDEVICEREMOVED:
              if (controller != nullptr && controller == SDL_GameControllerFromInstanceID(event.cdevice.which)) {  // NOSONAR(cpp:S134) controller lifecycle handling stays inline with SDL event routing
                close_controller(controller);
                controller = nullptr;
                leftTriggerPressed = false;
                rightTriggerPressed = false;
                leftShoulderPressed = false;
                rightShoulderPressed = false;
                controllerStartPressed = false;
                controllerBackPressed = false;
                controllerExitComboArmed = false;
                controllerExitComboTriggered = false;
                controllerNavigationNeutralRequired = false;
                reset_controller_navigation_hold_states(&moveUpHoldState, &moveDownHoldState, &moveLeftHoldState, &moveRightHoldState);
                logging::logger.warn("input", "Controller disconnected");
              }
              break;
            case SDL_CONTROLLERBUTTONDOWN:
              {
                const Uint32 controllerButtonDownTick = SDL_GetTicks();
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                  if (!controllerStartPressed) {
                    controllerStartPressed = true;
                    controllerStartDownTick = controllerButtonDownTick;
                  }
                } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                  if (!controllerBackPressed) {
                    controllerBackPressed = true;
                    controllerBackDownTick = controllerButtonDownTick;
                  }
                } else {
                  if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                    leftShoulderPressed = true;
                    leftShoulderRepeatTick = controllerButtonDownTick;
                  } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                    rightShoulderPressed = true;
                    rightShoulderRepeatTick = controllerButtonDownTick;
                  }

                  command = translate_controller_button(event.cbutton.button);
                  if (is_navigation_command(command)) {
                    seed_controller_navigation_hold_state(
                      controllerButtonDownTick,
                      command,
                      &moveUpHoldState,
                      &moveDownHoldState,
                      &moveLeftHoldState,
                      &moveRightHoldState
                    );
                  }
                }

                if (  // NOSONAR(cpp:S134) exit-combo arming stays inline with the button state machine
                  controllerStartPressed && controllerBackPressed && (state.activeScreen == app::ScreenId::home || state.activeScreen == app::ScreenId::hosts)
                ) {
                  controllerExitComboArmed = true;
                }
                break;
              }
            case SDL_CONTROLLERBUTTONUP:
              if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START && controllerStartPressed) {
                controllerStartPressed = false;
                if (!controllerExitComboArmed && !controllerExitComboTriggered) {
                  command = input::map_gamepad_button_to_ui_command(input::GamepadButton::start);
                }
                if (!controllerStartPressed && !controllerBackPressed) {
                  controllerExitComboArmed = false;
                  controllerExitComboTriggered = false;
                }
              } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK && controllerBackPressed) {
                controllerBackPressed = false;
                if (!controllerExitComboArmed && !controllerExitComboTriggered) {
                  command = input::map_gamepad_button_to_ui_command(input::GamepadButton::back);
                }
                if (!controllerStartPressed && !controllerBackPressed) {
                  controllerExitComboArmed = false;
                  controllerExitComboTriggered = false;
                }
              } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                leftShoulderPressed = false;
              } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                rightShoulderPressed = false;
              }
              release_controller_navigation_hold_state(translate_controller_button(event.cbutton.button), &moveUpHoldState, &moveDownHoldState, &moveLeftHoldState, &moveRightHoldState);
              break;
            case SDL_CONTROLLERAXISMOTION:
              command = translate_trigger_axis(event.caxis, &leftTriggerPressed, &rightTriggerPressed);
              if (command == input::UiCommand::fast_previous_page) {  // NOSONAR(cpp:S134) trigger repeat bookkeeping stays inline with translated command handling
                leftTriggerRepeatTick = SDL_GetTicks();
              } else if (command == input::UiCommand::fast_next_page) {
                rightTriggerRepeatTick = SDL_GetTicks();
              }
              break;
            case SDL_KEYDOWN:
              if (event.key.repeat == 0) {  // NOSONAR(cpp:S134) keyboard translation stays inline with SDL event routing
                command = translate_keyboard_key(event.key.keysym.sym, event.key.keysym.mod);
              }
              break;
            default:
              break;
          }

          process_command(command);
          if (command != input::UiCommand::none && !is_navigation_command(command)) {
            controllerNavigationNeutralRequired = true;
            skipPolledControllerNavigation = true;
            reset_controller_navigation_hold_states(&moveUpHoldState, &moveDownHoldState, &moveLeftHoldState, &moveRightHoldState);
          }
        } while (SDL_PollEvent(&event));
      }

      if (controllerNavigationNeutralRequired) {
        if (is_controller_navigation_active(controller)) {
          reset_controller_navigation_hold_states(&moveUpHoldState, &moveDownHoldState, &moveLeftHoldState, &moveRightHoldState);
        } else {
          controllerNavigationNeutralRequired = false;
        }
      }

      if (!skipPolledControllerNavigation && !controllerNavigationNeutralRequired) {
        process_command(poll_controller_navigation(controller, SDL_GetTicks(), &moveUpHoldState, &moveDownHoldState, &moveLeftHoldState, &moveRightHoldState));
      }

      finish_pairing_task_if_ready(state, &pairingTask);
      finish_app_list_task_if_ready(state, &appListTask);
      finish_app_art_task_if_ready(state, &appArtTask, &coverArtTextureCache);
      finish_host_probe_task_if_ready(state, &hostProbeTask);
      const Uint32 backgroundTaskTick = SDL_GetTicks();
      start_host_probe_task_if_needed(state, &hostProbeTask, backgroundTaskTick, &nextHostProbeTick);
      start_app_list_task_if_needed(state, &appListTask, backgroundTaskTick);
      start_app_art_task_if_needed(state, &appArtTask);

      if ((state.activeScreen != app::ScreenId::add_host || !state.addHostDraft.keypad.visible || keypadRedrawRequested) && !draw_current_shell()) {
        break;
      }
    }

    if (pairingTask.activeAttempt != nullptr) {
      pairingTask.activeAttempt->discardResult.store(true);
      finalize_pairing_attempt(nullptr, std::move(pairingTask.activeAttempt));
    }
    while (!pairingTask.retiredAttempts.empty()) {
      std::unique_ptr<PairingAttemptState> attempt = std::move(pairingTask.retiredAttempts.back());
      pairingTask.retiredAttempts.pop_back();
      if (attempt != nullptr) {
        attempt->discardResult.store(true);
      }
      finalize_pairing_attempt(nullptr, std::move(attempt));
    }
    if (appListTask.thread != nullptr) {
      int threadResult = 0;
      SDL_WaitThread(appListTask.thread, &threadResult);
      (void) threadResult;
    }
    if (appArtTask.thread != nullptr) {
      int threadResult = 0;
      SDL_WaitThread(appArtTask.thread, &threadResult);
      (void) threadResult;
    }
    for (const std::unique_ptr<HostProbeTaskState::ProbeWorkerState> &worker : hostProbeTask.workers) {
      if (worker == nullptr || worker->thread == nullptr) {
        continue;
      }

      int threadResult = 0;
      SDL_WaitThread(worker->thread, &threadResult);
      (void) threadResult;
    }

    close_controller(controller);
    clear_cover_art_texture_cache(&coverArtTextureCache);
    clear_asset_texture_cache(&assetTextureCache);
    clear_keypad_modal_layout_cache(&keypadModalLayoutCache);
    destroy_texture(titleLogoTexture);
    TTF_CloseFont(smallFont);
    TTF_CloseFont(bodyFont);
    TTF_CloseFont(titleFont);
    SDL_DestroyRenderer(renderer);
    IMG_Quit();
    TTF_Quit();
    return 0;
  }

}  // namespace ui
