#pragma once

// standard includes
#include <string>
#include <vector>

namespace startup {

  std::vector<std::string> format_memory_statistics_lines();

  void log_memory_statistics();

}  // namespace startup
