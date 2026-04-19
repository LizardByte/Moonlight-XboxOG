/**
 * @file src/network/host_discovery.cpp
 * @brief Implements host auto-discovery helpers.
 */
// class header include
#include "src/network/host_discovery.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// local includes
#include "src/app/host_records.h"
#include "src/logging/logger.h"
#include "src/network/host_pairing.h"
#include "src/network/runtime_network.h"

#ifdef NXDK
  // nxdk includes
  #include <lwip/apps/mdns.h>
  #include <lwip/apps/mdns_domain.h>
  #include <lwip/err.h>
  #include <lwip/igmp.h>
  #include <lwip/netif.h>
  #include <lwip/prot/dns.h>
  #include <lwip/tcpip.h>

  // platform includes
  #include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names

/**
 * @brief nxdk-provided pointer to the active lwIP network interface.
 */
extern "C" struct netif *g_pnetif;  // NOSONAR(cpp:S5421) external symbol declared by nxdk; cannot be const
#endif

namespace {

  constexpr uint32_t DEFAULT_DISCOVERY_TIMEOUT_MILLISECONDS = 1500U;

#ifdef NXDK
  constexpr char kXemuUserModeGatewayAddress[] = "10.0.2.2";
  constexpr char kMdnsDiscoveryHostName[] = "moonlight-xbox";
#endif

  network::testing::HostDiscoveryTestHandler &host_discovery_test_handler() {
    static network::testing::HostDiscoveryTestHandler handler;  ///< Optional scripted discovery handler used by host-native unit tests.
    return handler;
  }

  std::string trim_ascii_whitespace(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
      text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
      text.remove_suffix(1);
    }
    return std::string(text);
  }

  std::string first_dns_label(std::string_view domainText) {
    const std::size_t separator = domainText.find('.');
    return trim_ascii_whitespace(domainText.substr(0, separator));
  }

  /**
   * @brief Discovery mechanisms used by the NXDK host search flow.
   */
  enum class HostDiscoveryMethod {
    mdns,
    directGatewayProbe,
  };

  /**
   * @brief Return a stable log label for one discovery mechanism.
   *
   * @param method Discovery mechanism to describe.
   * @return Lowercase text suitable for runtime logs.
   */
  std::string_view host_discovery_method_name(HostDiscoveryMethod method) {
    switch (method) {
      case HostDiscoveryMethod::mdns:
        return "mDNS";
      case HostDiscoveryMethod::directGatewayProbe:
        return "direct gateway check";
      default:
        return "unknown";
    }
  }

  /**
   * @brief Format one host endpoint for discovery diagnostics.
   *
   * @param address Canonical IPv4 address for the host.
   * @param port Host HTTP port, or zero for the default port.
   * @return Human-readable endpoint text.
   */
  std::string format_discovered_host_endpoint(const std::string &address, uint16_t port) {
    const uint16_t effectivePort = app::effective_host_port(port);
    return address + ":" + std::to_string(effectivePort);
  }

  /**
   * @brief Append one discovered host when its endpoint is not already present.
   *
   * @param result Discovery result to update.
   * @param displayName User-facing label for the discovered host.
   * @param address Reachable IPv4 address for the host.
   * @param port Host HTTP port, or zero for the default Moonlight port.
   * @param method Discovery mechanism that found the host.
   * @return True when a new host was appended.
   */
  bool append_discovered_host(network::DiscoverHostsResult *result, std::string displayName, const std::string &address, uint16_t port, HostDiscoveryMethod method) {
    if (result == nullptr) {
      return false;
    }

    const std::string normalizedAddress = app::normalize_ipv4_address(address);
    if (normalizedAddress.empty()) {
      return false;
    }

    const uint16_t storedPort = app::effective_host_port(port) == app::DEFAULT_HOST_PORT ? 0 : port;
    if (const auto duplicate = std::find_if(result->hosts.begin(), result->hosts.end(), [&](const network::DiscoveredHost &host) {
          return host.address == normalizedAddress && app::effective_host_port(host.port) == app::effective_host_port(storedPort);
        });
        duplicate != result->hosts.end()) {
      logging::debug(
        "hosts",
        "Ignoring duplicate host from " + std::string(host_discovery_method_name(method)) + ": " + format_discovered_host_endpoint(normalizedAddress, storedPort)
      );
      if (duplicate->displayName.empty() && !displayName.empty()) {
        duplicate->displayName = std::move(displayName);
      }
      return false;
    }

    if (displayName.empty()) {
      displayName = "Host " + normalizedAddress;
    }

    result->hosts.push_back({
      std::move(displayName),
      normalizedAddress,
      storedPort,
    });
    const network::DiscoveredHost &host = result->hosts.back();
    logging::debug(
      "hosts",
      "Discovered host via " + std::string(host_discovery_method_name(method)) + ": " + host.displayName + " (" + format_discovered_host_endpoint(host.address, host.port) + ')'
    );
    return true;
  }

