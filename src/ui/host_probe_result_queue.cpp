// class header include
#include "src/ui/host_probe_result_queue.h"

// standard includes
#include <utility>

namespace ui {

  void reset_host_probe_result_queue(HostProbeResultQueue *queue) {
    if (queue == nullptr) {
      return;
    }

    const std::lock_guard<std::mutex> lock(queue->mutex);
    queue->targetCount = 0U;
    queue->publishedCount = 0U;
    queue->pendingResults.clear();
  }

  void begin_host_probe_result_round(HostProbeResultQueue *queue, std::size_t targetCount) {
    if (queue == nullptr) {
      return;
    }

    const std::lock_guard<std::mutex> lock(queue->mutex);
    queue->targetCount = targetCount;
    queue->publishedCount = 0U;
    queue->pendingResults.clear();
  }

  void publish_host_probe_result(HostProbeResultQueue *queue, HostProbeResult result) {
    if (queue == nullptr) {
      return;
    }

    const std::lock_guard<std::mutex> lock(queue->mutex);
    queue->pendingResults.push_back(std::move(result));
    ++queue->publishedCount;
  }

  void skip_host_probe_result_target(HostProbeResultQueue *queue) {
    if (queue == nullptr) {
      return;
    }

    const std::lock_guard<std::mutex> lock(queue->mutex);
    if (queue->targetCount > 0U) {
      --queue->targetCount;
    }
  }

  std::vector<HostProbeResult> drain_host_probe_results(HostProbeResultQueue *queue) {
    if (queue == nullptr) {
      return {};
    }

    const std::lock_guard<std::mutex> lock(queue->mutex);
    std::vector<HostProbeResult> results;
    results.swap(queue->pendingResults);
    return results;
  }

  bool host_probe_result_round_complete(const HostProbeResultQueue &queue) {
    const std::lock_guard<std::mutex> lock(queue.mutex);
    return queue.targetCount != 0U && queue.publishedCount >= queue.targetCount;
  }

}  // namespace ui
