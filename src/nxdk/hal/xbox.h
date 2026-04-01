#pragma once
#if defined(NXDK)
  #include <hal/xbox.h>
#else
  #include <cstdlib>

[[noreturn]] inline void XReboot() {
  std::exit(555);
}
#endif
