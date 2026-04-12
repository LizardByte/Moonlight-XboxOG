// class header include
#include "src/startup/memory_stats.h"

// local includes
#include "src/logging/global_logger.h"

// nxdk includes
#include <xboxkrnl/xboxkrnl.h>

namespace startup {
  namespace {

    constexpr unsigned long long BYTES_PER_MEBIBYTE = 1024ULL * 1024ULL;

  }  // namespace

  std::vector<std::string> format_memory_statistics_lines() {
    MM_STATISTICS memoryStatistics {};
    memoryStatistics.Length = sizeof(memoryStatistics);

    if (const NTSTATUS status = MmQueryStatistics(&memoryStatistics); !NT_SUCCESS(status)) {
      return {
        "Failed to query memory statistics. NTSTATUS: 0x" + std::to_string(static_cast<unsigned long>(status)),
      };
    }

    const unsigned long long totalPhysicalBytes = static_cast<unsigned long long>(memoryStatistics.TotalPhysicalPages) * PAGE_SIZE;
    const unsigned long long availableBytes = static_cast<unsigned long long>(memoryStatistics.AvailablePages) * PAGE_SIZE;
    const auto committedBytes = static_cast<unsigned long long>(memoryStatistics.VirtualMemoryBytesCommitted);

    return {
      "Memory statistics:",
      "Total physical memory: " + std::to_string(totalPhysicalBytes / BYTES_PER_MEBIBYTE) + " MiB (" + std::to_string(memoryStatistics.TotalPhysicalPages) + " pages)",
      "Available memory: " + std::to_string(availableBytes / BYTES_PER_MEBIBYTE) + " MiB (" + std::to_string(memoryStatistics.AvailablePages) + " pages)",
      "Committed virtual memory: " + std::to_string(committedBytes / BYTES_PER_MEBIBYTE) + " MiB",
    };
  }

  void log_memory_statistics() {
    for (const std::string &line : format_memory_statistics_lines()) {
      logging::logger.info("memory", line);
    }
  }

}  // namespace startup
