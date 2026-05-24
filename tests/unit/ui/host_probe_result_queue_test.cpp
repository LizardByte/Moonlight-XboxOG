/**
 * @file tests/unit/ui/host_probe_result_queue_test.cpp
 * @brief Verifies queued host probe results.
 */
// test header include
#include "src/ui/host_probe_result_queue.h"

// standard includes
#include <algorithm>
#include <thread>
#include <vector>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/network_test_constants.h"

namespace {

  network::HostPairingServerInfo make_probe_server_info(std::string_view hostName) {
    network::HostPairingServerInfo serverInfo {};
    serverInfo.httpPort = test_support::kTestPorts[test_support::kPortResolvedHttp];
    serverInfo.httpsPort = test_support::kTestPorts[test_support::kPortResolvedHttps];
    serverInfo.hostName = hostName;
    return serverInfo;
  }

  TEST(HostProbeResultQueueTest, DrainsPublishedResultsBeforeTheRoundCompletes) {
    ui::HostProbeResultQueue queue {};
    ui::begin_host_probe_result_round(&queue, 3U);

    ui::publish_host_probe_result(&queue, {
                                            test_support::kTestIpv4Addresses[test_support::kIpHostGridA],
                                            test_support::kTestPorts[test_support::kPortDefaultHost],
                                            true,
                                            make_probe_server_info("Host A"),
                                          });

    std::vector<ui::HostProbeResult> drainedResults = ui::drain_host_probe_results(&queue);
    ASSERT_EQ(drainedResults.size(), 1U);
    EXPECT_EQ(drainedResults.front().address, test_support::kTestIpv4Addresses[test_support::kIpHostGridA]);
    EXPECT_TRUE(drainedResults.front().success);
    EXPECT_FALSE(ui::host_probe_result_round_complete(queue));

    ui::publish_host_probe_result(&queue, {
                                            test_support::kTestIpv4Addresses[test_support::kIpHostGridB],
                                            test_support::kTestPorts[test_support::kPortDefaultHost],
                                            false,
                                            {},
                                          });
    ui::publish_host_probe_result(&queue, {
                                            test_support::kTestIpv4Addresses[test_support::kIpHostGridC],
                                            test_support::kTestPorts[test_support::kPortDefaultHost],
                                            true,
                                            make_probe_server_info("Host C"),
                                          });

    drainedResults = ui::drain_host_probe_results(&queue);
    ASSERT_EQ(drainedResults.size(), 2U);
    EXPECT_TRUE(ui::host_probe_result_round_complete(queue));
  }

  TEST(HostProbeResultQueueTest, SkipsExpectedTargetsThatNeverLaunchAWorker) {
    ui::HostProbeResultQueue queue {};
    ui::begin_host_probe_result_round(&queue, 2U);

    ui::skip_host_probe_result_target(&queue);
    EXPECT_FALSE(ui::host_probe_result_round_complete(queue));

    ui::publish_host_probe_result(&queue, {
                                            test_support::kTestIpv4Addresses[test_support::kIpOffice],
                                            test_support::kTestPorts[test_support::kPortDefaultHost],
                                            true,
                                            make_probe_server_info("Office PC"),
                                          });

    const std::vector<ui::HostProbeResult> drainedResults = ui::drain_host_probe_results(&queue);
    ASSERT_EQ(drainedResults.size(), 1U);
    EXPECT_TRUE(ui::host_probe_result_round_complete(queue));
  }

  TEST(HostProbeResultQueueTest, AcceptsConcurrentProbePublications) {
    ui::HostProbeResultQueue queue {};
    const std::vector<std::string> addresses = {
      test_support::kTestIpv4Addresses[test_support::kIpHostGridA],
      test_support::kTestIpv4Addresses[test_support::kIpHostGridB],
      test_support::kTestIpv4Addresses[test_support::kIpHostGridC],
      test_support::kTestIpv4Addresses[test_support::kIpHostGridD],
    };
    ui::begin_host_probe_result_round(&queue, addresses.size());

    std::vector<std::thread> workers;
    workers.reserve(addresses.size());
    for (std::size_t index = 0; index < addresses.size(); ++index) {
      workers.emplace_back([&queue, &addresses, index]() {
        ui::publish_host_probe_result(&queue, {
                                                addresses[index],
                                                test_support::kTestPorts[test_support::kPortDefaultHost],
                                                (index % 2U) == 0U,
                                                {},
                                              });
      });
    }
    for (std::thread &worker : workers) {
      worker.join();
    }

    const std::vector<ui::HostProbeResult> drainedResults = ui::drain_host_probe_results(&queue);
    ASSERT_EQ(drainedResults.size(), addresses.size());
    EXPECT_TRUE(ui::host_probe_result_round_complete(queue));
    for (const std::string &address : addresses) {
      EXPECT_TRUE(std::any_of(drainedResults.begin(), drainedResults.end(), [&address](const ui::HostProbeResult &result) {
        return result.address == address;
      }));
    }
  }

}  // namespace
