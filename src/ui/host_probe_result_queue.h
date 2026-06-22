/**
 * @file src/ui/host_probe_result_queue.h
 * @brief Declares queued host probe results.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// local includes
#include "src/network/host_pairing.h"

namespace ui {

  /**
   * @brief One completed saved-host reachability probe.
   */
  struct HostProbeResult {
    std::string address;  ///< Saved host address that was probed.
    uint16_t port = 0;  ///< Effective host port used for the probe.
    bool success = false;  ///< True when the host responded to the probe.
    network::HostPairingServerInfo serverInfo;  ///< Parsed host status returned by the probe when available.
  };

  /**
   * @brief Thread-safe queue used to publish per-host probe results back to the shell loop.
   */
  class HostProbeResultQueue {
  public:
    /**
     * @brief Construct an empty probe result queue.
     */
    HostProbeResultQueue() = default;

  private:
    friend void reset_host_probe_result_queue(HostProbeResultQueue *queue);
    friend void begin_host_probe_result_round(HostProbeResultQueue *queue, std::size_t targetCount);
    friend void publish_host_probe_result(HostProbeResultQueue *queue, HostProbeResult result);
    friend void skip_host_probe_result_target(HostProbeResultQueue *queue);
    friend std::vector<HostProbeResult> drain_host_probe_results(HostProbeResultQueue *queue);
    friend bool host_probe_result_round_complete(const HostProbeResultQueue &queue);

    mutable std::mutex mutex_;  ///< Guards the current round counters and pending result queue.
    std::size_t targetCount_ = 0U;  ///< Number of results expected for the active probe round.
    std::size_t publishedCount_ = 0U;  ///< Number of results published so far for the active round.
    std::vector<HostProbeResult> pendingResults_;  ///< Probe results waiting to be drained by the main thread.
  };

  /**
   * @brief Reset the queued probe results and counters for a queue.
   *
   * @param queue Queue to clear.
   */
  void reset_host_probe_result_queue(HostProbeResultQueue *queue);

  /**
   * @brief Start a fresh probe round with a known number of expected results.
   *
   * @param queue Queue that will receive probe results.
   * @param targetCount Number of probe results expected for the new round.
   */
  void begin_host_probe_result_round(HostProbeResultQueue *queue, std::size_t targetCount);

  /**
   * @brief Publish one completed host probe into the queue.
   *
   * @param queue Queue that receives the probe result.
   * @param result Completed host probe to append.
   */
  void publish_host_probe_result(HostProbeResultQueue *queue, HostProbeResult result);

  /**
   * @brief Remove one expected result from the active probe round.
   *
   * Use this when a planned probe worker could not be launched and therefore will
   * never publish a result.
   *
   * @param queue Queue whose expected result count should be reduced.
   */
  void skip_host_probe_result_target(HostProbeResultQueue *queue);

  /**
   * @brief Drain every probe result currently waiting in the queue.
   *
   * @param queue Queue whose pending results should be removed.
   * @return Completed probe results published since the previous drain.
   */
  std::vector<HostProbeResult> drain_host_probe_results(HostProbeResultQueue *queue);

  /**
   * @brief Return whether the active probe round has published every expected result.
   *
   * @param queue Queue to inspect.
   * @return True when the current round has received every expected result.
   */
  bool host_probe_result_round_complete(const HostProbeResultQueue &queue);

}  // namespace ui
