#pragma once

// standard includes
#include <string>
#include <vector>

namespace startup {

  /**
   * @brief Return formatted memory-statistics lines for startup diagnostics.
   *
   * @return Human-readable memory-statistics lines.
   */
  std::vector<std::string> format_memory_statistics_lines();

  /**
   * @brief Emit the current memory-statistics lines through the shared logger.
   */
  void log_memory_statistics();

}  // namespace startup
