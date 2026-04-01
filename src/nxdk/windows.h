#pragma once
#if defined(NXDK)
  #include <windows.h>
#else
  #define Sleep(x) SDL_Delay(x)
#endif
