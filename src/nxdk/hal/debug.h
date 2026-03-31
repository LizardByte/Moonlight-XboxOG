#pragma once
#if defined(NXDK)
  #include <hal/debug.h>
#else
  #define debugPrint(...) printf(__VA_ARGS__)
#endif
