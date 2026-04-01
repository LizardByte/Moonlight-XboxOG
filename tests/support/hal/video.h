#pragma once

struct VIDEO_MODE {
  int width;
  int height;
  int bpp;
  int refresh;
};

using VideoModeEnumerationContext = void;

inline constexpr int REFRESH_DEFAULT = 0;
inline constexpr unsigned long VIDEO_WIDESCREEN = 0x010000UL;

inline bool XVideoListModes(VIDEO_MODE *, int, int, VideoModeEnumerationContext **) {  // NOSONAR(cpp:S5008): This host-only shim must remain source-compatible with nxdk's void** enumeration context.
  return false;
}

inline unsigned long XVideoGetEncoderSettings() {
  return 0UL;
}