#ifdef NXDK

  constexpr char kGameStreamServiceType[] = "_nvstream";

  /**
   * @brief Cached status of the one-time lwIP mDNS bootstrap used by discovery.
   */
  enum class MdnsDiscoveryBootstrapState {
    uninitialized,
    ready,
    failed,
  };

  MdnsDiscoveryBootstrapState g_mdnsBootstrapState = MdnsDiscoveryBootstrapState::uninitialized;  // NOSONAR(cpp:S5421) one-time NXDK runtime state used to avoid repeated mdns setup attempts
  err_t g_mdnsBootstrapError = ERR_OK;  // NOSONAR(cpp:S5421) cached lwIP error from the one-time NXDK mDNS bootstrap

  /**
   * @brief Discovery-side aggregation of one service instance while mDNS answers arrive.
   */
  struct PendingDiscoveredService {
    std::string instanceDomain;  ///< Full service-instance domain reported by PTR/SRV records.
    std::string displayName;  ///< Friendly host label derived from the service instance name.
    std::string targetDomain;  ///< DNS host target reported by the SRV record.
    uint16_t port = 0;  ///< Host port reported by the SRV record.
  };

  /**
   * @brief Thread-safe collector updated by the lwIP mDNS search callback.
   */
  struct DiscoveryCollector {
    mutable std::mutex mutex;  ///< Guards the partial discovery state shared with the callback.
    std::unordered_map<std::string, PendingDiscoveredService> servicesByInstance;  ///< Partial records keyed by instance domain.
    std::unordered_map<std::string, std::string> ipv4ByDomain;  ///< Resolved IPv4 addresses keyed by target host domain.
  };

  std::string encoded_domain_to_string(const char *encodedDomain, std::size_t encodedLength) {
    if (encodedDomain == nullptr || encodedLength == 0U) {
      return {};
    }

    std::string domainText;
    std::size_t offset = 0U;
    while (offset < encodedLength) {
      const auto labelLength = static_cast<std::size_t>(static_cast<unsigned char>(encodedDomain[offset]));
      if (labelLength == 0U) {
        break;
      }
      ++offset;
      if (labelLength > encodedLength - offset) {
        return {};
      }
      if (!domainText.empty()) {
        domainText.push_back('.');
      }
      domainText.append(encodedDomain + offset, labelLength);
      offset += labelLength;
    }

    return domainText;
  }

  std::string mdns_domain_to_string(const mdns_domain &domain) {
    return encoded_domain_to_string(reinterpret_cast<const char *>(domain.name), domain.length);
  }

  std::string ipv4_address_from_rdata(const char *rdata, int rdataLength) {
    if (rdata == nullptr || rdataLength != 4) {
      return {};
    }

    return std::to_string(static_cast<unsigned int>(static_cast<unsigned char>(rdata[0]))) + '.' + std::to_string(static_cast<unsigned int>(static_cast<unsigned char>(rdata[1]))) + '.' +
           std::to_string(static_cast<unsigned int>(static_cast<unsigned char>(rdata[2]))) + '.' + std::to_string(static_cast<unsigned int>(static_cast<unsigned char>(rdata[3])));
  }

  uint16_t read_u16_be(const char *bytes) {
    return static_cast<uint16_t>((static_cast<unsigned int>(static_cast<unsigned char>(bytes[0])) << 8U) | static_cast<unsigned int>(static_cast<unsigned char>(bytes[1])));
  }

  void remember_ptr_service_instance(DiscoveryCollector *collector, std::string instanceDomain) {
    if (collector == nullptr || instanceDomain.empty()) {
      return;
    }

    const std::scoped_lock lock(collector->mutex);
    PendingDiscoveredService &service = collector->servicesByInstance[instanceDomain];
    service.instanceDomain = std::move(instanceDomain);
    if (service.displayName.empty()) {
      service.displayName = first_dns_label(service.instanceDomain);
    }
  }

  void remember_srv_service_target(DiscoveryCollector *collector, std::string instanceDomain, std::string targetDomain, uint16_t port) {
    if (collector == nullptr || instanceDomain.empty() || targetDomain.empty() || port == 0) {
      return;
    }

    const std::scoped_lock lock(collector->mutex);
    PendingDiscoveredService &service = collector->servicesByInstance[instanceDomain];
    service.instanceDomain = std::move(instanceDomain);
    if (service.displayName.empty()) {
      service.displayName = first_dns_label(service.instanceDomain);
    }
    service.targetDomain = std::move(targetDomain);
    service.port = port;
  }

  void remember_a_record(DiscoveryCollector *collector, std::string domain, std::string ipv4Address) {
    if (collector == nullptr || domain.empty() || ipv4Address.empty()) {
      return;
    }

    const std::scoped_lock lock(collector->mutex);
    collector->ipv4ByDomain[std::move(domain)] = std::move(ipv4Address);
  }

  /**
   * @brief Consume one lwIP mDNS answer and collect enough data to resolve hosts.
   *
   * @param answer Parsed mDNS answer supplied by lwIP.
   * @param varpart Variable-length answer payload supplied by lwIP.
   * @param varlen Length of @p varpart in bytes.
   * @param flags Frame-boundary flags supplied by lwIP.
   * @param arg Pointer to the shared discovery collector.
   */
  void collect_mdns_search_result(struct mdns_answer *answer, const char *varpart, int varlen, int flags, void *arg) {  // NOSONAR(cpp:S5008) lwIP callback signature is fixed
    (void) flags;
    auto *collector = static_cast<DiscoveryCollector *>(arg);
    if (collector == nullptr || answer == nullptr) {
      return;
    }

    switch (answer->info.type) {
      case DNS_RRTYPE_PTR:
        {
          const std::string instanceDomain = encoded_domain_to_string(varpart, static_cast<std::size_t>(std::max(varlen, 0)));
          remember_ptr_service_instance(collector, instanceDomain);
          return;
        }
      case DNS_RRTYPE_SRV:
        {
          if (varpart == nullptr || varlen < 7) {
            return;
          }
          const std::string instanceDomain = mdns_domain_to_string(answer->info.domain);
          const std::string targetDomain = encoded_domain_to_string(varpart + 6, static_cast<std::size_t>(varlen - 6));
          remember_srv_service_target(collector, instanceDomain, targetDomain, read_u16_be(varpart + 4));
          return;
        }
      case DNS_RRTYPE_A:
        {
          const std::string domain = mdns_domain_to_string(answer->info.domain);
          const std::string ipv4Address = ipv4_address_from_rdata(varpart, varlen);
          remember_a_record(collector, domain, ipv4Address);
          return;
        }
      default:
        return;
    }
  }

  network::DiscoverHostsResult build_discover_hosts_result(const DiscoveryCollector &collector) {
    network::DiscoverHostsResult result {};

    const std::scoped_lock lock(collector.mutex);
    for (const auto &[instanceDomain, service] : collector.servicesByInstance) {
      (void) instanceDomain;
      if (service.targetDomain.empty() || service.port == 0) {
        continue;
      }
      const auto addressIterator = collector.ipv4ByDomain.find(service.targetDomain);
      if (addressIterator == collector.ipv4ByDomain.end()) {
        continue;
      }

      append_discovered_host(
        &result,
        service.displayName.empty() ? first_dns_label(service.targetDomain) : service.displayName,
        addressIterator->second,
        service.port,
        HostDiscoveryMethod::mdns
      );
    }

    std::sort(result.hosts.begin(), result.hosts.end(), [](const network::DiscoveredHost &left, const network::DiscoveredHost &right) {
      if (left.displayName != right.displayName) {
        return left.displayName < right.displayName;
      }
      if (left.address != right.address) {
        return left.address < right.address;
      }
      return app::effective_host_port(left.port) < app::effective_host_port(right.port);
    });
    return result;
  }

  /**
   * @brief Probe xemu's user-mode NAT gateway for a directly reachable host.
   *
   * xemu's default `-nic user` network exposes the host machine at `10.0.2.2`
   * but does not reliably forward multicast mDNS traffic. When the runtime
   * network reports that gateway address, try a direct Moonlight `/serverinfo`
   * query so the host still appears automatically in the common xemu setup.
   *
   * @param result Discovery result to extend.
   */
  void append_xemu_gateway_host_if_available(network::DiscoverHostsResult *result) {
    if (result == nullptr) {
      return;
    }

    if (const network::RuntimeNetworkStatus &status = network::runtime_network_status(); app::normalize_ipv4_address(status.gateway) != kXemuUserModeGatewayAddress) {
      return;
    }

    logging::debug("hosts", "Running direct gateway host check via " + std::string(kXemuUserModeGatewayAddress));

    network::HostPairingServerInfo serverInfo {};
    if (std::string errorMessage; !network::query_server_info(kXemuUserModeGatewayAddress, 0, nullptr, &serverInfo, &errorMessage)) {
      if (!errorMessage.empty()) {
        logging::debug("hosts", "Direct gateway host check failed: " + errorMessage);
      }
      return;
    }

    append_discovered_host(
      result,
      trim_ascii_whitespace(serverInfo.hostName),
      kXemuUserModeGatewayAddress,
      serverInfo.httpPort,
      HostDiscoveryMethod::directGatewayProbe
    );
  }

  /**
   * @brief Ensure the active lwIP interface exposes the multicast features mDNS needs.
   *
   * nxdk's netif is sufficient for normal IPv4 unicast traffic, but discovery
   * also needs the Ethernet and IGMP capability bits that lwIP checks before
   * joining the IPv4 mDNS multicast group on the original Xbox runtime.
   *
   * @param netif Active lwIP network interface used by discovery.
   * @return lwIP status for the multicast-capability preparation.
   */
  err_t prepare_netif_for_mdns_discovery(struct netif *netif) {
    if (netif == nullptr) {
      return ERR_ARG;
    }

    netif->flags |= NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    if (netif_igmp_data(netif) == nullptr) {
      const err_t igmpStartResult = igmp_start(netif);
      if (igmpStartResult != ERR_OK) {
        return igmpStartResult;
      }
    }
    return ERR_OK;
  }

  /**
   * @brief Initialize lwIP mDNS once before issuing discovery queries.
   *
   * lwIP search requests use a dedicated mDNS UDP PCB that is created by
   * `mdns_resp_init()`. Without that one-time initialization, discovery sends
   * queries through a null PCB and lwIP emits `udp_sendto_if: invalid pcb`.
   * The receive path also expects the active netif to be registered with mDNS,
   * so the helper adds the runtime interface once using a stable local host
   * name before the first search starts.
   *
   * @param netif Active lwIP network interface that should participate in mDNS.
   * @return lwIP status for the bootstrap attempt.
   * This helper must run while the TCP/IP core lock is held.
   */
  err_t ensure_mdns_initialized_for_discovery(struct netif *netif) {
    if (g_mdnsBootstrapState == MdnsDiscoveryBootstrapState::ready) {
      return ERR_OK;
    }
    if (netif == nullptr) {
      g_mdnsBootstrapError = ERR_ARG;
      return g_mdnsBootstrapError;
    }
    if (g_mdnsBootstrapState == MdnsDiscoveryBootstrapState::failed) {
      return g_mdnsBootstrapError;
    }

    g_mdnsBootstrapError = prepare_netif_for_mdns_discovery(netif);
    if (g_mdnsBootstrapError != ERR_OK) {
      g_mdnsBootstrapState = MdnsDiscoveryBootstrapState::failed;
      return g_mdnsBootstrapError;
    }
    mdns_resp_init();
    g_mdnsBootstrapError = mdns_resp_add_netif(netif, kMdnsDiscoveryHostName);
    if (g_mdnsBootstrapError != ERR_OK) {
      g_mdnsBootstrapState = MdnsDiscoveryBootstrapState::failed;
      return g_mdnsBootstrapError;
    }

    g_mdnsBootstrapState = MdnsDiscoveryBootstrapState::ready;
    g_mdnsBootstrapError = ERR_OK;
    return ERR_OK;
  }

