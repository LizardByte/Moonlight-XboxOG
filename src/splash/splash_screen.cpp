// class header include
#include "src/splash/splash_screen.h"

// standard includes
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// nxdk includes
#include <hal/debug.h>
#include <hal/xbox.h>
#include <SDL.h>
#include <SDL_image.h>
#include <windows.h>

// local includes
#include "src/os.h"
#include "src/splash/splash_layout.h"

namespace {

  constexpr Uint8 SPLASH_BACKGROUND_RED = 0x56;
  constexpr Uint8 SPLASH_BACKGROUND_GREEN = 0x5C;
  constexpr Uint8 SPLASH_BACKGROUND_BLUE = 0x64;

  void printSDLErrorAndReboot() {
    debugPrint("SDL_Error: %s\n", SDL_GetError());
    debugPrint("Rebooting in 5 seconds.\n");
    Sleep(5000);
    XReboot();
  }

  void printIMGErrorAndReboot() {
    debugPrint("SDL_Image Error: %s\n", IMG_GetError());
    debugPrint("Rebooting in 5 seconds.\n");
    Sleep(5000);
    XReboot();
  }

  std::string buildAssetPath(const char *assetName) {
    return std::string(DATA_PATH) + "assets" + PATH_SEP + assetName;
  }

  SDL_Rect createCenteredRect(const SDL_Surface *screenSurface, int width, int height) {
    SDL_Rect destination {};
    if (screenSurface == nullptr) {
      return destination;
    }

    destination.w = width;
    destination.h = height;
    destination.x = (screenSurface->w - destination.w) / 2;
    destination.y = (screenSurface->h - destination.h) / 2;
    return destination;
  }

  SDL_Rect calculateLogoDestination(const SDL_Surface *screenSurface, int logoWidth, int logoHeight, const VIDEO_MODE &videoMode) {
    if (screenSurface == nullptr) {
      return {};
    }

    const splash::SplashLayout layout = splash::calculate_logo_destination(
      screenSurface->w,
      screenSurface->h,
      logoWidth,
      logoHeight,
      videoMode,
      XVideoGetEncoderSettings()
    );

    SDL_Rect destination {};
    destination.x = layout.x;
    destination.y = layout.y;
    destination.w = layout.width;
    destination.h = layout.height;
    return destination;
  }

  Uint32 readSurfacePixel(const SDL_Surface *surface, int x, int y) {
    const Uint8 *row = static_cast<const Uint8 *>(surface->pixels) + (y * surface->pitch);
    Uint32 pixel;
    std::memcpy(&pixel, row + x * static_cast<int>(sizeof(Uint32)), sizeof(Uint32));
    return pixel;
  }

  void writeSurfacePixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {
    Uint8 *row = static_cast<Uint8 *>(surface->pixels) + (y * surface->pitch);
    std::memcpy(row + x * static_cast<int>(sizeof(Uint32)), &pixel, sizeof(Uint32));
  }

