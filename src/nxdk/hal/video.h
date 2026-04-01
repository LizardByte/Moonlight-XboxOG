#pragma once
#if defined(NXDK)
  #include <hal/video.h>
#else
  #define XVideoSetMode(...)
  #define XVideoWaitForVBlank(...) SDL_Delay(1000 / 60 - 1)
#endif
