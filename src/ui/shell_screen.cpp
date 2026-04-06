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
#include <windows.h>

// local includes
#include "src/input/navigation_input.h"
#include "src/logging/log_file.h"
#include "src/network/host_pairing.h"
#include "src/network/runtime_network.h"
#include "src/os.h"
#include "src/startup/client_identity_storage.h"
#include "src/startup/cover_art_cache.h"
#include "src/startup/host_storage.h"
#include "src/startup/saved_files.h"
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
  constexpr Uint32 CONTEXT_HOLD_MILLISECONDS = 550U;
  constexpr Uint32 EXIT_COMBO_HOLD_MILLISECONDS = 900U;
  constexpr Uint32 LOG_VIEWER_SCROLL_REPEAT_MILLISECONDS = 110U;
  constexpr Uint32 LOG_VIEWER_FAST_SCROLL_REPEAT_MILLISECONDS = 45U;
  constexpr int MIN_SVG_RASTER_DIMENSION = 256;
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

  Uint32 read_surface_pixel(const SDL_Surface *surface, int x, int y) {
    const Uint8 *row = static_cast<const Uint8 *>(surface->pixels) + (y * surface->pitch);
    Uint32 pixel = 0;
    std::memcpy(&pixel, row + (x * static_cast<int>(sizeof(Uint32))), sizeof(Uint32));
    return pixel;
  }

  void write_surface_pixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {
    Uint8 *row = static_cast<Uint8 *>(surface->pixels) + (y * surface->pitch);
    std::memcpy(row + (x * static_cast<int>(sizeof(Uint32))), &pixel, sizeof(Uint32));
  }

  Uint32 sample_bilinear_pixel(const SDL_Surface *sourceSurface, float sourceX, float sourceY, const SDL_PixelFormat *targetFormat) {
    const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, sourceSurface->w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, sourceSurface->h - 1);
    const int x1 = std::min(x0 + 1, sourceSurface->w - 1);
    const int y1 = std::min(y0 + 1, sourceSurface->h - 1);
    const float tx = std::clamp(sourceX - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = std::clamp(sourceY - static_cast<float>(y0), 0.0f, 1.0f);

    Uint8 topLeftRed = 0;
    Uint8 topLeftGreen = 0;
    Uint8 topLeftBlue = 0;
    Uint8 topLeftAlpha = 0;
    Uint8 topRightRed = 0;
    Uint8 topRightGreen = 0;
    Uint8 topRightBlue = 0;
    Uint8 topRightAlpha = 0;
    Uint8 bottomLeftRed = 0;
    Uint8 bottomLeftGreen = 0;
    Uint8 bottomLeftBlue = 0;
    Uint8 bottomLeftAlpha = 0;
    Uint8 bottomRightRed = 0;
    Uint8 bottomRightGreen = 0;
    Uint8 bottomRightBlue = 0;
    Uint8 bottomRightAlpha = 0;

    SDL_GetRGBA(read_surface_pixel(sourceSurface, x0, y0), sourceSurface->format, &topLeftRed, &topLeftGreen, &topLeftBlue, &topLeftAlpha);
    SDL_GetRGBA(read_surface_pixel(sourceSurface, x1, y0), sourceSurface->format, &topRightRed, &topRightGreen, &topRightBlue, &topRightAlpha);
    SDL_GetRGBA(read_surface_pixel(sourceSurface, x0, y1), sourceSurface->format, &bottomLeftRed, &bottomLeftGreen, &bottomLeftBlue, &bottomLeftAlpha);
    SDL_GetRGBA(read_surface_pixel(sourceSurface, x1, y1), sourceSurface->format, &bottomRightRed, &bottomRightGreen, &bottomRightBlue, &bottomRightAlpha);

    const float topRed = (static_cast<float>(topLeftRed) * (1.0f - tx)) + (static_cast<float>(topRightRed) * tx);
    const float topGreen = (static_cast<float>(topLeftGreen) * (1.0f - tx)) + (static_cast<float>(topRightGreen) * tx);
    const float topBlue = (static_cast<float>(topLeftBlue) * (1.0f - tx)) + (static_cast<float>(topRightBlue) * tx);
    const float topAlpha = (static_cast<float>(topLeftAlpha) * (1.0f - tx)) + (static_cast<float>(topRightAlpha) * tx);
    const float bottomRed = (static_cast<float>(bottomLeftRed) * (1.0f - tx)) + (static_cast<float>(bottomRightRed) * tx);
    const float bottomGreen = (static_cast<float>(bottomLeftGreen) * (1.0f - tx)) + (static_cast<float>(bottomRightGreen) * tx);
    const float bottomBlue = (static_cast<float>(bottomLeftBlue) * (1.0f - tx)) + (static_cast<float>(bottomRightBlue) * tx);
    const float bottomAlpha = (static_cast<float>(bottomLeftAlpha) * (1.0f - tx)) + (static_cast<float>(bottomRightAlpha) * tx);

    return SDL_MapRGBA(
      targetFormat,
      static_cast<Uint8>((topRed * (1.0f - ty)) + (bottomRed * ty)),
      static_cast<Uint8>((topGreen * (1.0f - ty)) + (bottomGreen * ty)),
      static_cast<Uint8>((topBlue * (1.0f - ty)) + (bottomBlue * ty)),
      static_cast<Uint8>((topAlpha * (1.0f - ty)) + (bottomAlpha * ty))
    );
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

  SDL_Surface *create_scaled_surface_bilinear(SDL_Surface *sourceSurface, int targetWidth, int targetHeight) {
    if (sourceSurface == nullptr || targetWidth <= 0 || targetHeight <= 0) {
      return nullptr;
    }

    SDL_Surface *scaledSurface = SDL_CreateRGBSurfaceWithFormat(0, targetWidth, targetHeight, 32, SDL_PIXELFORMAT_ARGB8888);
    if (scaledSurface == nullptr) {
      SDL_FreeSurface(sourceSurface);
      return nullptr;
    }

    if (SDL_LockSurface(sourceSurface) < 0) {
      SDL_FreeSurface(sourceSurface);
      SDL_FreeSurface(scaledSurface);
      return nullptr;
    }

    if (SDL_LockSurface(scaledSurface) < 0) {
      SDL_UnlockSurface(sourceSurface);
      SDL_FreeSurface(sourceSurface);
      SDL_FreeSurface(scaledSurface);
      return nullptr;
    }

    for (int y = 0; y < scaledSurface->h; ++y) {
      const float sourceY = ((static_cast<float>(y) + 0.5f) * static_cast<float>(sourceSurface->h) / static_cast<float>(scaledSurface->h)) - 0.5f;
      for (int x = 0; x < scaledSurface->w; ++x) {
        const float sourceX = ((static_cast<float>(x) + 0.5f) * static_cast<float>(sourceSurface->w) / static_cast<float>(scaledSurface->w)) - 0.5f;
        write_surface_pixel(scaledSurface, x, y, sample_bilinear_pixel(sourceSurface, sourceX, sourceY, scaledSurface->format));
      }
    }

    SDL_UnlockSurface(scaledSurface);
    SDL_UnlockSurface(sourceSurface);
    SDL_FreeSurface(sourceSurface);

    if (SDL_SetSurfaceBlendMode(scaledSurface, SDL_BLENDMODE_BLEND) != 0) {
      SDL_FreeSurface(scaledSurface);
      return nullptr;
    }

    return scaledSurface;
  }

  SDL_Surface *prepare_asset_surface(SDL_Surface *surface, const char *relativePath) {
    SDL_Surface *normalizedSurface = normalize_asset_surface(surface);
    if (normalizedSurface == nullptr || !asset_path_uses_svg(relativePath)) {
      return normalizedSurface;
    }

    const int sourceMaxDimension = std::max(normalizedSurface->w, normalizedSurface->h);
    if (sourceMaxDimension <= 0 || sourceMaxDimension >= MIN_SVG_RASTER_DIMENSION) {
      return normalizedSurface;
    }

    const int targetWidth = std::max(1, (normalizedSurface->w * MIN_SVG_RASTER_DIMENSION) / sourceMaxDimension);
    const int targetHeight = std::max(1, (normalizedSurface->h * MIN_SVG_RASTER_DIMENSION) / sourceMaxDimension);
    return create_scaled_surface_bilinear(normalizedSurface, targetWidth, targetHeight);
  }

  int report_shell_failure(logging::Logger &logger, const char *category, const std::string &message) {
    logger.log(logging::LogLevel::error, category, message);
    logger.log(logging::LogLevel::warning, category, "Holding the failure screen for 5 seconds before exit.");
    Sleep(5000);
    return 1;
  }

  bool host_matches_endpoint(const app::HostRecord &host, const std::string &address, uint16_t port) {
    if (host.address != address) {
      return false;
    }

    const uint16_t effectivePort = app::effective_host_port(port);
    if (app::effective_host_port(host.port) == effectivePort) {
      return true;
    }
    if (host.resolvedHttpPort != 0 && host.resolvedHttpPort == effectivePort) {
      return true;
    }
    if (host.httpsPort != 0 && host.httpsPort == effectivePort) {
      return true;
    }
    return false;
  }

  bool append_error(std::string *errorMessage, std::string message) {
    if (errorMessage != nullptr) {
      *errorMessage = std::move(message);
    }

    return false;
  }

  void destroy_texture(SDL_Texture *texture) {
    if (texture != nullptr) {
      SDL_DestroyTexture(texture);
    }
  }

  struct CoverArtTextureCache {
    std::unordered_map<std::string, SDL_Texture *> textures;
    std::unordered_map<std::string, bool> failedKeys;
  };

  struct AssetTextureCache {
    std::unordered_map<std::string, SDL_Texture *> textures;
    std::unordered_map<std::string, bool> failedKeys;
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

    for (const auto &entry : cache->textures) {
      destroy_texture(entry.second);
    }
    cache->textures.clear();
    cache->failedKeys.clear();
  }

  void clear_cover_art_texture(CoverArtTextureCache *cache, const std::string &cacheKey) {
    if (cache == nullptr || cacheKey.empty()) {
      return;
    }

    const auto textureIterator = cache->textures.find(cacheKey);
    if (textureIterator != cache->textures.end()) {
      destroy_texture(textureIterator->second);
      cache->textures.erase(textureIterator);
    }
    cache->failedKeys.erase(cacheKey);
  }

  void clear_asset_texture_cache(AssetTextureCache *cache) {
    if (cache == nullptr) {
      return;
    }

    for (const auto &entry : cache->textures) {
      destroy_texture(entry.second);
    }
    cache->textures.clear();
    cache->failedKeys.clear();
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
    for (std::size_t index = 0; index < text.size(); ++index) {
      const unsigned char character = static_cast<unsigned char>(text[index]);
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
      if ((character & 0xE0U) == 0xC0U) {
        sequenceLength = 2U;
      } else if ((character & 0xF0U) == 0xE0U) {
        sequenceLength = 3U;
      } else if ((character & 0xF8U) == 0xF0U) {
        sequenceLength = 4U;
      }

      const bool sequenceAvailable = sequenceLength > 0U && index + sequenceLength <= text.size();
      bool sequenceValid = sequenceAvailable;
      for (std::size_t continuationIndex = 1U; sequenceValid && continuationIndex < sequenceLength; ++continuationIndex) {
        const unsigned char continuation = static_cast<unsigned char>(text[index + continuationIndex]);
        sequenceValid = (continuation & 0xC0U) == 0x80U;
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
    AssetTextureCache *assetCache
  );

  SDL_Texture *load_cover_art_texture(SDL_Renderer *renderer, CoverArtTextureCache *cache, const std::string &cacheKey) {
    if (renderer == nullptr || cache == nullptr || cacheKey.empty()) {
      return nullptr;
    }

    const auto existingTexture = cache->textures.find(cacheKey);
    if (existingTexture != cache->textures.end()) {
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

    cache->textures.emplace(cacheKey, texture);
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

    surface = prepare_asset_surface(surface, relativePath);
    if (surface == nullptr) {
      return nullptr;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
  }

  SDL_Texture *load_cached_asset_texture(SDL_Renderer *renderer, AssetTextureCache *cache, const std::string &relativePath) {
    if (renderer == nullptr || cache == nullptr || relativePath.empty()) {
      return nullptr;
    }

    const auto existingTexture = cache->textures.find(relativePath);
    if (existingTexture != cache->textures.end()) {
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

    cache->textures.emplace(relativePath, texture);
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

  bool render_text_line(
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
      const std::string candidate = sanitize_ascii_text_for_render(sanitized.substr(0, length)) + ellipsis;
      if (TTF_SizeText(font, candidate.c_str(), &textWidth, &textHeight) == 0 && textWidth <= maxWidth) {
        return candidate;
      }
    }

    return ellipsis;
  }

  bool render_text_line_simple(
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
    AssetTextureCache *assetCache
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

  void draw_line(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 0xFF) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
  }

  void draw_host_icon(SDL_Renderer *renderer, const SDL_Rect &rect, const ui::ShellHostTile &tile) {
    const Uint8 iconRed = tile.reachability == app::HostReachability::online ? TEXT_RED : MUTED_RED;
    const Uint8 iconGreen = tile.reachability == app::HostReachability::online ? TEXT_GREEN : MUTED_GREEN;
    const Uint8 iconBlue = tile.reachability == app::HostReachability::online ? TEXT_BLUE : MUTED_BLUE;

    const int iconWidth = std::min(rect.w, std::max(52, (rect.h * 10) / 9));
    const int iconHeight = std::min(rect.h, std::max(44, (iconWidth * 4) / 5));
    const SDL_Rect iconRect {
      rect.x + std::max(0, (rect.w - iconWidth) / 2),
      rect.y + std::max(0, (rect.h - iconHeight) / 2),
      iconWidth,
      iconHeight,
    };
    const SDL_Rect monitorRect {iconRect.x + (iconRect.w / 10), iconRect.y + (iconRect.h / 14), (iconRect.w * 4) / 5, (iconRect.h * 3) / 5};
    const SDL_Rect standRect {iconRect.x + (iconRect.w * 7) / 16, monitorRect.y + monitorRect.h + std::max(4, iconRect.h / 20), std::max(6, iconRect.w / 8), std::max(6, iconRect.h / 10)};
    const SDL_Rect baseRect {iconRect.x + (iconRect.w * 5) / 16, standRect.y + standRect.h, iconRect.w / 4, std::max(4, iconRect.h / 18)};
    draw_rect(renderer, monitorRect, iconRed, iconGreen, iconBlue);
    draw_rect(renderer, standRect, iconRed, iconGreen, iconBlue);
    draw_rect(renderer, baseRect, iconRed, iconGreen, iconBlue);

    const int symbolMargin = std::max(4, std::min(monitorRect.w, monitorRect.h) / 5);
    const SDL_Rect symbolRect {
      monitorRect.x + symbolMargin,
      monitorRect.y + symbolMargin,
      std::max(10, monitorRect.w - (symbolMargin * 2)),
      std::max(10, monitorRect.h - (symbolMargin * 2)),
    };
    if (tile.reachability != app::HostReachability::online) {
      const int centerX = symbolRect.x + (symbolRect.w / 2);
      const int topY = symbolRect.y;
      const int bottomY = symbolRect.y + symbolRect.h;
      draw_line(renderer, centerX, topY, symbolRect.x, bottomY, iconRed, iconGreen, iconBlue);
      draw_line(renderer, symbolRect.x, bottomY, symbolRect.x + symbolRect.w, bottomY, iconRed, iconGreen, iconBlue);
      draw_line(renderer, symbolRect.x + symbolRect.w, bottomY, centerX, topY, iconRed, iconGreen, iconBlue);
      fill_rect(renderer, {centerX - 2, symbolRect.y + std::max(4, symbolRect.h / 5), 4, std::max(6, symbolRect.h / 3)}, iconRed, iconGreen, iconBlue);
      fill_rect(renderer, {centerX - 2, bottomY - std::max(6, symbolRect.h / 6), 4, 4}, iconRed, iconGreen, iconBlue);
      return;
    }

    if (tile.pairingState != app::PairingState::paired) {
      const int bodyWidth = std::max(10, (symbolRect.w * 3) / 5);
      const int bodyHeight = std::max(8, (symbolRect.h * 2) / 5);
      const SDL_Rect bodyRect {
        symbolRect.x + std::max(0, (symbolRect.w - bodyWidth) / 2),
        symbolRect.y + std::max(4, symbolRect.h / 3),
        bodyWidth,
        bodyHeight,
      };
      const SDL_Rect shackleRect {
        bodyRect.x + std::max(1, bodyRect.w / 8),
        symbolRect.y + 2,
        std::max(8, (bodyRect.w * 3) / 4),
        std::max(8, symbolRect.h / 2),
      };
      draw_rect(renderer, shackleRect, iconRed, iconGreen, iconBlue);
      draw_rect(renderer, bodyRect, iconRed, iconGreen, iconBlue);
      return;
    }

    draw_line(renderer, symbolRect.x + std::max(2, symbolRect.w / 10), symbolRect.y + (symbolRect.h / 2), symbolRect.x + (symbolRect.w / 2) - 1, symbolRect.y + symbolRect.h - std::max(2, symbolRect.h / 8), iconRed, iconGreen, iconBlue);
    draw_line(renderer, symbolRect.x + (symbolRect.w / 2) - 1, symbolRect.y + symbolRect.h - std::max(2, symbolRect.h / 8), symbolRect.x + symbolRect.w - std::max(2, symbolRect.w / 10), symbolRect.y + std::max(2, symbolRect.h / 10), iconRed, iconGreen, iconBlue);
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
    SDL_Texture *texture = tile.boxArtCached ? load_cover_art_texture(renderer, textureCache, tile.boxArtCacheKey) : nullptr;
    if (texture != nullptr) {
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

  bool render_log_viewer_modal(
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
    const std::vector<ui::ShellFooterAction> logViewerActions = {
      {"change-view", "Change View", "icons\\button-x.svg", "icons\\button-y.svg", false},
      {"scroll", "Scroll", "icons\\button-lb.svg", "icons\\button-rb.svg", false},
      {"fast-scroll", "Fast Scroll", "icons\\button-lt.svg", "icons\\button-rt.svg", false},
      {"close", "Close", "icons\\button-a.svg", "icons\\button-b.svg", false},
    };
    if (!render_footer_actions(renderer, smallFont, assetCache, logViewerActions, hintRect)) {
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
    auto build_log_viewer_layout = [&](int availableWidth) {
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
    const bool olderLinesAvailable = logViewerLayout.firstVisibleIndex > 0U;
    if (olderLinesAvailable) {
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

    if (viewModel.logViewerScrollOffset > 0U) {
      if (!render_text_line_simple(renderer, smallFont, "Newer lines below", {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, textRect.x + 6, std::max(textRect.y + 6, textRect.y + textRect.h - TTF_FontLineSkip(smallFont) - 6), std::max(1, textRect.w - 12))) {
        return false;
      }
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
    const std::size_t visibleRowCount = static_cast<std::size_t>(std::max(1, (rect.h + rowSpacing) / std::max(1, rowStep)));
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
      const SDL_Color color = row.enabled ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF} : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF};
      if (!render_text_line_simple(renderer, font, label, color, rowRect.x + 12, rowRect.y + 8, rowRect.w - 24)) {
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
    const bool renderedIcon = !button.iconAssetPath.empty() && render_asset_icon(renderer, assetCache, button.iconAssetPath, iconRect);
    if (!renderedIcon) {
      if (!render_text_centered(renderer, font, button.glyph, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, iconRect)) {
        return false;
      }
    }

    return render_text_line(renderer, smallFont, button.label, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, iconRect.x + iconRect.w + 8, buttonRect.y + std::max(6, (buttonRect.h - TTF_FontLineSkip(smallFont)) / 2), buttonRect.w - (iconRect.w + 26));
  }

  bool render_footer_actions(
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
      int labelHeight = 0;
      if (TTF_SizeUTF8(font, action.label.c_str(), &labelWidth, &labelHeight) != 0) {
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

  bool render_notification(
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
    const int selectedRow = static_cast<int>(std::min(selectedIndex, itemCount - 1U) / columnCount);
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

  void log_app_update(logging::Logger &logger, const app::ClientState &state, const app::AppUpdate &update) {
    if (!update.activatedItemId.empty()) {
      logger.log(logging::LogLevel::info, "ui", "Activated menu item: " + update.activatedItemId);
    }
    if (update.screenChanged) {
      logger.log(logging::LogLevel::info, "ui", std::string("Switched screen to ") + app::to_string(state.activeScreen));
    }
    if (update.overlayVisibilityChanged) {
      logger.log(logging::LogLevel::info, "overlay", state.overlayVisible ? "Overlay enabled" : "Overlay disabled");
    }
    if (update.exitRequested) {
      logger.log(logging::LogLevel::info, "app", "Exit requested from shell");
    }
  }

  bool persist_hosts(logging::Logger &logger, app::ClientState &state) {
    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(state.hosts);
    if (saveResult.success) {
      state.hostsDirty = false;
      logger.log(logging::LogLevel::info, "hosts", "Saved host records");
      return true;
    }

    logger.log(logging::LogLevel::error, "hosts", saveResult.errorMessage);
    return false;
  }

  void persist_hosts_if_needed(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update) {
    if (!update.hostsChanged) {
      return;
    }

    persist_hosts(logger, state);
  }

  void apply_server_info_to_host(app::ClientState &state, const std::string &address, uint16_t port, const network::HostPairingServerInfo &serverInfo) {
    for (app::HostRecord &host : state.hosts) {
      if (!host_matches_endpoint(host, address, port)) {
        continue;
      }

      bool persistedMetadataChanged = false;
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
        const app::PairingState resolvedPairingState = serverInfo.pairingStatusCurrentClient ? app::PairingState::paired : app::PairingState::not_paired;
        persistedMetadataChanged = persistedMetadataChanged || host.pairingState != resolvedPairingState;
        host.pairingState = resolvedPairingState;
        if (!serverInfo.pairingStatusCurrentClient) {
          host.apps.clear();
          host.appListState = app::HostAppListState::failed;
          host.appListStatusMessage = "The host reports that this client is no longer paired. Pair the host again from Sunshine.";
          host.appListContentHash = 0;
          host.lastAppListRefreshTick = 0;
          state.selectedAppIndex = 0U;
          if (state.activeScreen == app::ScreenId::apps && state.selectedHostIndex < state.hosts.size() && &host == &state.hosts[state.selectedHostIndex]) {
            state.statusMessage = host.appListStatusMessage;
          }
        }
      }
      state.hostsDirty = state.hostsDirty || persistedMetadataChanged;
      break;
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
    const std::string coverArtRoot = startup::default_cover_art_cache_root();
    if (coverArtRoot.empty() || path.size() <= coverArtRoot.size() || path.rfind(coverArtRoot, 0U) != 0U) {
      return {};
    }

    const std::size_t fileNameStart = path.find_last_of("\\/");
    if (fileNameStart == std::string::npos || fileNameStart + 1U >= path.size()) {
      return {};
    }

    const std::string fileName = path.substr(fileNameStart + 1U);
    if (fileName.size() <= 4U || fileName.substr(fileName.size() - 4U) != ".bin") {
      return {};
    }
    return fileName.substr(0, fileName.size() - 4U);
  }

  void clear_deleted_cover_art_flag(app::ClientState &state, const std::string &cacheKey) {
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

  void refresh_saved_files_if_needed(logging::Logger &logger, app::ClientState &state) {
    if (state.activeScreen != app::ScreenId::settings || !state.savedFilesDirty) {
      return;
    }

    const startup::ListSavedFilesResult savedFiles = startup::list_saved_files();
    for (const std::string &warning : savedFiles.warnings) {
      logger.log(logging::LogLevel::warning, "storage", warning);
    }
    app::replace_saved_files(state, savedFiles.files);
  }

  void delete_saved_file_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update, CoverArtTextureCache *coverArtTextureCache) {
    if (!update.savedFileDeleteRequested) {
      return;
    }

    std::string errorMessage;
    if (!startup::delete_saved_file(update.savedFileDeletePath, &errorMessage)) {
      state.statusMessage = errorMessage;
      logger.log(logging::LogLevel::warning, "storage", errorMessage);
      return;
    }

    const std::string deletedDisplayName = display_name_for_saved_file(state, update.savedFileDeletePath);
    const std::string deletedCoverArtCacheKey = cover_art_cache_key_from_path(update.savedFileDeletePath);
    clear_deleted_cover_art_flag(state, deletedCoverArtCacheKey);
    clear_cover_art_texture(coverArtTextureCache, deletedCoverArtCacheKey);
    state.savedFilesDirty = true;
    state.statusMessage = "Deleted saved file " + deletedDisplayName;
    logger.log(logging::LogLevel::info, "storage", state.statusMessage);
  }

  void factory_reset_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update, CoverArtTextureCache *coverArtTextureCache) {
    if (!update.factoryResetRequested) {
      return;
    }

    std::string errorMessage;
    if (!startup::delete_all_saved_files(&errorMessage)) {
      state.statusMessage = errorMessage;
      logger.log(logging::LogLevel::warning, "storage", errorMessage);
      return;
    }

    state.hosts.clear();
    state = app::create_initial_state();
    state.savedFiles.clear();
    state.savedFilesDirty = true;
    state.statusMessage = "Factory reset completed";
    clear_cover_art_texture_cache(coverArtTextureCache);
    app::set_log_file_path(state, logging::default_log_file_path());
    logger.log(logging::LogLevel::info, "storage", state.statusMessage);
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
      return append_error(errorMessage, "No valid paired client identity is available. Pair the host again before browsing apps.");
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
    std::string errorMessage;
    if (!network::query_server_info(address, port, clientIdentity, &serverInfo, &errorMessage)) {
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
    struct TargetHost {
      std::string address;
      uint16_t port = 0;
    };

    struct ProbeResult {
      std::string address;
      uint16_t port = 0;
      bool success = false;
      network::HostPairingServerInfo serverInfo;
    };

    SDL_Thread *thread = nullptr;
    std::atomic<bool> completed = false;
    std::vector<TargetHost> targets;
    std::vector<ProbeResult> results;
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
    return task.activeAttempt != nullptr && task.activeAttempt->thread != nullptr && !task.activeAttempt->completed.load();
  }

  bool pairing_attempt_is_ready(const PairingAttemptState *attempt) {
    return attempt != nullptr && attempt->thread != nullptr && attempt->completed.load(std::memory_order_acquire);
  }

  void finalize_pairing_attempt(logging::Logger &logger, app::ClientState *state, std::unique_ptr<PairingAttemptState> attempt) {
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
      logger.log(entry.level, "pairing", entry.message);
    }

    if (discardResult || state == nullptr) {
      logger.log(logging::LogLevel::info, "pairing", "Ignored a completed pairing result after leaving the pairing screen or starting a new attempt");
      return;
    }

    const bool hostsChanged = app::apply_pairing_result(
      *state,
      request.address,
      request.httpPort,
      result.success || result.alreadyPaired,
      result.message
    );

    logger.log(result.success || result.alreadyPaired ? logging::LogLevel::info : logging::LogLevel::warning, "pairing", result.message);
    if (hostsChanged) {
      persist_hosts(logger, *state);
    }
  }

  void retire_active_pairing_attempt(PairingTaskState *task, bool discardResult) {
    if (task == nullptr || task->activeAttempt == nullptr) {
      return;
    }

    if (discardResult) {
      task->activeAttempt->discardResult.store(true);
    }
    task->activeAttempt->cancelRequested.store(true, std::memory_order_release);
    task->retiredAttempts.push_back(std::move(task->activeAttempt));
  }

  void reap_retired_pairing_attempts(logging::Logger &logger, PairingTaskState *task) {
    if (task == nullptr) {
      return;
    }

    auto iterator = task->retiredAttempts.begin();
    while (iterator != task->retiredAttempts.end()) {
      if (!pairing_attempt_is_ready(iterator->get())) {
        ++iterator;
        continue;
      }

      finalize_pairing_attempt(logger, nullptr, std::move(*iterator));
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

    task->thread = nullptr;
    task->completed.store(false);
    task->targets.clear();
    task->results.clear();
  }

  bool host_probe_task_is_active(const HostProbeTaskState &task) {
    return task.thread != nullptr && !task.completed.load();
  }

  int run_pairing_task(void *context) {
    PairingAttemptState *task = static_cast<PairingAttemptState *>(context);
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
        task->completed.store(true, std::memory_order_release);
        return 0;
      }

      const startup::SaveClientIdentityResult saveResult = startup::save_client_identity(identity);
      if (!saveResult.success) {
        task->result = {false, false, saveResult.errorMessage};
        task->completed.store(true, std::memory_order_release);
        return 0;
      }

      task->deferredLogs.push_back({logging::LogLevel::info, "Saved pairing identity"});
    }

    task->request.identity = std::move(identity);
    task->result = network::pair_host(task->request, &task->cancelRequested);
    task->completed.store(true, std::memory_order_release);
    return 0;
  }

  void finish_pairing_task_if_ready(logging::Logger &logger, app::ClientState &state, PairingTaskState *task) {
    if (task == nullptr) {
      return;
    }

    reap_retired_pairing_attempts(logger, task);
    if (!pairing_attempt_is_ready(task->activeAttempt.get())) {
      return;
    }

    finalize_pairing_attempt(logger, &state, std::move(task->activeAttempt));
  }

  void cancel_pairing_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update, PairingTaskState *task) {
    if (task == nullptr || !update.pairingCancelledRequested || task->activeAttempt == nullptr || task->activeAttempt->thread == nullptr) {
      return;
    }

    task->activeAttempt->discardResult.store(true);
    task->activeAttempt->cancelRequested.store(true, std::memory_order_release);
    retire_active_pairing_attempt(task, true);
    state.statusMessage.clear();
    logger.log(logging::LogLevel::info, "pairing", "Cancelled the in-flight pairing attempt after leaving the pairing screen");
  }

  void test_host_connection_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update) {
    if (!update.connectionTestRequested) {
      return;
    }

    const std::string address = update.connectionTestAddress;
    const uint16_t port = update.connectionTestPort == 0 ? app::DEFAULT_HOST_PORT : update.connectionTestPort;

    if (address.empty()) {
      app::apply_connection_test_result(state, false, "Connection test failed because the host address is invalid");
      logger.log(logging::LogLevel::warning, "hosts", state.statusMessage);
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
    logger.log(success ? logging::LogLevel::info : logging::LogLevel::warning, "hosts", resultMessage);
    if (state.hostsDirty) {
      persist_hosts(logger, state);
    }
  }

  void browse_host_apps_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update) {
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
      logger.log(logging::LogLevel::warning, "apps", resultMessage);
      return;
    }

    apply_server_info_to_host(state, address, port, serverInfo);
    if (state.hostsDirty) {
      persist_hosts(logger, state);
    }

    host = app::selected_host(state);
    if (host == nullptr || host->pairingState != app::PairingState::paired) {
      state.statusMessage = host != nullptr && !host->appListStatusMessage.empty() ? host->appListStatusMessage : "This host is no longer paired. Pair it again from Sunshine before opening apps.";
      logger.log(logging::LogLevel::warning, "apps", state.statusMessage);
      return;
    }

    if (app::begin_selected_host_app_browse(state, update.appsBrowseShowHidden)) {
      logger.log(logging::LogLevel::info, "apps", "Authorized host browse for " + host->displayName);
      return;
    }

    logger.log(logging::LogLevel::warning, "apps", state.statusMessage.empty() ? "Failed to enter the apps screen" : state.statusMessage);
  }

  int run_host_probe_task(void *context) {
    HostProbeTaskState *task = static_cast<HostProbeTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    network::PairingIdentity clientIdentity {};
    const network::PairingIdentity *clientIdentityPointer = try_load_saved_pairing_identity(&clientIdentity) ? &clientIdentity : nullptr;

    task->results.clear();
    task->results.reserve(task->targets.size());
    for (const HostProbeTaskState::TargetHost &target : task->targets) {
      HostProbeTaskState::ProbeResult result {};
      result.address = target.address;
      result.port = target.port;
      result.success = test_tcp_host_connection(target.address, target.port, clientIdentityPointer, nullptr, &result.serverInfo);
      task->results.push_back(std::move(result));
    }

    task->completed.store(true, std::memory_order_release);
    return 0;
  }

  void finish_host_probe_task_if_ready(logging::Logger &logger, app::ClientState &state, HostProbeTaskState *task) {
    if (task == nullptr || task->thread == nullptr || !task->completed.load(std::memory_order_acquire)) {
      return;
    }

    SDL_Thread *thread = task->thread;
    task->thread = nullptr;
    int threadResult = 0;
    SDL_WaitThread(thread, &threadResult);
    (void) threadResult;

    const std::vector<HostProbeTaskState::ProbeResult> results = task->results;
    reset_host_probe_task(task);

    std::size_t onlineCount = 0;
    std::size_t offlineCount = 0;
    bool metadataChanged = false;
    for (const HostProbeTaskState::ProbeResult &result : results) {
      if (result.success) {
        apply_server_info_to_host(state, result.address, result.port, result.serverInfo);
        metadataChanged = metadataChanged || state.hostsDirty;
        ++onlineCount;
        continue;
      }

      for (app::HostRecord &host : state.hosts) {
        if (host.address == result.address && app::effective_host_port(host.port) == app::effective_host_port(result.port)) {
          host.reachability = app::HostReachability::offline;
          host.manualAddress = result.address;
          ++offlineCount;
          break;
        }
      }
    }

    logger.log(logging::LogLevel::info, "hosts", "Refreshed " + std::to_string(results.size()) + " saved host(s): " + std::to_string(onlineCount) + " online, " + std::to_string(offlineCount) + " offline");
    if (metadataChanged) {
      persist_hosts(logger, state);
    }
  }

  void start_host_probe_task_if_needed(logging::Logger &logger, const app::ClientState &state, HostProbeTaskState *task, Uint32 now, Uint32 *nextHostProbeTick) {
    if (task == nullptr || host_probe_task_is_active(*task) || state.activeScreen != app::ScreenId::hosts || !network::runtime_network_ready()) {
      return;
    }
    if (nextHostProbeTick != nullptr && *nextHostProbeTick != 0U && now < *nextHostProbeTick) {
      return;
    }

    reset_host_probe_task(task);
    for (const app::HostRecord &host : state.hosts) {
      task->targets.push_back({host.address, app::effective_host_port(host.port)});
    }
    if (task->targets.empty()) {
      return;
    }

    task->thread = SDL_CreateThread(run_host_probe_task, "probe-saved-hosts", task);
    if (task->thread == nullptr) {
      logger.log(logging::LogLevel::error, "hosts", std::string("Failed to start the saved-host refresh task: ") + SDL_GetError());
      reset_host_probe_task(task);
      return;
    }

    if (nextHostProbeTick != nullptr) {
      *nextHostProbeTick = now + HOST_PROBE_REFRESH_INTERVAL_MILLISECONDS;
    }
  }

  void pair_host_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update, PairingTaskState *task) {
    if (!update.pairingRequested || task == nullptr) {
      return;
    }

    finish_pairing_task_if_ready(logger, state, task);

    if (pairing_task_is_active(*task)) {
      retire_active_pairing_attempt(task, true);
      logger.log(logging::LogLevel::info, "pairing", "Discarded the previous background pairing attempt and started a fresh one");
    }

    std::string reachabilityMessage;
    network::HostPairingServerInfo serverInfo {};
    network::PairingIdentity clientIdentity {};
    const network::PairingIdentity *clientIdentityPointer = try_load_saved_pairing_identity(&clientIdentity) ? &clientIdentity : nullptr;
    if (!test_tcp_host_connection(update.pairingAddress, update.pairingPort, clientIdentityPointer, &reachabilityMessage, &serverInfo)) {
      for (app::HostRecord &host : state.hosts) {
        if (host.address == update.pairingAddress && app::effective_host_port(host.port) == app::effective_host_port(update.pairingPort)) {
          host.reachability = app::HostReachability::offline;
          host.manualAddress = update.pairingAddress;
          break;
        }
      }
      state.pairingDraft.stage = app::PairingStage::failed;
      state.pairingDraft.generatedPin.clear();
      state.pairingDraft.statusMessage = reachabilityMessage.empty() ? "The host could not be reached for pairing." : reachabilityMessage;
      state.statusMessage = state.pairingDraft.statusMessage;
      logger.log(logging::LogLevel::warning, "pairing", state.pairingDraft.statusMessage);
      return;
    }

    apply_server_info_to_host(state, update.pairingAddress, update.pairingPort, serverInfo);
    if (state.hostsDirty) {
      persist_hosts(logger, state);
    }

    std::unique_ptr<PairingAttemptState> attempt = std::make_unique<PairingAttemptState>();
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
      logger.log(logging::LogLevel::error, "pairing", createThreadError);
      return;
    }

    task->activeAttempt = std::move(attempt);

    state.pairingDraft.stage = app::PairingStage::in_progress;
    state.pairingDraft.statusMessage = "The host is reachable. If Sunshine prompts for a PIN, enter the code shown below and keep this screen open for the result.";
    state.statusMessage.clear();
    logger.log(logging::LogLevel::info, "pairing", "Started background pairing with " + update.pairingAddress + ":" + std::to_string(update.pairingPort));
  }

  int run_app_list_task(void *context) {
    AppListTaskState *task = static_cast<AppListTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    network::PairingIdentity clientIdentity {};
    std::string errorMessage;
    if (!load_saved_pairing_identity_for_streaming(&clientIdentity, &errorMessage)) {
      task->success = false;
      task->message = errorMessage;
      task->completed.store(true, std::memory_order_release);
      return 0;
    }

    std::vector<network::HostAppEntry> fetchedApps;
    errorMessage.clear();
    task->success = network::query_app_list(task->address, task->port, &clientIdentity, &fetchedApps, &task->serverInfo, &errorMessage);
    task->serverInfoAvailable = task->serverInfo.httpPort != 0 || task->serverInfo.httpsPort != 0 || !task->serverInfo.hostName.empty() || !task->serverInfo.uuid.empty();
    if (!task->success) {
      task->message = errorMessage.empty() ? "Failed to fetch the Sunshine app list" : errorMessage;
      task->completed.store(true, std::memory_order_release);
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

    task->message = task->apps.empty() ? "Sunshine returned no launchable apps for this host" : "Loaded " + std::to_string(task->apps.size()) + " Sunshine app(s)";
    task->completed.store(true, std::memory_order_release);
    return 0;
  }

  void finish_app_list_task_if_ready(logging::Logger &logger, app::ClientState &state, AppListTaskState *task) {
    if (task == nullptr || task->thread == nullptr || !task->completed.load(std::memory_order_acquire)) {
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
      logger.log(logging::LogLevel::info, "apps", "Fetched Sunshine app list from " + address + ":" + std::to_string(serverInfo.httpPort));
      if (state.hostsDirty) {
        persist_hosts(logger, state);
      }
      return;
    }

    app::apply_app_list_result(state, address, port, {}, 0, false, message);
    logger.log(logging::LogLevel::warning, "apps", message);
  }

  void start_app_list_task_if_needed(logging::Logger &logger, app::ClientState &state, AppListTaskState *task, Uint32 now) {
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

      if (state.selectedHostIndex < state.hosts.size()) {
        app::HostRecord &mutableHost = state.hosts[state.selectedHostIndex];
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
      logger.log(logging::LogLevel::error, "apps", errorMessage);
      if (!state.hosts.empty() && state.selectedHostIndex < state.hosts.size()) {
        state.hosts[state.selectedHostIndex].appListState = app::HostAppListState::failed;
        state.hosts[state.selectedHostIndex].appListStatusMessage = errorMessage;
        state.statusMessage = errorMessage;
      }
      reset_app_list_task(task);
      return;
    }

    if (state.selectedHostIndex < state.hosts.size()) {
      state.hosts[state.selectedHostIndex].lastAppListRefreshTick = now;
    }
  }

  int run_app_art_task(void *context) {
    AppArtTaskState *task = static_cast<AppArtTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    network::PairingIdentity clientIdentity {};
    std::string identityError;
    if (!load_saved_pairing_identity_for_streaming(&clientIdentity, &identityError)) {
      task->failureCount = task->apps.size();
      task->completed.store(true, std::memory_order_release);
      return 0;
    }

    for (const app::HostAppRecord &appRecord : task->apps) {
      if (appRecord.boxArtCached || appRecord.boxArtCacheKey.empty()) {
        continue;
      }

      std::vector<unsigned char> assetBytes;
      std::string errorMessage;
      if (!network::query_app_asset(task->address, task->port, &clientIdentity, appRecord.id, &assetBytes, &errorMessage)) {
        ++task->failureCount;
        continue;
      }

      const startup::SaveCoverArtResult saveResult = startup::save_cover_art(appRecord.boxArtCacheKey, assetBytes);
      if (!saveResult.success) {
        ++task->failureCount;
        continue;
      }

      task->cachedAppIds.push_back(appRecord.id);
    }

    task->completed.store(true, std::memory_order_release);
    return 0;
  }

  void finish_app_art_task_if_ready(logging::Logger &logger, app::ClientState &state, AppArtTaskState *task, CoverArtTextureCache *textureCache) {
    if (task == nullptr || task->thread == nullptr || !task->completed.load(std::memory_order_acquire)) {
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
      logger.log(logging::LogLevel::info, "apps", "Cached cover art for " + std::to_string(cachedAppIds.size()) + " app(s)");
    }
    if (failureCount > 0U) {
      logger.log(logging::LogLevel::warning, "apps", std::to_string(failureCount) + " app artwork request(s) fell back to placeholders");
    }
  }

  void start_app_art_task_if_needed(logging::Logger &logger, const app::ClientState &state, AppArtTaskState *task) {
    if (task == nullptr || app_art_task_is_active(*task) || state.activeScreen != app::ScreenId::apps) {
      return;
    }

    const app::HostRecord *host = app::apps_host(state);
    if (host == nullptr || host->appListState != app::HostAppListState::ready || host->apps.empty()) {
      return;
    }

    const bool missingArt = std::any_of(host->apps.begin(), host->apps.end(), [](const app::HostAppRecord &appRecord) {
      return !appRecord.boxArtCached && !appRecord.boxArtCacheKey.empty();
    });
    if (!missingArt) {
      return;
    }

    reset_app_art_task(task);
    task->address = host->address;
    task->port = host->httpsPort == 0 ? app::effective_host_port(host->port) : host->httpsPort;
    task->apps = host->apps;
    task->thread = SDL_CreateThread(run_app_art_task, "fetch-app-art", task);
    if (task->thread == nullptr) {
      logger.log(logging::LogLevel::error, "apps", std::string("Failed to start the cover-art fetch task: ") + SDL_GetError());
      reset_app_art_task(task);
    }
  }

  void show_log_file_if_requested(logging::Logger &logger, app::ClientState &state, const app::AppUpdate &update) {
    if (!update.logViewRequested) {
      return;
    }

    const std::string filePath = state.logFilePath.empty() ? logging::default_log_file_path() : state.logFilePath;
    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, LOG_VIEWER_MAX_LOADED_LINES);
    app::set_log_file_path(state, loadedLog.filePath);
    if (!loadedLog.errorMessage.empty()) {
      app::apply_log_viewer_contents(state, {loadedLog.errorMessage}, loadedLog.errorMessage);
      logger.log(logging::LogLevel::warning, "logging", loadedLog.errorMessage);
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
    logger.log(logging::LogLevel::info, "logging", statusMessage + ": " + loadedLog.filePath);
  }

  bool draw_shell(
    SDL_Renderer *renderer,
    SDL_Texture *titleLogoTexture,
    TTF_Font *titleFont,
    TTF_Font *bodyFont,
    TTF_Font *smallFont,
    const ui::ShellViewModel &viewModel,
    CoverArtTextureCache *textureCache,
    AssetTextureCache *assetCache
  ) {
    int screenWidth = 0;
    int screenHeight = 0;
    if (SDL_GetRendererOutputSize(renderer, &screenWidth, &screenHeight) != 0 || screenWidth <= 0 || screenHeight <= 0) {
      return false;
    }

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
    const bool renderedPageTitle = viewModel.screen == app::ScreenId::apps ? render_text_line_simple(renderer, bodyFont, viewModel.pageTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, pageTitleX, pageTitleY, headerRect.w / 3) : render_text_line(renderer, bodyFont, viewModel.pageTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, pageTitleX, pageTitleY, headerRect.w / 3);
    if (!viewModel.pageTitle.empty() && !renderedPageTitle) {
      return false;
    }

    if (viewModel.screen == app::ScreenId::hosts) {
      const int buttonWidth = std::max(132, headerRect.w / 7);
      const int buttonHeight = std::max(40, headerRect.h / 2);
      int buttonX = headerRect.x + headerRect.w - 16 - ((buttonWidth + 12) * static_cast<int>(viewModel.toolbarButtons.size()));
      for (const ui::ShellToolbarButton &button : viewModel.toolbarButtons) {
        const SDL_Rect buttonRect {buttonX, headerRect.y + 18, buttonWidth, buttonHeight};
        if (!render_toolbar_button(renderer, bodyFont, smallFont, assetCache, button, buttonRect)) {
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
        const int column = static_cast<int>(index % viewModel.hostColumnCount);
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
        const SDL_Rect nameRect {
          tileRect.x + 8,
          tileRect.y + tileRect.h - statusHeight - nameHeight - 10,
          tileRect.w - 16,
          nameHeight,
        };
        if (!render_text_centered_simple(renderer, bodyFont, tile.displayName, online ? SDL_Color {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF} : SDL_Color {MUTED_RED, MUTED_GREEN, MUTED_BLUE, 0xFF}, nameRect)) {
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
          const int column = static_cast<int>(index % viewModel.appColumnCount);
          const SDL_Rect tileRect {
            gridRect.x + gridPadding + (column * (cellWidth + tileGap)) + std::max(0, (cellWidth - tileWidth) / 2),
            gridRect.y + gridPadding + (row * (cellHeight + tileGap)) + std::max(0, (cellHeight - tileHeight) / 2),
            tileWidth,
            tileHeight,
          };
          const ui::ShellAppTile &tile = viewModel.appTiles[index];
          if (!render_app_cover(renderer, smallFont, tile, tileRect, textureCache, assetCache)) {
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
          if (index + 1U < viewModel.bodyLines.size()) {
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
      const int menuPanelWidth = std::max(228, (contentRect.w * 34) / 100);
      const SDL_Rect menuPanel {contentRect.x, contentRect.y, menuPanelWidth, contentRect.h};
      const SDL_Rect bodyPanel {contentRect.x + menuPanelWidth + panelGap, contentRect.y, contentRect.w - menuPanelWidth - panelGap, contentRect.h};
      fill_rect(renderer, menuPanel, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0xA0);
      fill_rect(renderer, bodyPanel, BACKGROUND_RED, BACKGROUND_GREEN, BACKGROUND_BLUE, 0x60);
      draw_rect(renderer, menuPanel, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
      draw_rect(renderer, bodyPanel, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
      if (!render_action_rows(renderer, bodyFont, viewModel.menuRows, {menuPanel.x + 12, menuPanel.y + 18, menuPanel.w - 24, menuPanel.h - 36}, std::max(36, screenHeight / 13))) {
        return false;
      }
      int bodyY = bodyPanel.y + 16;
      if (viewModel.screen == app::ScreenId::settings && !viewModel.selectedMenuRowLabel.empty()) {
        int selectedLabelHeight = 0;
        if (!render_text_line(renderer, bodyFont, "Selected: " + viewModel.selectedMenuRowLabel, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, bodyPanel.x + 16, bodyY, bodyPanel.w - 32, &selectedLabelHeight)) {
          return false;
        }
        bodyY += selectedLabelHeight + 12;
      }
      if (viewModel.screen == app::ScreenId::settings && !viewModel.detailMenuRows.empty()) {
        const int detailMenuHeight = std::min(std::max(88, bodyPanel.h / 3), std::max(88, 54 * static_cast<int>(viewModel.detailMenuRows.size())));
        const SDL_Rect detailMenuRect {bodyPanel.x + 16, bodyY, bodyPanel.w - 32, detailMenuHeight};
        fill_rect(renderer, detailMenuRect, PANEL_ALT_RED, PANEL_ALT_GREEN, PANEL_ALT_BLUE, 0xA8);
        draw_rect(renderer, detailMenuRect, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xD0);
        if (!render_action_rows(renderer, bodyFont, viewModel.detailMenuRows, {detailMenuRect.x + 10, detailMenuRect.y + 10, detailMenuRect.w - 20, detailMenuRect.h - 20}, std::max(34, TTF_FontLineSkip(bodyFont) + 12))) {
          return false;
        }
        bodyY = detailMenuRect.y + detailMenuRect.h + 16;
      }
      for (const std::string &line : viewModel.bodyLines) {
        int drawnHeight = 0;
        if (!render_text_line(renderer, bodyFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, bodyPanel.x + 16, bodyY, bodyPanel.w - 32, &drawnHeight)) {
          return false;
        }
        bodyY += drawnHeight + 8;
      }
    }

    if (!render_footer_actions(renderer, smallFont, assetCache, viewModel.footerActions, footerRect)) {
      return false;
    }

    if (viewModel.notificationVisible && !viewModel.notification.message.empty()) {
      if (!render_notification(renderer, bodyFont, smallFont, assetCache, viewModel.notification, screenWidth, footerRect.y, outerMargin)) {
        return false;
      }
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
      int modalTextHeight = 0;
      for (const std::string &line : viewModel.keypadModalLines) {
        modalTextHeight += measure_wrapped_text_height(smallFont, line, modalInnerWidth) + 6;
      }
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

      if (!render_text_line(renderer, bodyFont, viewModel.keypadModalTitle, {ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE, 0xFF}, modalRect.x + 16, modalRect.y + 16, modalRect.w - 32)) {
        return false;
      }

      int modalY = modalRect.y + 52;
      for (const std::string &line : viewModel.keypadModalLines) {
        int drawnHeight = 0;
        if (!render_text_line(renderer, smallFont, line, {TEXT_RED, TEXT_GREEN, TEXT_BLUE, 0xFF}, modalRect.x + 16, modalY, modalRect.w - 32, &drawnHeight)) {
          return false;
        }
        modalY += drawnHeight + 6;
      }

      const int buttonAreaTop = modalY + 16;
      const int buttonAreaHeight = (modalRect.y + modalRect.h) - buttonAreaTop - 24;
      const int buttonWidth = (modalRect.w - 32 - (buttonGap * (buttonColumnCount - 1))) / buttonColumnCount;
      const int buttonHeight = std::max(34, (buttonAreaHeight - (buttonGap * std::max(0, buttonRowCount - 1))) / buttonRowCount);

      for (std::size_t index = 0; index < viewModel.keypadModalButtons.size(); ++index) {
        const int row = static_cast<int>(index / viewModel.keypadModalColumnCount);
        const int column = static_cast<int>(index % viewModel.keypadModalColumnCount);
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
        if (!render_text_centered(renderer, bodyFont, button.label, buttonColor, buttonRect)) {
          return false;
        }
      }
    }

    SDL_RenderPresent(renderer);
    return true;
  }

  void close_controller(SDL_GameController *controller) {
    if (controller != nullptr) {
      SDL_GameControllerClose(controller);
    }
  }

}  // namespace

namespace ui {

  int run_shell(SDL_Window *window, const VIDEO_MODE &videoMode, app::ClientState &state, logging::Logger &logger) {
    if (window == nullptr) {
      return report_shell_failure(logger, "sdl", "Shell requires a valid SDL window");
    }

    if (TTF_Init() != 0) {
      return report_shell_failure(logger, "ttf", std::string("TTF_Init failed: ") + TTF_GetError());
    }

    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == nullptr) {
      IMG_Quit();
      TTF_Quit();
      return report_shell_failure(logger, "sdl", std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
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
      return report_shell_failure(logger, "ttf", std::string("Failed to load shell font from ") + fontPath + ": " + TTF_GetError());
    }

    SDL_Texture *titleLogoTexture = load_texture_from_asset(renderer, "moonlight-logo.svg");

    SDL_GameController *controller = nullptr;
    for (int joystickIndex = 0; joystickIndex < SDL_NumJoysticks(); ++joystickIndex) {
      if (SDL_IsGameController(joystickIndex)) {
        controller = SDL_GameControllerOpen(joystickIndex);
        if (controller != nullptr) {
          logger.log(logging::LogLevel::info, "input", "Opened primary controller");
          break;
        }
      }
    }

    bool running = true;
    bool leftTriggerPressed = false;
    bool rightTriggerPressed = false;
    bool leftShoulderPressed = false;
    bool rightShoulderPressed = false;
    bool controllerAPressed = false;
    bool controllerAContextTriggered = false;
    bool controllerStartPressed = false;
    bool controllerBackPressed = false;
    bool controllerExitComboArmed = false;
    bool controllerExitComboTriggered = false;
    Uint32 controllerADownTick = 0;
    Uint32 controllerStartDownTick = 0;
    Uint32 controllerBackDownTick = 0;
    Uint32 nextHostProbeTick = 0;
    Uint32 leftShoulderRepeatTick = 0;
    Uint32 rightShoulderRepeatTick = 0;
    Uint32 leftTriggerRepeatTick = 0;
    Uint32 rightTriggerRepeatTick = 0;
    PairingTaskState pairingTask {};
    AppListTaskState appListTask {};
    AppArtTaskState appArtTask {};
    HostProbeTaskState hostProbeTask {};
    CoverArtTextureCache coverArtTextureCache {};
    AssetTextureCache assetTextureCache {};
    reset_pairing_task(&pairingTask);
    reset_app_list_task(&appListTask);
    reset_app_art_task(&appArtTask);
    reset_host_probe_task(&hostProbeTask);
    logger.set_minimum_level(state.loggingLevel);
    logger.log(logging::LogLevel::info, "app", "Entered interactive shell");

    const auto draw_current_shell = [&]() {
      const ui::ShellViewModel viewModel = build_shell_view_model(state, logger.snapshot(logging::LogLevel::info));
      if (draw_shell(renderer, titleLogoTexture, titleFont, bodyFont, smallFont, viewModel, &coverArtTextureCache, &assetTextureCache)) {
        return true;
      }

      report_shell_failure(logger, "render", std::string("Shell render failed: ") + SDL_GetError());
      running = false;
      state.shouldExit = true;
      return false;
    };

    const auto process_command = [&](input::UiCommand command) {
      if (command == input::UiCommand::none) {
        return;
      }

      const app::AppUpdate update = app::handle_command(state, command);
      logger.set_minimum_level(state.loggingLevel);
      log_app_update(logger, state, update);
      if (update.screenChanged && !draw_current_shell()) {
        return;
      }
      show_log_file_if_requested(logger, state, update);
      cancel_pairing_if_requested(logger, state, update, &pairingTask);
      test_host_connection_if_requested(logger, state, update);
      browse_host_apps_if_requested(logger, state, update);
      pair_host_if_requested(logger, state, update, &pairingTask);
      delete_saved_file_if_requested(logger, state, update, &coverArtTextureCache);
      factory_reset_if_requested(logger, state, update, &coverArtTextureCache);
      refresh_saved_files_if_needed(logger, state);
      persist_hosts_if_needed(logger, state, update);
    };

    while (running && !state.shouldExit) {
      finish_pairing_task_if_ready(logger, state, &pairingTask);
      finish_app_list_task_if_ready(logger, state, &appListTask);
      finish_app_art_task_if_ready(logger, state, &appArtTask, &coverArtTextureCache);
      finish_host_probe_task_if_ready(logger, state, &hostProbeTask);
      refresh_saved_files_if_needed(logger, state);
      start_host_probe_task_if_needed(logger, state, &hostProbeTask, SDL_GetTicks(), &nextHostProbeTick);
      start_app_list_task_if_needed(logger, state, &appListTask, SDL_GetTicks());
      start_app_art_task_if_needed(logger, state, &appArtTask);

      if (
        !controllerExitComboTriggered && controllerStartPressed && controllerBackPressed && (state.activeScreen == app::ScreenId::home || state.activeScreen == app::ScreenId::hosts)
      ) {
        controllerExitComboArmed = true;
        const Uint32 comboStartTick = controllerStartDownTick > controllerBackDownTick ? controllerStartDownTick : controllerBackDownTick;
        if (SDL_GetTicks() - comboStartTick >= EXIT_COMBO_HOLD_MILLISECONDS) {
          controllerExitComboTriggered = true;
          state.shouldExit = true;
          logger.log(logging::LogLevel::info, "app", "Exit requested from held Start+Back on the hosts screen");
        }
      }

      if (controllerAPressed && !controllerAContextTriggered && SDL_GetTicks() - controllerADownTick >= CONTEXT_HOLD_MILLISECONDS) {
        controllerAContextTriggered = true;
        process_command(input::UiCommand::open_context_menu);
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
      while (SDL_PollEvent(&event)) {
        input::UiCommand command = input::UiCommand::none;

        switch (event.type) {
          case SDL_QUIT:
            state.shouldExit = true;
            break;
          case SDL_CONTROLLERDEVICEADDED:
            if (controller == nullptr && SDL_IsGameController(event.cdevice.which)) {
              controller = SDL_GameControllerOpen(event.cdevice.which);
              if (controller != nullptr) {
                logger.log(logging::LogLevel::info, "input", "Controller connected");
              }
            }
            break;
          case SDL_CONTROLLERDEVICEREMOVED:
            if (controller != nullptr && controller == SDL_GameControllerFromInstanceID(event.cdevice.which)) {
              close_controller(controller);
              controller = nullptr;
              leftTriggerPressed = false;
              rightTriggerPressed = false;
              leftShoulderPressed = false;
              rightShoulderPressed = false;
              controllerAPressed = false;
              controllerAContextTriggered = false;
              controllerStartPressed = false;
              controllerBackPressed = false;
              controllerExitComboArmed = false;
              controllerExitComboTriggered = false;
              logger.log(logging::LogLevel::warning, "input", "Controller disconnected");
            }
            break;
          case SDL_CONTROLLERBUTTONDOWN:
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
              if (!controllerAPressed) {
                controllerAPressed = true;
                controllerAContextTriggered = false;
                controllerADownTick = SDL_GetTicks();
              }
            } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
              if (!controllerStartPressed) {
                controllerStartPressed = true;
                controllerStartDownTick = SDL_GetTicks();
              }
            } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
              if (!controllerBackPressed) {
                controllerBackPressed = true;
                controllerBackDownTick = SDL_GetTicks();
              }
            } else {
              if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                leftShoulderPressed = true;
                leftShoulderRepeatTick = SDL_GetTicks();
              } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                rightShoulderPressed = true;
                rightShoulderRepeatTick = SDL_GetTicks();
              }
              command = translate_controller_button(event.cbutton.button);
            }
            if (
              controllerStartPressed && controllerBackPressed && (state.activeScreen == app::ScreenId::home || state.activeScreen == app::ScreenId::hosts)
            ) {
              controllerExitComboArmed = true;
            }
            break;
          case SDL_CONTROLLERBUTTONUP:
            if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A && controllerAPressed) {
              controllerAPressed = false;
              if (!controllerAContextTriggered) {
                command = input::UiCommand::activate;
              }
              controllerAContextTriggered = false;
            } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START && controllerStartPressed) {
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
            break;
          case SDL_CONTROLLERAXISMOTION:
            command = translate_trigger_axis(event.caxis, &leftTriggerPressed, &rightTriggerPressed);
            if (command == input::UiCommand::fast_previous_page) {
              leftTriggerRepeatTick = SDL_GetTicks();
            } else if (command == input::UiCommand::fast_next_page) {
              rightTriggerRepeatTick = SDL_GetTicks();
            }
            break;
          case SDL_KEYDOWN:
            if (event.key.repeat == 0) {
              command = translate_keyboard_key(event.key.keysym.sym, event.key.keysym.mod);
            }
            break;
          default:
            break;
        }

        process_command(command);
      }

      finish_pairing_task_if_ready(logger, state, &pairingTask);
      finish_app_list_task_if_ready(logger, state, &appListTask);
      finish_app_art_task_if_ready(logger, state, &appArtTask, &coverArtTextureCache);
      finish_host_probe_task_if_ready(logger, state, &hostProbeTask);
      const Uint32 backgroundTaskTick = SDL_GetTicks();
      start_host_probe_task_if_needed(logger, state, &hostProbeTask, backgroundTaskTick, &nextHostProbeTick);
      start_app_list_task_if_needed(logger, state, &appListTask, backgroundTaskTick);
      start_app_art_task_if_needed(logger, state, &appArtTask);

      const ui::ShellViewModel viewModel = build_shell_view_model(state, logger.snapshot(logging::LogLevel::info));
      if (!draw_shell(renderer, titleLogoTexture, titleFont, bodyFont, smallFont, viewModel, &coverArtTextureCache, &assetTextureCache)) {
        report_shell_failure(logger, "render", std::string("Shell render failed: ") + SDL_GetError());
        running = false;
        break;
      }

      SDL_Delay(16);
    }

    if (pairingTask.activeAttempt != nullptr) {
      pairingTask.activeAttempt->discardResult.store(true);
      finalize_pairing_attempt(logger, nullptr, std::move(pairingTask.activeAttempt));
    }
    while (!pairingTask.retiredAttempts.empty()) {
      std::unique_ptr<PairingAttemptState> attempt = std::move(pairingTask.retiredAttempts.back());
      pairingTask.retiredAttempts.pop_back();
      if (attempt != nullptr) {
        attempt->discardResult.store(true);
      }
      finalize_pairing_attempt(logger, nullptr, std::move(attempt));
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
    if (hostProbeTask.thread != nullptr) {
      int threadResult = 0;
      SDL_WaitThread(hostProbeTask.thread, &threadResult);
      (void) threadResult;
    }

    close_controller(controller);
    clear_cover_art_texture_cache(&coverArtTextureCache);
    clear_asset_texture_cache(&assetTextureCache);
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
