/**
 * @file tests/unit/network/host_pairing_test.cpp
 * @brief Verifies host pairing helpers.
 */
// test header include
#include "src/network/host_pairing.h"

// standard includes
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// lib includes
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

// test includes
#include "tests/support/network_test_constants.h"

namespace {

  using network::testing::HostPairingHttpTestRequest;
  using network::testing::HostPairingHttpTestResponse;

  constexpr std::string_view kUnpairedClientErrorMessage = "The host reports that this client is no longer paired. Pair the host again.";

  class ScopedHostPairingHttpTestHandler {
  public:
    explicit ScopedHostPairingHttpTestHandler(network::testing::HostPairingHttpTestHandler handler) {
      network::testing::set_host_pairing_http_test_handler(std::move(handler));
    }

    ~ScopedHostPairingHttpTestHandler() {
      network::testing::clear_host_pairing_http_test_handler();
    }

    ScopedHostPairingHttpTestHandler(const ScopedHostPairingHttpTestHandler &) = delete;
    ScopedHostPairingHttpTestHandler &operator=(const ScopedHostPairingHttpTestHandler &) = delete;
  };

  struct ScriptedHttpExchange {
    std::function<void(const HostPairingHttpTestRequest &)> validateRequest;
    bool success = true;
    int statusCode = 200;
    std::string body;
    std::string errorMessage;
  };

  class ScriptedHostPairingHttpHandler {
  public:
    explicit ScriptedHostPairingHttpHandler(std::vector<ScriptedHttpExchange> exchanges):
        exchanges_(std::move(exchanges)) {
    }

    bool operator()(const HostPairingHttpTestRequest &request, HostPairingHttpTestResponse *response, std::string *errorMessage, const std::atomic<bool> *) {
      if (index_ >= exchanges_.size()) {
        ADD_FAILURE() << "Unexpected host_pairing HTTP request: " << request.pathAndQuery;
        if (errorMessage != nullptr) {
          *errorMessage = "Unexpected host_pairing HTTP request";
        }
        return false;
      }

      const ScriptedHttpExchange &exchange = exchanges_[index_];
      ++index_;
      if (exchange.validateRequest) {
        exchange.validateRequest(request);
      }
      if (!exchange.success) {
        if (errorMessage != nullptr) {
          *errorMessage = exchange.errorMessage;
        }
        return false;
      }

      if (response != nullptr) {
        response->statusCode = exchange.statusCode;
        response->body = exchange.body;
      }
      if (errorMessage != nullptr) {
        errorMessage->clear();
      }
      return true;
    }

    [[nodiscard]] bool all_consumed() const {
      return index_ == exchanges_.size();
    }

  private:
    std::vector<ScriptedHttpExchange> exchanges_;
    std::size_t index_ = 0U;
  };

  template<typename Handler>
  network::testing::HostPairingHttpTestHandler make_host_pairing_http_test_handler(Handler *handler) {
    return [handler](const HostPairingHttpTestRequest &request, HostPairingHttpTestResponse *response, std::string *errorMessage, const std::atomic<bool> *cancelRequested) {
      return (*handler)(request, response, errorMessage, cancelRequested);
    };
  }

  struct PkeyDeleter {
    void operator()(EVP_PKEY *key) const {
      if (key != nullptr) {
        EVP_PKEY_free(key);
      }
    }
  };

  struct BioDeleter {
    void operator()(BIO *bio) const {
      if (bio != nullptr) {
        BIO_free(bio);
      }
    }
  };

  std::string extract_query_parameter(std::string_view pathAndQuery, std::string_view parameterName) {
    const std::string needle = std::string(parameterName) + "=";
    const std::size_t parameterStart = pathAndQuery.find(needle);
    if (parameterStart == std::string_view::npos) {
      return {};
    }

    const std::size_t valueStart = parameterStart + needle.size();
    const std::size_t valueEnd = pathAndQuery.find('&', valueStart);
    return std::string(pathAndQuery.substr(valueStart, valueEnd == std::string_view::npos ? std::string_view::npos : valueEnd - valueStart));
  }

  const unsigned char *openssl_bytes(const std::byte *data) {
    return reinterpret_cast<const unsigned char *>(data);
  }

  unsigned char *openssl_bytes(std::byte *data) {
    return reinterpret_cast<unsigned char *>(data);
  }

  std::string hex_encode_bytes(const std::byte *data, std::size_t size) {
    static constexpr char kHexDigits[] = "0123456789abcdef";

    std::string output;
    output.reserve(size * 2U);
    for (std::size_t index = 0; index < size; ++index) {
      const unsigned int byteValue = std::to_integer<unsigned int>(data[index]);
      output.push_back(kHexDigits[(byteValue >> 4U) & 0x0F]);
      output.push_back(kHexDigits[byteValue & 0x0F]);
    }
    return output;
  }

  std::string hex_encode_text(std::string_view text) {
    return hex_encode_bytes(reinterpret_cast<const std::byte *>(text.data()), text.size());
  }

