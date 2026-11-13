/**
 * @file tests/unit/network/socket_bio_test.cpp
 * @brief Verify socket BIO retry behavior, ownership, and a real TLS exchange.
 */
#include "src/network/host_pairing.h"
#include "src/network/socket_bio.h"

#include <algorithm>
#include <cstring>
#include <lizardbyte/common/testing.h>
#include <memory>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <string>

namespace {

  /** @brief Deterministic socket behavior used to exercise BIO callbacks. */
  struct FakeSocket {
    int result = -1;  ///< Next read or write result.
    bool retry = false;  ///< Whether the last error can be retried.
    std::string sent;  ///< Last bytes passed to the send callback.
  };

  /**
   * @brief Build callbacks for a deterministic socket.
   * @param socket Caller-owned test socket.
   * @return Transport callbacks retaining the socket's address.
   */
  network::SocketBioTransport fake_transport(FakeSocket &socket) {
    return {
      &socket,
      [](void *context, char *buffer, int size) {
        const auto &socket = *static_cast<FakeSocket *>(context);
        if (socket.result > 0) {
          std::memset(buffer, 'a', std::min(size, socket.result));
        }
        return socket.result;
      },
      [](void *context, const char *buffer, int size) {
        auto &socket = *static_cast<FakeSocket *>(context);
        socket.sent.assign(buffer, size);
        return socket.result;
      },
      [](void *context) {
        return static_cast<FakeSocket *>(context)->retry;
      }
    };
  }

  TEST_F(BaseTest, SocketBioReportsRetryDirectionAndClearsItAfterSuccess) {
    FakeSocket socket;
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(network::make_socket_bio(fake_transport(socket)), BIO_free);
    ASSERT_NE(bio, nullptr);
    char buffer[8] {};
    socket.retry = true;
    EXPECT_EQ(BIO_read(bio.get(), buffer, sizeof(buffer)), -1);
    EXPECT_TRUE(BIO_should_read(bio.get()));
    EXPECT_EQ(BIO_write(bio.get(), "hello", 5), -1);
    EXPECT_TRUE(BIO_should_write(bio.get()));
    EXPECT_FALSE(BIO_should_read(bio.get()));
    socket.result = 3;
    EXPECT_EQ(BIO_read(bio.get(), buffer, sizeof(buffer)), 3);
    EXPECT_EQ(std::string(buffer, 3), "aaa");
    EXPECT_FALSE(BIO_should_retry(bio.get()));
    EXPECT_EQ(BIO_write(bio.get(), "hello", 5), 3);
    EXPECT_EQ(socket.sent, "hello");
    EXPECT_FALSE(BIO_should_retry(bio.get()));
  }

  TEST_F(BaseTest, SocketBioDistinguishesFatalErrorsFromEofAndRetainsCallerOwnership) {
    FakeSocket socket;
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(network::make_socket_bio(fake_transport(socket)), BIO_free);
    ASSERT_NE(bio, nullptr);
    char byte;
    EXPECT_EQ(BIO_read(bio.get(), &byte, 1), -1);
    EXPECT_FALSE(BIO_should_retry(bio.get()));
    EXPECT_FALSE(BIO_eof(bio.get()));
    socket.result = 0;
    EXPECT_EQ(BIO_read(bio.get(), &byte, 1), 0);
    EXPECT_TRUE(BIO_eof(bio.get()));
    EXPECT_EQ(BIO_flush(bio.get()), 1);
    EXPECT_EQ(BIO_set_close(bio.get(), BIO_CLOSE), 0);
    EXPECT_EQ(BIO_get_close(bio.get()), BIO_NOCLOSE);
    bio.reset();
    socket.sent = "still owned by caller";
    EXPECT_EQ(socket.sent, "still owned by caller");
    auto invalid = fake_transport(socket);
    invalid.receive = nullptr;
    EXPECT_EQ(network::make_socket_bio(invalid), nullptr);
  }

