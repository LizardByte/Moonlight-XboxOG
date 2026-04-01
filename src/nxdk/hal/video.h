#pragma once
#if defined(NXDK)
  #include <hal/video.h>
#else
struct VIDEO_MODE {
  int width;
  int height;
  int bpp;
  int refresh;
};

inline constexpr int REFRESH_DEFAULT = 0;
inline constexpr unsigned long VIDEO_WIDESCREEN = 0x00000001UL;

inline bool XVideoListModes(VIDEO_MODE *, int, int, void **) {
  return false;
}

inline unsigned long XVideoGetEncoderSettings() {
  return 0;
}

inline void XVideoSetMode(int, int, int, int) {}

inline void XVideoWaitForVBlank() {}
#endif