  Uint32 sampleBilinearPixel(const SDL_Surface *sourceSurface, float sourceX, float sourceY, const SDL_PixelFormat *targetFormat) {
    const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, sourceSurface->w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, sourceSurface->h - 1);
    const int x1 = std::min(x0 + 1, sourceSurface->w - 1);
    const int y1 = std::min(y0 + 1, sourceSurface->h - 1);
    const float tx = std::clamp(sourceX - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = std::clamp(sourceY - static_cast<float>(y0), 0.0f, 1.0f);

    Uint8 topLeftRed;
    Uint8 topLeftGreen;
    Uint8 topLeftBlue;
    Uint8 topLeftAlpha;
    Uint8 topRightRed;
    Uint8 topRightGreen;
    Uint8 topRightBlue;
    Uint8 topRightAlpha;
    Uint8 bottomLeftRed;
    Uint8 bottomLeftGreen;
    Uint8 bottomLeftBlue;
    Uint8 bottomLeftAlpha;
    Uint8 bottomRightRed;
    Uint8 bottomRightGreen;
    Uint8 bottomRightBlue;
    Uint8 bottomRightAlpha;

    SDL_GetRGBA(readSurfacePixel(sourceSurface, x0, y0), sourceSurface->format, &topLeftRed, &topLeftGreen, &topLeftBlue, &topLeftAlpha);
    SDL_GetRGBA(readSurfacePixel(sourceSurface, x1, y0), sourceSurface->format, &topRightRed, &topRightGreen, &topRightBlue, &topRightAlpha);
    SDL_GetRGBA(readSurfacePixel(sourceSurface, x0, y1), sourceSurface->format, &bottomLeftRed, &bottomLeftGreen, &bottomLeftBlue, &bottomLeftAlpha);
    SDL_GetRGBA(readSurfacePixel(sourceSurface, x1, y1), sourceSurface->format, &bottomRightRed, &bottomRightGreen, &bottomRightBlue, &bottomRightAlpha);

    const float topRed = (static_cast<float>(topLeftRed) * (1.0f - tx)) + (static_cast<float>(topRightRed) * tx);
    const float topGreen = (static_cast<float>(topLeftGreen) * (1.0f - tx)) + (static_cast<float>(topRightGreen) * tx);
    const float topBlue = (static_cast<float>(topLeftBlue) * (1.0f - tx)) + (static_cast<float>(topRightBlue) * tx);
    const float topAlpha = (static_cast<float>(topLeftAlpha) * (1.0f - tx)) + (static_cast<float>(topRightAlpha) * tx);
    const float bottomRed = (static_cast<float>(bottomLeftRed) * (1.0f - tx)) + (static_cast<float>(bottomRightRed) * tx);
    const float bottomGreen = (static_cast<float>(bottomLeftGreen) * (1.0f - tx)) + (static_cast<float>(bottomRightGreen) * tx);
    const float bottomBlue = (static_cast<float>(bottomLeftBlue) * (1.0f - tx)) + (static_cast<float>(bottomRightBlue) * tx);
    const float bottomAlpha = (static_cast<float>(bottomLeftAlpha) * (1.0f - tx)) + (static_cast<float>(bottomRightAlpha) * tx);

    const auto red = static_cast<Uint8>((topRed * (1.0f - ty)) + (bottomRed * ty));
    const auto green = static_cast<Uint8>((topGreen * (1.0f - ty)) + (bottomGreen * ty));
    const auto blue = static_cast<Uint8>((topBlue * (1.0f - ty)) + (bottomBlue * ty));
    const auto alpha = static_cast<Uint8>((topAlpha * (1.0f - ty)) + (bottomAlpha * ty));
    return SDL_MapRGBA(targetFormat, red, green, blue, alpha);
  }

