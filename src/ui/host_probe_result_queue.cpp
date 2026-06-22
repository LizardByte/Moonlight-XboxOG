/**
 * @file src/ui/host_probe_result_queue.cpp
 * @brief Implements queued host probe results.
 */
// class header include
#include "src/ui/host_probe_result_queue.h"

// standard includes
#include <utility>

namespace ui {

  void reset_host_probe_result_queue(HostProbeResultQueue *queue) {
    if (queue == nullptr) {
      return;
    }

    const std::scoped_lock lock(queue->mutex_);
    queue->targetCount_ = 0U;
    queue->publishedCount_ = 0U;
    queue->pendingResults_.clear();
  }

  void begin_host_probe_result_round(HostProbeResultQueue *queue, std::size_t targetCount) {
    if (queue == nullptr) {
      return;
    }

    const std::scoped_lock lock(queue->mutex_);
    queue->targetCount_ = targetCount;
    queue->publishedCount_ = 0U;
    queue->pendingResults_.clear();
  }

  void publish_host_probe_result(HostProbeResultQueue *queue, HostProbeResult result) {
    if (queue == nullptr) {
      return;
    }

    const std::scoped_lock lock(queue->mutex_);
    queue->pendingResults_.push_back(std::move(result));
    ++queue->publishedCount_;
  }

  void skip_host_probe_result_target(HostProbeResultQueue *queue) {
    if (queue == nullptr) {
      return;
    }

    const std::scoped_lock lock(queue->mutex_);
    if (queue->targetCount_ > 0U) {
      --queue->targetCount_;
    }
  }

  std::vector<HostProbeResult> drain_host_probe_results(HostProbeResultQueue *queue) {
    if (queue == nullptr) {
      return {};
    }

    const std::scoped_lock lock(queue->mutex_);
    std::vector<HostProbeResult> results;
    results.swap(queue->pendingResults_);
    return results;
  }

  bool host_probe_result_round_complete(const HostProbeResultQueue &queue) {
    const std::scoped_lock lock(queue.mutex_);
    return queue.targetCount_ != 0U && queue.publishedCount_ >= queue.targetCount_;
  }

}  // namespace ui