  TEST_F(BaseTest, SocketBioCompletesTlsHandshakeAndTransfersEncryptedData) {
    const auto identity = network::create_pairing_identity();
    ASSERT_FALSE(identity.certificatePem.empty());
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> serverContext(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> clientContext(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    ASSERT_NE(serverContext, nullptr);
    ASSERT_NE(clientContext, nullptr);
    std::unique_ptr<BIO, decltype(&BIO_free)> certInput(BIO_new_mem_buf(identity.certificatePem.data(), static_cast<int>(identity.certificatePem.size())), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> keyInput(BIO_new_mem_buf(identity.privateKeyPem.data(), static_cast<int>(identity.privateKeyPem.size())), BIO_free);
    std::unique_ptr<X509, decltype(&X509_free)> certificate(PEM_read_bio_X509(certInput.get(), nullptr, nullptr, nullptr), X509_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PrivateKey(keyInput.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    ASSERT_NE(certificate, nullptr);
    ASSERT_NE(key, nullptr);
    ASSERT_EQ(SSL_CTX_use_certificate(serverContext.get(), certificate.get()), 1);
    ASSERT_EQ(SSL_CTX_use_PrivateKey(serverContext.get(), key.get()), 1);
    std::unique_ptr<SSL, decltype(&SSL_free)> server(SSL_new(serverContext.get()), SSL_free);
    std::unique_ptr<SSL, decltype(&SSL_free)> client(SSL_new(clientContext.get()), SSL_free);
    ASSERT_NE(server, nullptr);
    ASSERT_NE(client, nullptr);
    BIO *clientEnd = nullptr;
    BIO *serverEnd = nullptr;
    ASSERT_EQ(BIO_new_bio_pair(&clientEnd, 0, &serverEnd, 0), 1);
    std::unique_ptr<BIO, decltype(&BIO_free)> transport(clientEnd, BIO_free);
    const network::SocketBioTransport callbacks {
      transport.get(),
      [](void *context, char *buffer, int size) {
        return BIO_read(static_cast<BIO *>(context), buffer, size);
      },
      [](void *context, const char *buffer, int size) {
        return BIO_write(static_cast<BIO *>(context), buffer, size);
      },
      [](void *context) {
        return BIO_should_retry(static_cast<BIO *>(context)) != 0;
      }
    };
    BIO *clientBio = network::make_socket_bio(callbacks);
    ASSERT_NE(clientBio, nullptr);
    int freed = 0;
    BIO_set_callback_arg(clientBio, reinterpret_cast<char *>(&freed));
    BIO_set_callback(clientBio, [](BIO *bio, int operation, const char *, int, long, long result) {
      if (operation == BIO_CB_FREE) {
        ++*reinterpret_cast<int *>(BIO_get_callback_arg(bio));
      }
      return result;
    });
    SSL_set_bio(client.get(), clientBio, clientBio);
    SSL_set_bio(server.get(), serverEnd, serverEnd);
    SSL_set_connect_state(client.get());
    SSL_set_accept_state(server.get());
    for (int step = 0; step < 32 && (!SSL_is_init_finished(client.get()) || !SSL_is_init_finished(server.get())); ++step) {
      for (SSL *peer : {client.get(), server.get()}) {
        const int result = SSL_do_handshake(peer);
        if (result != 1) {
          const int error = SSL_get_error(peer, result);
          ASSERT_TRUE(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) << error;
        }
      }
    }
    ASSERT_TRUE(SSL_is_init_finished(client.get()));
    ASSERT_TRUE(SSL_is_init_finished(server.get()));
    ASSERT_EQ(SSL_write(client.get(), "hello", 5), 5);
    char received[8] {};
    ASSERT_EQ(SSL_read(server.get(), received, sizeof(received)), 5);
    EXPECT_EQ(std::string(received, 5), "hello");
    client.reset();
    EXPECT_EQ(freed, 1);
    EXPECT_NE(BIO_get_data(transport.get()), nullptr);
  }
}  // namespace
