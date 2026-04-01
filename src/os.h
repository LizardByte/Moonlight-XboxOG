#pragma once
#if defined(NXDK)
  #define PATH_SEP "\\"
  #define DATA_PATH "D:" PATH_SEP
#else
  #define PATH_SEP "/"
  #define DATA_PATH "." PATH_SEP
#endif
