#include "src/network/host_pairing.h"

#include <gtest/gtest.h>

namespace {

  TEST(HostPairingTest, CreatesAValidClientIdentity) {
    std::string errorMessage;
    const network::PairingIdentity identity = network::create_pairing_identity(&errorMessage);

    EXPECT_TRUE(network::is_valid_pairing_identity(identity));
    EXPECT_TRUE(errorMessage.empty());
    EXPECT_EQ(identity.uniqueId.size(), 16U);
    EXPECT_NE(identity.certificatePem.find("BEGIN CERTIFICATE"), std::string::npos);
    EXPECT_NE(identity.privateKeyPem.find("BEGIN PRIVATE KEY"), std::string::npos);
  }

  TEST(HostPairingTest, ParsesServerInfoResponsesForPairing) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<appversion>7.1.431.0</appversion>"
      "<ExternalPort>47989</ExternalPort>"
      "<HttpsPort>47990</HttpsPort>"
      "<PairStatus>1</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, 47984, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.serverMajorVersion, 7);
    EXPECT_EQ(serverInfo.httpPort, 47989);
    EXPECT_EQ(serverInfo.httpsPort, 47990);
    EXPECT_TRUE(serverInfo.paired);
  }

  TEST(HostPairingTest, RejectsServerInfoResponsesMissingRequiredFields) {
    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    EXPECT_FALSE(network::parse_server_info_response("<root><HttpsPort>47990</HttpsPort></root>", 47984, &serverInfo, &errorMessage));
    EXPECT_FALSE(errorMessage.empty());
  }

}  // namespace


