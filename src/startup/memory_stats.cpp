// class header include
#include "src/startup/memory_stats.h"

// nxdk includes
#include <hal/debug.h>
#include <xboxkrnl/xboxkrnl.h>

namespace startup {
  namespace {

    constexpr unsigned long long BYTES_PER_MEBIBYTE = 1024ULL * 1024ULL;

  }  // namespace

  void log_memory_statistics() {
    MM_STATISTICS memoryStatistics {};
    memoryStatistics.Length = sizeof(memoryStatistics);

    if (const NTSTATUS status = MmQueryStatistics(&memoryStatistics); !NT_SUCCESS(status)) {
      debugPrint("Failed to query memory statistics. NTSTATUS: 0x%08lx\n", static_cast<unsigned long>(status));
      return;
    }

    const unsigned long long totalPhysicalBytes = static_cast<unsigned long long>(memoryStatistics.TotalPhysicalPages) * PAGE_SIZE;
    const unsigned long long availableBytes = static_cast<unsigned long long>(memoryStatistics.AvailablePages) * PAGE_SIZE;
    const auto committedBytes = static_cast<unsigned long long>(memoryStatistics.VirtualMemoryBytesCommitted);

    debugPrint("Memory statistics:\n");
    debugPrint("Total physical memory: %llu MiB (%lu pages)\n", totalPhysicalBytes / BYTES_PER_MEBIBYTE, memoryStatistics.TotalPhysicalPages);
    debugPrint("Available memory: %llu MiB (%lu pages)\n", availableBytes / BYTES_PER_MEBIBYTE, memoryStatistics.AvailablePages);
    debugPrint("Committed virtual memory: %llu MiB\n", committedBytes / BYTES_PER_MEBIBYTE);
  }

}  // namespace startup