  SDL_Surface *createScaledSplashLogoSurface(const SDL_Surface *screenSurface, SDL_Surface *sourceSurface, const VIDEO_MODE &videoMode) {
    const SDL_Rect targetDestination = calculateLogoDestination(screenSurface, sourceSurface->w, sourceSurface->h, videoMode);
    SDL_Surface *scaledSurface = SDL_CreateRGBSurfaceWithFormat(0, targetDestination.w, targetDestination.h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (scaledSurface == nullptr) {
      debugPrint("Failed to create scaled splash asset surface: %s\n", SDL_GetError());
      SDL_FreeSurface(sourceSurface);
      return nullptr;
    }

    if (SDL_LockSurface(sourceSurface) < 0) {
      debugPrint("Failed to lock source splash asset surface: %s\n", SDL_GetError());
      SDL_FreeSurface(sourceSurface);
      SDL_FreeSurface(scaledSurface);
      return nullptr;
    }

    if (SDL_LockSurface(scaledSurface) < 0) {
      debugPrint("Failed to lock scaled splash asset surface: %s\n", SDL_GetError());
      SDL_UnlockSurface(sourceSurface);
      SDL_FreeSurface(sourceSurface);
      SDL_FreeSurface(scaledSurface);
      return nullptr;
    }

    for (int y = 0; y < scaledSurface->h; ++y) {
      const float sourceY = ((static_cast<float>(y) + 0.5f) * static_cast<float>(sourceSurface->h) / static_cast<float>(scaledSurface->h)) - 0.5f;
      for (int x = 0; x < scaledSurface->w; ++x) {
        const float sourceX = ((static_cast<float>(x) + 0.5f) * static_cast<float>(sourceSurface->w) / static_cast<float>(scaledSurface->w)) - 0.5f;
        writeSurfacePixel(scaledSurface, x, y, sampleBilinearPixel(sourceSurface, sourceX, sourceY, scaledSurface->format));
      }
    }

    SDL_UnlockSurface(scaledSurface);
    SDL_UnlockSurface(sourceSurface);
    SDL_FreeSurface(sourceSurface);

    if (SDL_SetSurfaceBlendMode(scaledSurface, SDL_BLENDMODE_BLEND) < 0) {
      debugPrint("Failed to enable alpha blending for scaled splash asset: %s\n", SDL_GetError());
      SDL_FreeSurface(scaledSurface);
      return nullptr;
    }

    return scaledSurface;
  }

  SDL_Surface *normalizeSplashLogoSurface(SDL_Surface *surface) {
    if (surface == nullptr) {
      return nullptr;
    }

    SDL_Surface *normalizedSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
    if (normalizedSurface == nullptr) {
      debugPrint("Failed to normalize splash asset surface format %s: %s\n", SDL_GetPixelFormatName(surface->format->format), SDL_GetError());
      SDL_FreeSurface(surface);
      return nullptr;
    }

    SDL_FreeSurface(surface);

    if (SDL_SetSurfaceBlendMode(normalizedSurface, SDL_BLENDMODE_BLEND) < 0) {
      debugPrint("Failed to enable alpha blending for splash asset: %s\n", SDL_GetError());
      SDL_FreeSurface(normalizedSurface);
      return nullptr;
    }

    return normalizedSurface;
  }

  SDL_Surface *loadSplashLogoSurface() {
    const std::string assetPath = buildAssetPath("moonlight-logo-wedges.svg");
    if (SDL_Surface *loadedSurface = IMG_Load(assetPath.c_str()); loadedSurface != nullptr) {
      if (SDL_Surface *normalizedSurface = normalizeSplashLogoSurface(loadedSurface); normalizedSurface != nullptr) {
        return normalizedSurface;
      }

      debugPrint("Failed to prepare splash asset %s for rendering.\n", assetPath.c_str());
    }

    debugPrint("Failed to load splash asset %s: %s\n", assetPath.c_str(), IMG_GetError());

    return nullptr;
  }

  void cleanupSplashScreen(SDL_Surface *imageSurface) {
    if (imageSurface != nullptr) {
      SDL_FreeSurface(imageSurface);
    }

    IMG_Quit();
  }

  void runSplashScreen(SDL_Window *window, const VIDEO_MODE &videoMode, const std::function<bool()> &keepShowing) {
    int done = 0;
    const int imageInitFlags = IMG_INIT_JPG | IMG_INIT_PNG;
    const int initializedImageFlags = IMG_Init(imageInitFlags);
    SDL_Event event;
    SDL_Surface *screenSurface = nullptr;
    SDL_Surface *imageSurface = nullptr;

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    if (window == nullptr) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Splash screen requires a valid SDL window.\n");
      printSDLErrorAndReboot();
      return;
    }

    (void) initializedImageFlags;

    screenSurface = SDL_GetWindowSurface(window);
    if (!screenSurface) {
      cleanupSplashScreen(nullptr);
      printSDLErrorAndReboot();
      return;
    }

    imageSurface = loadSplashLogoSurface();
    if (!imageSurface) {
      cleanupSplashScreen(nullptr);
      printIMGErrorAndReboot();
      return;
    }

    imageSurface = createScaledSplashLogoSurface(screenSurface, imageSurface, videoMode);
    if (!imageSurface) {
      cleanupSplashScreen(nullptr);
      printSDLErrorAndReboot();
      return;
    }

    SDL_Rect logoDestination = createCenteredRect(screenSurface, imageSurface->w, imageSurface->h);

    while (!done) {
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
          done = 1;
        }
      }

      if (const Uint32 backgroundColor = SDL_MapRGB(screenSurface->format, SPLASH_BACKGROUND_RED, SPLASH_BACKGROUND_GREEN, SPLASH_BACKGROUND_BLUE); SDL_FillRect(screenSurface, nullptr, backgroundColor) < 0) {
        cleanupSplashScreen(imageSurface);
        printSDLErrorAndReboot();
        return;
      }

      if (SDL_BlitSurface(imageSurface, nullptr, screenSurface, &logoDestination) < 0) {
        cleanupSplashScreen(imageSurface);
        printSDLErrorAndReboot();
        return;
      }

      if (SDL_UpdateWindowSurface(window) < 0) {
        cleanupSplashScreen(imageSurface);
        printSDLErrorAndReboot();
        return;
      }

      if (!keepShowing()) {
        done = 1;
      }

      SDL_Delay(16);
    }

    cleanupSplashScreen(imageSurface);
  }

}  // namespace

namespace splash {

  void show_splash_screen(SDL_Window *window, const VIDEO_MODE &videoMode, unsigned int durationMilliseconds) {
    const Uint32 startTicks = SDL_GetTicks();
    runSplashScreen(window, videoMode, [startTicks, durationMilliseconds]() {
      return SDL_GetTicks() - startTicks < durationMilliseconds;
    });
  }

  void show_splash_screen(SDL_Window *window, const VIDEO_MODE &videoMode, const std::function<bool()> &keepShowing) {
    runSplashScreen(window, videoMode, keepShowing);
  }

}  // namespace splash