  std::vector<std::byte> hex_decode_text(std::string_view text) {
    std::vector<std::byte> bytes;
    EXPECT_EQ(text.size() % 2U, 0U);
    bytes.reserve(text.size() / 2U);
    for (std::size_t index = 0; index + 1U < text.size(); index += 2U) {
      char encodedByte[] = {text[index], text[index + 1U], '\0'};
      unsigned int value = 0U;
      const std::from_chars_result decodeResult = std::from_chars(encodedByte, encodedByte + 2, value, 16);
      EXPECT_EQ(decodeResult.ec, std::errc());
      EXPECT_EQ(decodeResult.ptr, encodedByte + 2);
      bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
  }

  std::vector<std::byte> sha256_digest(const std::byte *data, std::size_t size) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digestBuffer {};
    unsigned int digestSize = 0U;
    EXPECT_EQ(EVP_Digest(openssl_bytes(data), size, digestBuffer.data(), &digestSize, EVP_sha256(), nullptr), 1);

    std::vector<std::byte> digest;
    digest.reserve(digestSize);
    for (unsigned int index = 0; index < digestSize; ++index) {
      digest.push_back(static_cast<std::byte>(digestBuffer[index]));
    }
    return digest;
  }

  std::vector<std::byte> derive_pairing_aes_key(std::string_view saltHex, std::string_view pin) {
    std::vector<std::byte> source = hex_decode_text(saltHex);
    source.reserve(source.size() + pin.size());
    for (const char character : pin) {
      source.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return sha256_digest(source.data(), source.size());
  }

  std::vector<std::byte> aes_128_ecb_encrypt(const std::vector<std::byte> &plaintext, const std::vector<std::byte> &key) {
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    EXPECT_NE(context, nullptr);
    EXPECT_EQ(EVP_EncryptInit_ex(context.get(), EVP_aes_128_ecb(), nullptr, openssl_bytes(key.data()), nullptr), 1);
    EXPECT_EQ(EVP_CIPHER_CTX_set_padding(context.get(), 0), 1);

    std::vector<std::byte> ciphertext(plaintext.size() + 16U);
    int ciphertextSize = 0;
    EXPECT_EQ(EVP_EncryptUpdate(context.get(), openssl_bytes(ciphertext.data()), &ciphertextSize, openssl_bytes(plaintext.data()), static_cast<int>(plaintext.size())), 1);
    ciphertext.resize(static_cast<std::size_t>(ciphertextSize));
    return ciphertext;
  }

  std::string sign_sha256_hex(const std::vector<std::byte> &data, std::string_view privateKeyPem) {
    std::unique_ptr<BIO, BioDeleter> bio(BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size())));
    EXPECT_NE(bio, nullptr);
    std::unique_ptr<EVP_PKEY, PkeyDeleter> privateKey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
    EXPECT_NE(privateKey, nullptr);

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    EXPECT_NE(context, nullptr);
    EXPECT_EQ(EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, privateKey.get()), 1);
    EXPECT_EQ(EVP_DigestSignUpdate(context.get(), openssl_bytes(data.data()), data.size()), 1);

    std::size_t signatureSize = 0U;
    EXPECT_EQ(EVP_DigestSignFinal(context.get(), nullptr, &signatureSize), 1);
    std::vector<unsigned char> signature(signatureSize);
    EXPECT_EQ(EVP_DigestSignFinal(context.get(), signature.data(), &signatureSize), 1);
    signature.resize(signatureSize);
    return hex_encode_bytes(reinterpret_cast<const std::byte *>(signature.data()), signature.size());
  }

  std::vector<std::byte> filled_bytes(std::size_t size, std::byte value) {
    return std::vector<std::byte>(size, value);
  }

  std::vector<std::byte> sequential_bytes(std::size_t size) {
    std::vector<std::byte> bytes(size);
    for (std::size_t index = 0; index < size; ++index) {
      bytes[index] = static_cast<std::byte>(index + 1U);
    }
    return bytes;
  }

  std::string make_server_info_xml(bool paired, uint16_t httpPort, uint16_t httpsPort, std::string_view hostName = "Scripted Host", std::string_view uuid = "scripted-host") {
    return "<root status_code=\"200\"><hostname>" + std::string(hostName) + "</hostname><appversion>7.1.0.0</appversion><uuid>" + std::string(uuid) + "</uuid><LocalIP>" +
           std::string(test_support::kTestIpv4Addresses[test_support::kIpServerLocal]) + "</LocalIP><ExternalIP>" + std::string(test_support::kTestIpv4Addresses[test_support::kIpServerExternal]) +
           "</ExternalIP><ExternalPort>" + std::to_string(httpPort) + "</ExternalPort><HttpsPort>" + std::to_string(httpsPort) + "</HttpsPort><PairStatus>" + (paired ? "1" : "0") + "</PairStatus></root>";
  }

  std::string make_pair_phase_response(std::string_view pairedValue, std::string_view tagName = {}, std::string_view tagValue = {}) {
    std::string response = "<root><paired>" + std::string(pairedValue) + "</paired>";
    if (!tagName.empty()) {
      response += "<" + std::string(tagName) + ">" + std::string(tagValue) + "</" + std::string(tagName) + ">";
    }
    response += "</root>";
    return response;
  }

  bool fail_unexpected_pairing_request(std::string *errorMessage) {
    if (errorMessage != nullptr) {
      *errorMessage = "Unexpected extra pairing request";
    }
    ADD_FAILURE() << "Unexpected extra pairing request";
    return false;
  }

  bool handle_short_challenge_pairing_request(
    std::size_t *callCount,
    std::string *saltHex,
    std::string_view pin,
    const network::PairingIdentity &serverIdentity,
    const HostPairingHttpTestRequest &request,
    HostPairingHttpTestResponse *response
  ) {
    switch ((*callCount)++) {
      case 0U:
        response->statusCode = 200;
        response->body = make_server_info_xml(false, 47989U, 47990U, "Pair Host", "pair-host");
        return true;
      case 1U:
        EXPECT_EQ(extract_query_parameter(request.pathAndQuery, "phrase"), "getservercert");
        *saltHex = extract_query_parameter(request.pathAndQuery, "salt");
        response->statusCode = 200;
        response->body = make_pair_phase_response("1", "plaincert", hex_encode_text(serverIdentity.certificatePem));
        return true;
      case 2U:
        {
          const std::vector<std::byte> aesKey = derive_pairing_aes_key(*saltHex, pin);
          const std::vector<std::byte> shortPlaintext = filled_bytes(16U, std::byte {0x2A});
          const std::vector<std::byte> encryptedResponse = aes_128_ecb_encrypt(shortPlaintext, aesKey);
          response->statusCode = 200;
          response->body = make_pair_phase_response("1", "challengeresponse", hex_encode_bytes(encryptedResponse.data(), encryptedResponse.size()));
          return true;
        }
      default:
        return fail_unexpected_pairing_request(nullptr);
    }
  }

  bool handle_successful_pairing_request(
    std::size_t *callCount,
    std::string *saltHex,
    std::string_view pin,
    const network::PairingIdentity &clientIdentity,
    const network::PairingIdentity &serverIdentity,
    const HostPairingHttpTestRequest &request,
    HostPairingHttpTestResponse *response,
    std::string *errorMessage
  ) {
    switch ((*callCount)++) {
      case 0U:
        EXPECT_FALSE(request.useTls);
        EXPECT_NE(request.pathAndQuery.find("/serverinfo?uniqueid=" + clientIdentity.uniqueId), std::string::npos);
        response->statusCode = 200;
        response->body = make_server_info_xml(false, 47989U, 47990U, "Pair Host", "pair-host");
        return true;
      case 1U:
        EXPECT_EQ(extract_query_parameter(request.pathAndQuery, "phrase"), "getservercert");
        EXPECT_EQ(extract_query_parameter(request.pathAndQuery, "clientcert"), hex_encode_text(clientIdentity.certificatePem));
        *saltHex = extract_query_parameter(request.pathAndQuery, "salt");
        response->statusCode = 200;
        response->body = make_pair_phase_response("1", "plaincert", hex_encode_text(serverIdentity.certificatePem));
        return true;
      case 2U:
        {
          const std::vector<std::byte> aesKey = derive_pairing_aes_key(*saltHex, pin);
          const std::vector<std::byte> challengePlaintext = sequential_bytes(48U);
          const std::vector<std::byte> encryptedResponse = aes_128_ecb_encrypt(challengePlaintext, aesKey);
          EXPECT_FALSE(extract_query_parameter(request.pathAndQuery, "clientchallenge").empty());
          response->statusCode = 200;
          response->body = make_pair_phase_response("1", "challengeresponse", hex_encode_bytes(encryptedResponse.data(), encryptedResponse.size()));
          return true;
        }
      case 3U:
        {
          const std::vector<std::byte> serverSecret = filled_bytes(16U, std::byte {0x5A});
          EXPECT_FALSE(extract_query_parameter(request.pathAndQuery, "serverchallengeresp").empty());
          response->statusCode = 200;
          response->body = make_pair_phase_response("1", "pairingsecret", hex_encode_bytes(serverSecret.data(), serverSecret.size()) + sign_sha256_hex(serverSecret, serverIdentity.privateKeyPem));
          return true;
        }
      case 4U:
        EXPECT_FALSE(extract_query_parameter(request.pathAndQuery, "clientpairingsecret").empty());
        response->statusCode = 200;
        response->body = make_pair_phase_response("1");
        return true;
      case 5U:
        EXPECT_TRUE(request.useTls);
        if (request.tlsClientIdentity == nullptr) {
          if (errorMessage != nullptr) {
            *errorMessage = "Expected a TLS client identity during pairchallenge";
          }
          ADD_FAILURE() << "Expected a TLS client identity during pairchallenge";
          return false;
        }
        EXPECT_EQ(request.tlsClientIdentity->uniqueId, clientIdentity.uniqueId);
        EXPECT_EQ(request.expectedTlsCertificatePem, serverIdentity.certificatePem);
        EXPECT_EQ(extract_query_parameter(request.pathAndQuery, "phrase"), "pairchallenge");
        response->statusCode = 200;
        response->body = make_pair_phase_response("1");
        return true;
      default:
        return fail_unexpected_pairing_request(errorMessage);
    }
  }

  TEST(HostPairingTest, CreatesAValidClientIdentity) {
    std::string errorMessage;
    const network::PairingIdentity identity = network::create_pairing_identity(&errorMessage);

    EXPECT_TRUE(network::is_valid_pairing_identity(identity));
    EXPECT_TRUE(errorMessage.empty());
    EXPECT_EQ(identity.uniqueId.size(), 16U);
    EXPECT_NE(identity.certificatePem.find("BEGIN CERTIFICATE"), std::string::npos);
    EXPECT_NE(identity.privateKeyPem.find("BEGIN PRIVATE KEY"), std::string::npos);
  }

  TEST(HostPairingTest, GeneratesSecureFourDigitPins) {
    for (int attempt = 0; attempt < 32; ++attempt) {
      std::string pin;
      std::string errorMessage;

      ASSERT_TRUE(network::generate_pairing_pin(&pin, &errorMessage)) << errorMessage;
      EXPECT_TRUE(errorMessage.empty());
      ASSERT_EQ(pin.size(), 4U);
      for (char character : pin) {
        EXPECT_GE(character, '0');
        EXPECT_LE(character, '9');
      }
    }
  }

  TEST(HostPairingTest, RequiresAnOutputBufferWhenGeneratingPins) {
    std::string errorMessage;

    EXPECT_FALSE(network::generate_pairing_pin(nullptr, &errorMessage));
    EXPECT_EQ(errorMessage, "A pairing PIN output buffer is required");
  }

  TEST(HostPairingTest, RejectsIncompletePairingIdentities) {
    EXPECT_FALSE(network::is_valid_pairing_identity({}));
    EXPECT_FALSE(network::is_valid_pairing_identity({"ABCDEF0123456789", "not a certificate", "not a key"}));
  }

  TEST(HostPairingTest, CancelledPairingReturnsImmediatelyBeforeStartingTheHandshake) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    std::atomic cancelRequested {true};
    const network::HostPairingResult result = network::pair_host({
                                                                   test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
                                                                   test_support::kTestPorts[test_support::kPortPairing],
                                                                   "1234",
                                                                   "MoonlightXboxOG",
                                                                   identity,
                                                                 },
                                                                 &cancelRequested);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.alreadyPaired);
    EXPECT_EQ(result.message, "Pairing cancelled");
  }

  TEST(HostPairingTest, ParsesServerInfoResponsesForPairing) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<hostname>Sunshine-PC</hostname>"
      "<appversion>7.1.431.0</appversion>"
      "<uuid>host-uuid-123</uuid>"
      "<LocalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpServerLocal]) +
      "</LocalIP>"
      "<ExternalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpServerExternal]) +
      "</ExternalIP>"
      "<LocalIP6>fe80::1234</LocalIP6>"
      "<MacAddress>00:11:22:33:44:55</MacAddress>"
      "<CurrentGame>42</CurrentGame>"
      "<ExternalPort>47989</ExternalPort>"
      "<HttpsPort>47990</HttpsPort>"
      "<PairStatus>1</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, test_support::kTestPorts[test_support::kPortPairing], &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.serverMajorVersion, 7);
    EXPECT_EQ(serverInfo.httpPort, test_support::kTestPorts[test_support::kPortResolvedHttp]);
    EXPECT_EQ(serverInfo.httpsPort, test_support::kTestPorts[test_support::kPortResolvedHttps]);
    EXPECT_TRUE(serverInfo.paired);
    EXPECT_EQ(serverInfo.hostName, "Sunshine-PC");
    EXPECT_EQ(serverInfo.uuid, "host-uuid-123");
    EXPECT_EQ(serverInfo.activeAddress, test_support::kTestIpv4Addresses[test_support::kIpServerLocal]);
    EXPECT_EQ(serverInfo.localAddress, test_support::kTestIpv4Addresses[test_support::kIpServerLocal]);
    EXPECT_EQ(serverInfo.remoteAddress, test_support::kTestIpv4Addresses[test_support::kIpServerExternal]);
    EXPECT_EQ(serverInfo.ipv6Address, "fe80::1234");
    EXPECT_EQ(serverInfo.macAddress, "00:11:22:33:44:55");
    EXPECT_EQ(serverInfo.runningGameId, 42U);
  }

  TEST(HostPairingTest, RejectsServerInfoResponsesMissingRequiredFields) {
    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    EXPECT_FALSE(network::parse_server_info_response("<root><HttpsPort>47990</HttpsPort></root>", test_support::kTestPorts[test_support::kPortPairing], &serverInfo, &errorMessage));
    EXPECT_FALSE(errorMessage.empty());
  }

  TEST(HostPairingTest, ParsesAlternateServerInfoFieldNamesAndFallsBackToTheRequestedPort) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<HostName>Bedroom PC</HostName>"
      "<appversion>8.2.0.0</appversion>"
      "<UniqueId>host-uuid-456</UniqueId>"
      "<localip>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpLocalFallback]) +
      "</localip>"
      "<externalip>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpExternalFallback]) +
      "</externalip>"
      "<IPv6>fe80::beef</IPv6>"
      "<mac>AA:BB:CC:DD:EE:FF</mac>"
      "<currentgame>invalid</currentgame>"
      "<HttpPort>bad</HttpPort>"
      "<HttpsPort>0</HttpsPort>"
      "<PairStatus>0</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, test_support::kTestPorts[test_support::kPortPairing], &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.serverMajorVersion, 8);
    EXPECT_EQ(serverInfo.httpPort, test_support::kTestPorts[test_support::kPortPairing]);
    EXPECT_EQ(serverInfo.httpsPort, test_support::kTestPorts[test_support::kPortPairing]);
    EXPECT_FALSE(serverInfo.paired);
    EXPECT_EQ(serverInfo.hostName, "Bedroom PC");
    EXPECT_EQ(serverInfo.uuid, "host-uuid-456");
    EXPECT_EQ(serverInfo.activeAddress, test_support::kTestIpv4Addresses[test_support::kIpLocalFallback]);
    EXPECT_EQ(serverInfo.localAddress, test_support::kTestIpv4Addresses[test_support::kIpLocalFallback]);
    EXPECT_EQ(serverInfo.remoteAddress, test_support::kTestIpv4Addresses[test_support::kIpExternalFallback]);
    EXPECT_EQ(serverInfo.ipv6Address, "fe80::beef");
    EXPECT_EQ(serverInfo.macAddress, "AA:BB:CC:DD:EE:FF");
    EXPECT_EQ(serverInfo.runningGameId, 0U);
  }

  TEST(HostPairingTest, PrefersRequestedAddressForFollowUpRequests) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<hostname>Sunshine-PC</hostname>"
      "<appversion>7.1.431.0</appversion>"
      "<uuid>host-uuid-123</uuid>"
      "<LocalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpLoopback]) +
      "</LocalIP>"
      "<ExternalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpExternalFallback]) +
      "</ExternalIP>"
      "<HttpsPort>47990</HttpsPort>"
      "<PairStatus>1</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, test_support::kTestPorts[test_support::kPortPairing], &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.activeAddress, test_support::kTestIpv4Addresses[test_support::kIpLoopback]);
    EXPECT_EQ(
      network::resolve_reachable_address(test_support::kTestIpv4Addresses[test_support::kIpRuntimeDhcpGateway], serverInfo),
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeDhcpGateway]
    );
  }

  TEST(HostPairingTest, FallsBackToReportedAddressWhenRequestedAddressIsMissing) {
    network::HostPairingServerInfo serverInfo {};
    serverInfo.activeAddress = test_support::kTestIpv4Addresses[test_support::kIpExternalFallback];
    serverInfo.localAddress = test_support::kTestIpv4Addresses[test_support::kIpLocalFallback];
    serverInfo.remoteAddress = test_support::kTestIpv4Addresses[test_support::kIpServerExternalAlt];

    EXPECT_EQ(network::resolve_reachable_address({}, serverInfo), test_support::kTestIpv4Addresses[test_support::kIpExternalFallback]);
  }

  TEST(HostPairingTest, ParsesHostAppLists) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<App><AppTitle>Steam</AppTitle><ID>101</ID><IsHdrSupported>1</IsHdrSupported></App>"
      "<App><AppTitle>Desktop</AppTitle><ID>102</ID><Hidden>0</Hidden></App>"
      "<App><AppTitle>Broken Entry</AppTitle><ID>oops</ID></App>"
      "</root>";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(xml, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[0].id, 101);
    EXPECT_TRUE(apps[0].hdrSupported);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 102);
    EXPECT_FALSE(apps[1].hidden);
  }

  TEST(HostPairingTest, RejectsAppListsWithoutValidApps) {
    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::parse_app_list_response("<root><App><AppTitle>Broken</AppTitle><ID>bad</ID></App></root>", &apps, &errorMessage));
    EXPECT_FALSE(errorMessage.empty());
  }

  TEST(HostPairingTest, ReportsExplicitHostStatusErrorsWhenAppEntriesAreMissing) {
    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::parse_app_list_response("<root status_code=\"503\" status_message=\"Host is busy\"></root>", &apps, &errorMessage));
    EXPECT_EQ(errorMessage, "Host is busy");
  }

  TEST(HostPairingTest, ParsesAttributeBasedHostAppLists) {
    const std::string xml = R"(<root status_code="200"><App ID="201" AppTitle="Steam" IsHdrSupported="1" /><App appid="202" title="Desktop" hidden="0" /></root>)";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(xml, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[0].id, 201);
    EXPECT_TRUE(apps[0].hdrSupported);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 202);
    EXPECT_FALSE(apps[1].hidden);
  }

  TEST(HostPairingTest, ParsesAlternateXmlGameElementsInAppLists) {
    const std::string xml = R"(<root status_code="200"><Game><Name>Steam</Name><ID>301</ID><Hidden>false</Hidden></Game><Game title="Desktop" id="302" IsHdrSupported="true" /></root>)";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(xml, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[0].id, 301);
    EXPECT_FALSE(apps[0].hidden);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 302);
    EXPECT_TRUE(apps[1].hdrSupported);
  }

  TEST(HostPairingTest, ParsesProgramAndApplicationElementsWithAttributeFlags) {
    const std::string xml =
      R"(<root status_code="200"><Application Name="Big Picture" id="501" hidden="yes" HDRSupported="true" /><Application title="Desktop" appId="502" IsHidden="no" /></root>)";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(xml, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Big Picture");
    EXPECT_EQ(apps[0].id, 501);
    EXPECT_TRUE(apps[0].hdrSupported);
    EXPECT_TRUE(apps[0].hidden);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 502);
    EXPECT_FALSE(apps[1].hidden);
  }

  TEST(HostPairingTest, RejectsNonXmlAppLists) {
    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::parse_app_list_response(R"({"apps":[{"name":"Steam","id":401}]})", &apps, &errorMessage));
    EXPECT_NE(errorMessage.find("not XML"), std::string::npos);
  }

  TEST(HostPairingTest, HashesEquivalentAppListsStablyAndDetectsChanges) {
    const std::vector<network::HostAppEntry> baseline = {
      {"Steam", 101, true, false},
      {"Desktop", 102, false, false},
    };
    const std::vector<network::HostAppEntry> identical = {
      {"Steam", 101, true, false},
      {"Desktop", 102, false, false},
    };
    const std::vector<network::HostAppEntry> changed = {
      {"Steam", 101, true, false},
      {"Desktop", 102, false, true},
    };

    EXPECT_EQ(network::hash_app_list_entries(baseline), network::hash_app_list_entries(identical));
    EXPECT_NE(network::hash_app_list_entries(baseline), network::hash_app_list_entries(changed));
  }

  TEST(HostPairingTest, DetectsExplicitUnpairedClientErrors) {
    EXPECT_TRUE(network::error_indicates_unpaired_client("The host reports that this client is no longer paired. Pair the host again."));
    EXPECT_TRUE(network::error_indicates_unpaired_client("The client is not authorized. Certificate verification failed."));
    EXPECT_TRUE(network::error_indicates_unpaired_client("The host returned HTTP 401 while requesting /applist"));
    EXPECT_TRUE(network::error_indicates_unpaired_client("Request failed with HTTP 403 FORBIDDEN"));
    EXPECT_TRUE(network::error_indicates_unpaired_client("The client is UNAUTHORIZED to access this host"));
    EXPECT_FALSE(network::error_indicates_unpaired_client("Timed out while refreshing apps"));
  }

  TEST(HostPairingTest, ResolvesReachableAddressesInPriorityOrder) {
    network::HostPairingServerInfo serverInfo {};
    serverInfo.activeAddress = test_support::kTestIpv4Addresses[test_support::kIpServerExternalAlt];
    serverInfo.localAddress = test_support::kTestIpv4Addresses[test_support::kIpLocalFallback];
    serverInfo.remoteAddress = test_support::kTestIpv4Addresses[test_support::kIpExternalFallback];

    EXPECT_EQ(
      network::resolve_reachable_address(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], serverInfo),
      test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]
    );

    EXPECT_EQ(network::resolve_reachable_address({}, serverInfo), test_support::kTestIpv4Addresses[test_support::kIpServerExternalAlt]);

    serverInfo.activeAddress.clear();
    EXPECT_EQ(network::resolve_reachable_address({}, serverInfo), test_support::kTestIpv4Addresses[test_support::kIpLocalFallback]);

    serverInfo.localAddress.clear();
    EXPECT_EQ(network::resolve_reachable_address({}, serverInfo), test_support::kTestIpv4Addresses[test_support::kIpExternalFallback]);

    serverInfo.remoteAddress.clear();
    EXPECT_TRUE(network::resolve_reachable_address({}, serverInfo).empty());
  }

  TEST(HostPairingTest, QueriesServerInfoAcrossFallbackPortsWhenTheDefaultPortFails) {
    ScriptedHostPairingHttpHandler handler({
      {
        [](const HostPairingHttpTestRequest &request) {
          EXPECT_EQ(request.port, 47989U);
          EXPECT_FALSE(request.useTls);
          EXPECT_EQ(request.pathAndQuery, "/serverinfo?uniqueid=0123456789ABCDEF&uuid=11111111-2222-3333-4444-555555555555");
        },
        false,
        0,
        {},
        "default port failed",
      },
      {
        [](const HostPairingHttpTestRequest &request) {
          EXPECT_EQ(request.port, 47984U);
          EXPECT_FALSE(request.useTls);
        },
        true,
        200,
        make_server_info_xml(false, 47984U, 47990U, "Fallback Host", "fallback-host"),
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::query_server_info(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, nullptr, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_TRUE(handler.all_consumed());
    EXPECT_EQ(serverInfo.httpPort, 47984U);
    EXPECT_EQ(serverInfo.httpsPort, 47990U);
    EXPECT_EQ(serverInfo.hostName, "Fallback Host");
  }

  TEST(HostPairingTest, QueriesAuthorizedServerInfoForTheCurrentClientWhenIdentityIsAvailable) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    ScriptedHostPairingHttpHandler handler({
      {
        [&identity](const HostPairingHttpTestRequest &request) {
          EXPECT_FALSE(request.useTls);
          EXPECT_EQ(request.tlsClientIdentity, nullptr);
          EXPECT_NE(request.pathAndQuery.find("uniqueid=" + identity.uniqueId), std::string::npos);
        },
        true,
        200,
        make_server_info_xml(true, 47989U, 47990U, "Base Host", "base-host"),
      },
      {
        [&identity](const HostPairingHttpTestRequest &request) {
          ASSERT_NE(request.tlsClientIdentity, nullptr);
          EXPECT_TRUE(request.useTls);
          EXPECT_EQ(request.port, 47990U);
          EXPECT_EQ(request.tlsClientIdentity->uniqueId, identity.uniqueId);
        },
        true,
        200,
        make_server_info_xml(true, 47989U, 47990U, "Authorized Host", "authorized-host"),
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::query_server_info(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing], &identity, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_TRUE(handler.all_consumed());
    EXPECT_TRUE(serverInfo.pairingStatusCurrentClientKnown);
    EXPECT_TRUE(serverInfo.pairingStatusCurrentClient);
    EXPECT_TRUE(serverInfo.paired);
    EXPECT_EQ(serverInfo.hostName, "Authorized Host");
    EXPECT_EQ(serverInfo.uuid, "authorized-host");
  }

  TEST(HostPairingTest, MarksTheCurrentClientUnpairedWhenAuthorizedServerInfoReturnsUnauthorized) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    ScriptedHostPairingHttpHandler handler({
      {
        {},
        true,
        200,
        make_server_info_xml(true, 47989U, 47990U, "Base Host", "base-host"),
      },
      {
        {},
        true,
        401,
        "",
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::query_server_info(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing], &identity, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_TRUE(serverInfo.pairingStatusCurrentClientKnown);
    EXPECT_FALSE(serverInfo.pairingStatusCurrentClient);
    EXPECT_FALSE(serverInfo.paired);
  }

  TEST(HostPairingTest, LeavesCurrentClientPairingStatusUnknownWhenAuthorizedServerInfoCannotBeParsed) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    ScriptedHostPairingHttpHandler handler({
      {
        {},
        true,
        200,
        make_server_info_xml(true, 47989U, 47990U, "Base Host", "base-host"),
      },
      {
        {},
        true,
        200,
        "<root><bad>response</bad></root>",
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::query_server_info(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing], &identity, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_FALSE(serverInfo.pairingStatusCurrentClientKnown);
    EXPECT_TRUE(serverInfo.paired);
    EXPECT_EQ(serverInfo.hostName, "Base Host");
  }

  TEST(HostPairingTest, QueryAppListPropagatesServerInfoFailures) {
    ScopedHostPairingHttpTestHandler guard([](const HostPairingHttpTestRequest &, HostPairingHttpTestResponse *, std::string *errorMessage, const std::atomic<bool> *) {
      if (errorMessage != nullptr) {
        *errorMessage = "serverinfo unavailable";
      }
      return false;
    });

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::query_app_list(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing], nullptr, &apps, nullptr, &errorMessage));
    EXPECT_NE(errorMessage.find("serverinfo unavailable"), std::string::npos);
  }

  TEST(HostPairingTest, QueryAppListMapsUnauthorizedHttpResponsesToTheUnpairedMessage) {
    ScriptedHostPairingHttpHandler handler({
      {
        {},
        true,
        200,
        make_server_info_xml(true, 47989U, 47990U),
      },
      {
        [](const HostPairingHttpTestRequest &request) {
          EXPECT_TRUE(request.useTls);
          EXPECT_NE(request.pathAndQuery.find("/applist?uniqueid=0123456789ABCDEF"), std::string::npos);
        },
        true,
        401,
        "",
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::query_app_list(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing], nullptr, &apps, nullptr, &errorMessage));
    EXPECT_EQ(errorMessage, kUnpairedClientErrorMessage);
    EXPECT_TRUE(handler.all_consumed());
  }

  TEST(HostPairingTest, QueryAppListMapsUnauthorizedPayloadsToTheUnpairedMessage) {
    ScriptedHostPairingHttpHandler handler({
      {
        {},
        true,
        200,
        make_server_info_xml(true, 47989U, 47990U),
      },
      {
        {},
        true,
        200,
        R"(<root status_code="403" status_message="unauthorized"></root>)",
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::query_app_list(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing], nullptr, &apps, nullptr, &errorMessage));
    EXPECT_EQ(errorMessage, kUnpairedClientErrorMessage);
  }

  TEST(HostPairingTest, QueryAppListReturnsAppsAndResolvedServerInfoOnSuccess) {
    ScriptedHostPairingHttpHandler handler({
      {
        {},
        true,
        200,
        make_server_info_xml(true, 47989U, 47990U, "Apps Host", "apps-host"),
      },
      {
        {},
        true,
        200,
        "<root status_code=\"200\"><App><AppTitle>Steam</AppTitle><ID>101</ID></App><App><AppTitle>Desktop</AppTitle><ID>102</ID><Hidden>1</Hidden></App></root>",
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    std::vector<network::HostAppEntry> apps;
    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::query_app_list(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing], nullptr, &apps, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_TRUE(handler.all_consumed());
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_TRUE(serverInfo.pairingStatusCurrentClient);
    EXPECT_EQ(serverInfo.hostName, "Apps Host");
    EXPECT_EQ(serverInfo.uuid, "apps-host");
  }

  TEST(HostPairingTest, QueryAppAssetRejectsInvalidRequestsBeforeStartingTransport) {
    std::vector<unsigned char> assetBytes;
    std::string errorMessage;

    EXPECT_FALSE(network::query_app_asset({}, 47990U, nullptr, 77, &assetBytes, &errorMessage));
    EXPECT_EQ(errorMessage, "The app-asset request requires a valid host address, port, and app ID");
  }

  TEST(HostPairingTest, QueryAppAssetFallsBackAcrossCandidatePathsUntilOneSucceeds) {
    std::vector<std::string> requestedPaths;
    ScriptedHostPairingHttpHandler handler({
      {
        [&requestedPaths](const HostPairingHttpTestRequest &request) {
          requestedPaths.push_back(request.pathAndQuery);
          EXPECT_TRUE(request.useTls);
          EXPECT_NE(request.pathAndQuery.find("AssetIdx=0"), std::string::npos);
        },
        true,
        200,
        "<root status_code=\"200\"></root>",
      },
      {
        [&requestedPaths](const HostPairingHttpTestRequest &request) {
          requestedPaths.push_back(request.pathAndQuery);
          EXPECT_TRUE(request.useTls);
        },
        true,
        200,
        std::string("\x89PNG", 4),
      },
    });
    ScopedHostPairingHttpTestHandler guard(make_host_pairing_http_test_handler(&handler));

    std::vector<unsigned char> assetBytes;
    std::string errorMessage;

    ASSERT_TRUE(network::query_app_asset(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 47990U, nullptr, 77, &assetBytes, &errorMessage)) << errorMessage;
    EXPECT_TRUE(handler.all_consumed());
    ASSERT_EQ(requestedPaths.size(), 2U);
    EXPECT_FALSE(assetBytes.empty());
    EXPECT_EQ(assetBytes[0], 0x89U);
  }

  TEST(HostPairingTest, QueryAppAssetAggregatesAttemptFailuresWhenNoCandidateSucceeds) {
    std::size_t callCount = 0U;
    ScopedHostPairingHttpTestHandler guard([&callCount](const HostPairingHttpTestRequest &, HostPairingHttpTestResponse *, std::string *errorMessage, const std::atomic<bool> *) {
      ++callCount;
      if (errorMessage != nullptr) {
        *errorMessage = "network down";
      }
      return false;
    });

    std::vector<unsigned char> assetBytes;
    std::string errorMessage;

    EXPECT_FALSE(network::query_app_asset(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 47990U, nullptr, 77, &assetBytes, &errorMessage));
    EXPECT_EQ(callCount, 6U);
    EXPECT_NE(errorMessage.find("Failed to fetch app artwork for app ID 77"), std::string::npos);
    EXPECT_NE(errorMessage.find("network down"), std::string::npos);
  }

  TEST(HostPairingTest, PairHostRejectsInvalidRequestsBeforeStartingTransport) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    EXPECT_EQ(network::pair_host({{}, 47989U, "1234", "MoonlightXboxOG", identity}).message, "Pairing requires a valid host address");
    EXPECT_EQ(network::pair_host({test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 47989U, "123", "MoonlightXboxOG", identity}).message, "Pairing requires a four-digit PIN");
    EXPECT_EQ(network::pair_host({test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 47989U, "1234", "MoonlightXboxOG", {}}).message, "Client pairing identity is missing or invalid");
  }

  TEST(HostPairingTest, PairHostShortCircuitsWhenTheHostAlreadyReportsTheClientAsPaired) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    std::size_t callCount = 0U;
    ScopedHostPairingHttpTestHandler guard([&callCount](const HostPairingHttpTestRequest &request, HostPairingHttpTestResponse *response, std::string *, const std::atomic<bool> *) {
      ++callCount;
      EXPECT_FALSE(request.useTls);
      response->statusCode = 200;
      response->body = make_server_info_xml(true, 47989U, 47990U, "Already Paired Host", "already-paired-host");
      return true;
    });

    const network::HostPairingResult result = network::pair_host({
      test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
      47989U,
      "1234",
      "MoonlightXboxOG",
      identity,
    });

    EXPECT_EQ(callCount, 1U);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.alreadyPaired);
    EXPECT_EQ(result.message, "The host already reports this client as paired");
  }

  TEST(HostPairingTest, PairHostPreservesThePhaseOneRejectionMessage) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    std::size_t callCount = 0U;
    ScopedHostPairingHttpTestHandler guard([&callCount](const HostPairingHttpTestRequest &request, HostPairingHttpTestResponse *response, std::string *, const std::atomic<bool> *) {
      if (callCount++ == 0U) {
        response->statusCode = 200;
        response->body = make_server_info_xml(false, 47989U, 47990U, "Pair Host", "pair-host");
        return true;
      }

      EXPECT_NE(request.pathAndQuery.find("phrase=getservercert"), std::string::npos);
      response->statusCode = 200;
      response->body = make_pair_phase_response("0");
      return true;
    });

    const network::HostPairingResult result = network::pair_host({
      test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
      47989U,
      "1234",
      "MoonlightXboxOG",
      identity,
    });

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Pairing failed during phase 1 (getservercert): The host rejected the initial pairing request");
  }

  TEST(HostPairingTest, PairHostFailsWhenTheChallengeResponseIsTooShort) {
    const network::PairingIdentity clientIdentity = network::create_pairing_identity();
    const network::PairingIdentity serverIdentity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(clientIdentity));
    ASSERT_TRUE(network::is_valid_pairing_identity(serverIdentity));

    const std::string pin = "1234";
    std::size_t callCount = 0U;
    std::string saltHex;
    ScopedHostPairingHttpTestHandler guard([&](const HostPairingHttpTestRequest &request, HostPairingHttpTestResponse *response, std::string *, const std::atomic<bool> *) {
      return handle_short_challenge_pairing_request(&callCount, &saltHex, pin, serverIdentity, request, response);
    });

    const network::HostPairingResult result = network::pair_host({
      test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
      47989U,
      pin,
      "MoonlightXboxOG",
      clientIdentity,
    });

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "Pairing failed during phase 2 (client challenge): The host returned an incomplete challenge response during pairing");
  }

  TEST(HostPairingTest, PairHostCanCompleteAScriptedHandshakeSuccessfully) {
    const network::PairingIdentity clientIdentity = network::create_pairing_identity();
    const network::PairingIdentity serverIdentity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(clientIdentity));
    ASSERT_TRUE(network::is_valid_pairing_identity(serverIdentity));

    const std::string pin = "1234";
    std::size_t callCount = 0U;
    std::string saltHex;
    ScopedHostPairingHttpTestHandler guard([&](const HostPairingHttpTestRequest &request, HostPairingHttpTestResponse *response, std::string *errorMessage, const std::atomic<bool> *) {
      return handle_successful_pairing_request(&callCount, &saltHex, pin, clientIdentity, serverIdentity, request, response, errorMessage);
    });

    const network::HostPairingResult result = network::pair_host({
      test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
      47989U,
      pin,
      "MoonlightXboxOG",
      clientIdentity,
    });

    EXPECT_EQ(callCount, 6U);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.alreadyPaired);
    EXPECT_EQ(result.message, "Paired successfully with " + std::string(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]));
  }

}  // namespace
