#pragma once
#if defined(NXDK)
  #include <hal/xbox.h>
#else
  #define XReboot(...) exit(555)
#endif
