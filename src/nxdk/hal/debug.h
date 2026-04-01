#pragma once
#if defined(NXDK)
  #include <hal/debug.h>
#else
  #include <cstdio>

  #define debugPrint(...) std::printf(__VA_ARGS__)
#endif