#endif

}  // namespace

namespace network {

  DiscoverHostsResult discover_hosts(uint32_t timeoutMilliseconds) {
    if (timeoutMilliseconds == 0U) {
      timeoutMilliseconds = DEFAULT_DISCOVERY_TIMEOUT_MILLISECONDS;
    }

    if (const testing::HostDiscoveryTestHandler &handler = host_discovery_test_handler(); handler) {
      return handler(timeoutMilliseconds);
    }

#ifdef NXDK
    DiscoverHostsResult result {};
    if (!runtime_network_ready()) {
      result.errorMessage = runtime_network_status().summary;
      return result;
    }
    if (g_pnetif == nullptr) {
      result.errorMessage = "nxdk networking initialized without publishing an lwIP network interface";
      return result;
    }

    DiscoveryCollector collector {};
    u8_t requestId = 0;
    LOCK_TCPIP_CORE();
    const err_t mdnsBootstrapResult = ensure_mdns_initialized_for_discovery(g_pnetif);
    if (mdnsBootstrapResult != ERR_OK) {
      UNLOCK_TCPIP_CORE();
      result.errorMessage = "Failed to initialize host auto discovery mDNS support (lwIP error " + std::to_string(mdnsBootstrapResult) + ")";
      append_xemu_gateway_host_if_available(&result);
      return result;
    }
    const err_t searchResult = mdns_search_service(nullptr, kGameStreamServiceType, DNSSD_PROTO_TCP, g_pnetif, collect_mdns_search_result, &collector, &requestId);
    UNLOCK_TCPIP_CORE();
    if (searchResult != ERR_OK) {
      result.errorMessage = "Failed to start host auto discovery (lwIP error " + std::to_string(searchResult) + ")";
      return result;
    }

    Sleep(timeoutMilliseconds);
    LOCK_TCPIP_CORE();
    mdns_search_stop(requestId);
    UNLOCK_TCPIP_CORE();

    result = build_discover_hosts_result(collector);
    append_xemu_gateway_host_if_available(&result);
    std::sort(result.hosts.begin(), result.hosts.end(), [](const network::DiscoveredHost &left, const network::DiscoveredHost &right) {
      if (left.displayName != right.displayName) {
        return left.displayName < right.displayName;
      }
      if (left.address != right.address) {
        return left.address < right.address;
      }
      return app::effective_host_port(left.port) < app::effective_host_port(right.port);
    });
    return result;
#else
    (void) timeoutMilliseconds;
    return {};
#endif
  }

  namespace testing {

    void set_host_discovery_test_handler(HostDiscoveryTestHandler handler) {
      host_discovery_test_handler() = std::move(handler);
    }

    void clear_host_discovery_test_handler() {
      host_discovery_test_handler() = {};
    }

  }  // namespace testing

}  // namespace network
