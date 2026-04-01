#pragma once
#if defined(NXDK)
  #include <windows.h>
#else
  #include <chrono>
  #include <thread>

inline void Sleep(unsigned long milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
#endif
