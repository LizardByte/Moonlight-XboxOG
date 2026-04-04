// class header include
#include "src/network/host_pairing.h"

// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// platform includes
#include <winsock2.h>
#include <windows.h>

// nxdk includes
#ifdef NXDK
#include <hal/debug.h>
#endif

#define OPENSSL_SUPPRESS_DEPRECATED

// lib includes
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

// local includes
#include "src/network/runtime_network.h"

#ifdef NXDK
#define _CRT_RAND_S
#endif

#ifdef NXDK
extern "C" int rand_s(unsigned int *randomValue);
#endif

namespace {

  void trace_pairing_phase(const char *message) {
#ifdef NXDK
    if (message != nullptr) {
      debugPrint("[PAIRING] %s\n", message);
    }
#else
    (void) message;
#endif
  }

  void trace_pairing_detail(const std::string &message) {
#ifdef NXDK
    debugPrint("[PAIRING] %s\n", message.c_str());
#else
    (void) message;
#endif
  }

  constexpr std::size_t UNIQUE_ID_BYTE_COUNT = 8;
  constexpr std::size_t CLIENT_CHALLENGE_BYTE_COUNT = 16;
  constexpr std::size_t CLIENT_SECRET_BYTE_COUNT = 16;
  constexpr int SOCKET_TIMEOUT_MILLISECONDS = 5000;
  constexpr uint16_t DEFAULT_SERVERINFO_HTTP_PORT = 47989;
  constexpr uint16_t FALLBACK_SERVERINFO_HTTP_PORT = 47984;

  struct WsaGuard {
    WsaGuard()
      : initialized(false) {
      WSADATA wsaData {};
      initialized = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }

    ~WsaGuard() {
      if (initialized) {
        WSACleanup();
      }
    }

    bool initialized;
  };

  struct SocketGuard {
    SocketGuard()
      : handle(INVALID_SOCKET) {
    }

    ~SocketGuard() {
      if (handle != INVALID_SOCKET) {
        closesocket(handle);
      }
    }

    SOCKET handle;
  };

  struct SslCtxDeleter {
    void operator()(SSL_CTX *context) const {
      if (context != nullptr) {
        SSL_CTX_free(context);
      }
    }
  };

  struct SslDeleter {
    void operator()(SSL *ssl) const {
      if (ssl != nullptr) {
        SSL_free(ssl);
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

  struct X509Deleter {
    void operator()(X509 *certificate) const {
      if (certificate != nullptr) {
        X509_free(certificate);
      }
    }
  };

  struct PkeyDeleter {
    void operator()(EVP_PKEY *key) const {
      if (key != nullptr) {
        EVP_PKEY_free(key);
      }
    }
  };

  struct RsaDeleter {
    void operator()(RSA *rsa) const {
      if (rsa != nullptr) {
        RSA_free(rsa);
      }
    }
  };

  struct BignumDeleter {
    void operator()(BIGNUM *value) const {
      if (value != nullptr) {
        BN_free(value);
      }
    }
  };

  struct MdCtxDeleter {
    void operator()(EVP_MD_CTX *context) const {
      if (context != nullptr) {
        EVP_MD_CTX_free(context);
      }
    }
  };

  struct CipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX *context) const {
      if (context != nullptr) {
        EVP_CIPHER_CTX_free(context);
      }
    }
  };

  struct HttpResponse {
    int statusCode;
    std::string body;
  };

  bool append_error(std::string *errorMessage, std::string message);
  bool hex_value(char character, unsigned char *value);
  bool http_get(
    const std::string &address,
    uint16_t port,
    std::string_view pathAndQuery,
    bool useTls,
    const network::PairingIdentity *tlsClientIdentity,
    std::string_view expectedTlsCertificatePem,
    HttpResponse *response,
    std::string *errorMessage
  );

  std::string summarize_http_payload_preview(std::string_view text) {
    constexpr std::size_t MAX_PREVIEW_BYTES = 48;

    if (text.empty()) {
      return "<empty>";
    }

    std::string preview;
    const std::size_t previewLength = std::min(text.size(), MAX_PREVIEW_BYTES);
    for (std::size_t index = 0; index < previewLength; ++index) {
      const unsigned char character = static_cast<unsigned char>(text[index]);
      if (character >= 0x20U && character <= 0x7EU) {
        preview.push_back(static_cast<char>(character));
      }
      else if (character == '\r') {
        preview += "\\r";
      }
      else if (character == '\n') {
        preview += "\\n";
      }
      else if (character == '\t') {
        preview += "\\t";
      }
      else {
        char buffer[5] = {};
        std::snprintf(buffer, sizeof(buffer), "\\x%02X", character);
        preview += buffer;
      }
    }

    if (text.size() > previewLength) {
      preview += "...";
    }

    return preview;
  }

  void append_unique_port(std::vector<uint16_t> *ports, uint16_t port) {
    if (ports == nullptr || port == 0) {
      return;
    }

    if (std::find(ports->begin(), ports->end(), port) == ports->end()) {
      ports->push_back(port);
    }
  }

  std::vector<uint16_t> build_serverinfo_port_candidates(uint16_t preferredHttpPort) {
    std::vector<uint16_t> ports;
    append_unique_port(&ports, preferredHttpPort == 0 ? DEFAULT_SERVERINFO_HTTP_PORT : preferredHttpPort);
    append_unique_port(&ports, DEFAULT_SERVERINFO_HTTP_PORT);
    append_unique_port(&ports, FALLBACK_SERVERINFO_HTTP_PORT);
    return ports;
  }

  std::string_view trim_ascii_whitespace(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
      text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
      text.remove_suffix(1);
    }
    return text;
  }

  bool ascii_iequals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
      return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
      if (std::tolower(static_cast<unsigned char>(left[index])) != std::tolower(static_cast<unsigned char>(right[index]))) {
        return false;
      }
    }

    return true;
  }

  bool header_value_contains_token(std::string_view value, std::string_view token) {
    std::size_t start = 0;
    while (start < value.size()) {
      const std::size_t end = value.find(',', start);
      const std::string_view item = trim_ascii_whitespace(value.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
      if (ascii_iequals(item, token)) {
        return true;
      }

      if (end == std::string_view::npos) {
        break;
      }
      start = end + 1;
    }

    return false;
  }

  bool try_parse_decimal_size(std::string_view text, std::size_t *value) {
    std::size_t parsedValue = 0;
    if (text.empty()) {
      return false;
    }

    for (char character : text) {
      if (character < '0' || character > '9') {
        return false;
      }
      parsedValue = (parsedValue * 10U) + static_cast<std::size_t>(character - '0');
    }

    if (value != nullptr) {
      *value = parsedValue;
    }
    return true;
  }

  bool try_parse_hex_size(std::string_view text, std::size_t *value) {
    std::size_t parsedValue = 0;
    if (text.empty()) {
      return false;
    }

    for (char character : text) {
      unsigned char digit = 0;
      if (!hex_value(character, &digit)) {
        return false;
      }
      parsedValue = (parsedValue * 16U) + digit;
    }

    if (value != nullptr) {
      *value = parsedValue;
    }
    return true;
  }

  bool try_parse_chunked_message_length(std::string_view responseText, std::size_t bodyStart, std::size_t *messageLength, std::string *errorMessage) {
    std::size_t cursor = bodyStart;
    while (true) {
      const std::size_t chunkLineEnd = responseText.find("\r\n", cursor);
      if (chunkLineEnd == std::string_view::npos) {
        return false;
      }

      std::string_view chunkSizeText = responseText.substr(cursor, chunkLineEnd - cursor);
      const std::size_t chunkExtensionSeparator = chunkSizeText.find(';');
      if (chunkExtensionSeparator != std::string_view::npos) {
        chunkSizeText = chunkSizeText.substr(0, chunkExtensionSeparator);
      }
      chunkSizeText = trim_ascii_whitespace(chunkSizeText);

      std::size_t chunkSize = 0;
      if (!try_parse_hex_size(chunkSizeText, &chunkSize)) {
        return append_error(errorMessage, "Received an invalid chunked HTTP response while pairing");
      }

      const std::size_t chunkDataStart = chunkLineEnd + 2;
      const std::size_t chunkDataEnd = chunkDataStart + chunkSize;
      if (responseText.size() < chunkDataEnd + 2) {
        return false;
      }
      if (responseText.substr(chunkDataEnd, 2) != "\r\n") {
        return append_error(errorMessage, "Received a malformed chunked HTTP response while pairing");
      }

      cursor = chunkDataEnd + 2;
      if (chunkSize == 0U) {
        if (messageLength != nullptr) {
          *messageLength = cursor;
        }
        return true;
      }
    }
  }

  bool try_get_http_response_length(std::string_view responseText, std::size_t *messageLength, std::string *errorMessage) {
    const std::size_t headerTerminator = responseText.find("\r\n\r\n");
    if (headerTerminator == std::string_view::npos) {
      return false;
    }

    bool hasContentLength = false;
    bool isChunked = false;
    std::size_t contentLength = 0;

    const std::size_t statusLineEnd = responseText.find("\r\n");
    std::size_t lineStart = statusLineEnd == std::string_view::npos ? headerTerminator : statusLineEnd + 2;
    while (lineStart < headerTerminator) {
      const std::size_t lineEnd = responseText.find("\r\n", lineStart);
      if (lineEnd == std::string_view::npos || lineEnd > headerTerminator) {
        break;
      }

      const std::string_view headerLine = responseText.substr(lineStart, lineEnd - lineStart);
      const std::size_t separator = headerLine.find(':');
      if (separator != std::string_view::npos) {
        const std::string_view headerName = trim_ascii_whitespace(headerLine.substr(0, separator));
        const std::string_view headerValue = trim_ascii_whitespace(headerLine.substr(separator + 1));

        if (ascii_iequals(headerName, "Content-Length")) {
          hasContentLength = try_parse_decimal_size(headerValue, &contentLength);
          if (!hasContentLength) {
            return append_error(errorMessage, "Received an invalid Content-Length header while pairing");
          }
        }
        else if (ascii_iequals(headerName, "Transfer-Encoding") && header_value_contains_token(headerValue, "chunked")) {
          isChunked = true;
        }
      }

      lineStart = lineEnd + 2;
    }

    const std::size_t bodyStart = headerTerminator + 4;
    if (isChunked) {
      return try_parse_chunked_message_length(responseText, bodyStart, messageLength, errorMessage);
    }
    if (hasContentLength) {
      const std::size_t completeLength = bodyStart + contentLength;
      if (responseText.size() < completeLength) {
        return false;
      }
      if (messageLength != nullptr) {
        *messageLength = completeLength;
      }
      return true;
    }

    return false;
  }

  bool append_error(std::string *errorMessage, std::string message) {
    if (errorMessage != nullptr) {
      *errorMessage = std::move(message);
    }

    return false;
  }

  std::string take_openssl_error_queue() {
    std::string details;
    unsigned long errorCode = 0;
    while ((errorCode = ERR_get_error()) != 0) {
      char errorBuffer[256] = {};
      ERR_error_string_n(errorCode, errorBuffer, sizeof(errorBuffer));
      if (!details.empty()) {
        details += "; ";
      }
      details += errorBuffer;
    }
    return details;
  }

  bool append_openssl_error(std::string *errorMessage, std::string message) {
    const std::string details = take_openssl_error_queue();
    if (!details.empty()) {
      message += ": " + details;
    }
    return append_error(errorMessage, std::move(message));
  }

  void initialize_openssl() {
    static bool initialized = false;
    if (!initialized) {
      SSL_library_init();
      SSL_load_error_strings();
      OpenSSL_add_all_algorithms();
      initialized = true;
    }
  }

#ifdef NXDK
  int nxdk_rand_seed(const void *, int) {
    return 1;
  }

  int nxdk_rand_bytes(unsigned char *buffer, int size) {
    if (buffer == nullptr || size < 0) {
      return 0;
    }

    int offset = 0;
    while (offset < size) {
      unsigned int randomWord = 0;
      if (::rand_s(&randomWord) != 0) {
        return 0;
      }

      const int chunkSize = std::min<int>(static_cast<int>(sizeof(randomWord)), size - offset);
      std::memcpy(buffer + offset, &randomWord, static_cast<std::size_t>(chunkSize));
      offset += chunkSize;
    }

    return 1;
  }

  void nxdk_rand_cleanup() {
  }

  int nxdk_rand_add(const void *, int, double) {
    return 1;
  }

  int nxdk_rand_status() {
    return 1;
  }

  RAND_METHOD g_nxdk_rand_method = {
    &nxdk_rand_seed,
    &nxdk_rand_bytes,
    &nxdk_rand_cleanup,
    &nxdk_rand_add,
    &nxdk_rand_bytes,
    &nxdk_rand_status,
  };

  bool ensure_nxdk_rand_method(std::string *errorMessage) {
    static bool configured = false;
    if (configured) {
      return true;
    }

    ERR_clear_error();
    if (RAND_set_rand_method(&g_nxdk_rand_method) != 1) {
      return append_openssl_error(errorMessage, "Failed to install the Xbox OpenSSL random source");
    }

    configured = true;
    take_openssl_error_queue();
    return true;
  }
#endif

  bool ensure_pairing_entropy(std::string *errorMessage) {
    initialize_openssl();

#ifdef NXDK
    return ensure_nxdk_rand_method(errorMessage);
#else

    if (RAND_status() == 1) {
      return true;
    }

    ERR_clear_error();
    RAND_poll();
    if (RAND_status() == 1) {
      take_openssl_error_queue();
      return true;
    }

    return append_openssl_error(errorMessage, "OpenSSL could not gather enough entropy for pairing");
#endif
  }

  bool fill_random_bytes(unsigned char *buffer, std::size_t size, std::string *errorMessage) {
    if (!ensure_pairing_entropy(errorMessage)) {
      return false;
    }

    ERR_clear_error();
    if (RAND_bytes(buffer, static_cast<int>(size)) == 1) {
      return true;
    }

    return append_openssl_error(errorMessage, "Failed to generate secure random bytes for pairing");
  }

  std::string hex_encode(const unsigned char *data, std::size_t size) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";

    std::string output;
    output.resize(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
      output[index * 2] = HEX_DIGITS[(data[index] >> 4) & 0x0F];
      output[(index * 2) + 1] = HEX_DIGITS[data[index] & 0x0F];
    }

    return output;
  }

  bool hex_value(char character, unsigned char *value) {
    if (character >= '0' && character <= '9') {
      *value = static_cast<unsigned char>(character - '0');
      return true;
    }
    if (character >= 'a' && character <= 'f') {
      *value = static_cast<unsigned char>(10 + (character - 'a'));
      return true;
    }
    if (character >= 'A' && character <= 'F') {
      *value = static_cast<unsigned char>(10 + (character - 'A'));
      return true;
    }

    return false;
  }

  bool hex_decode(std::string_view text, std::vector<unsigned char> *bytes, std::string *errorMessage) {
    if (text.size() % 2 != 0) {
      return append_error(errorMessage, "Expected an even number of hexadecimal characters");
    }

    std::vector<unsigned char> decoded;
    decoded.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
      unsigned char upper = 0;
      unsigned char lower = 0;
      if (!hex_value(text[index], &upper) || !hex_value(text[index + 1], &lower)) {
        return append_error(errorMessage, "Encountered invalid hexadecimal data during pairing");
      }
      decoded.push_back(static_cast<unsigned char>((upper << 4) | lower));
    }

    if (bytes != nullptr) {
      *bytes = std::move(decoded);
    }
    return true;
  }

  bool generate_unique_id(std::string *uniqueId, std::string *errorMessage) {
    std::array<unsigned char, UNIQUE_ID_BYTE_COUNT> bytes {};
    if (!fill_random_bytes(bytes.data(), bytes.size(), errorMessage)) {
      return false;
    }

    std::string generatedUniqueId = hex_encode(bytes.data(), bytes.size());
    std::transform(generatedUniqueId.begin(), generatedUniqueId.end(), generatedUniqueId.begin(), [](unsigned char character) {
      return static_cast<char>(std::toupper(character));
    });

    if (uniqueId != nullptr) {
      *uniqueId = std::move(generatedUniqueId);
    }
    return true;
  }

  bool generate_uuid(std::string *uuid, std::string *errorMessage) {
    std::array<unsigned char, 16> bytes {};
    if (!fill_random_bytes(bytes.data(), bytes.size(), errorMessage)) {
      return false;
    }

    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    std::string hex = hex_encode(bytes.data(), bytes.size());
    if (uuid != nullptr) {
      *uuid = hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" + hex.substr(20, 12);
    }
    return true;
  }

  std::string extract_pem(BIO *bio) {
    BUF_MEM *memory = nullptr;
    BIO_get_mem_ptr(bio, &memory);
    if (memory == nullptr || memory->data == nullptr || memory->length == 0) {
      return {};
    }

    return {memory->data, memory->length};
  }

  std::string x509_to_pem(X509 *certificate) {
    std::unique_ptr<BIO, BioDeleter> bio(BIO_new(BIO_s_mem()));
    if (bio == nullptr || PEM_write_bio_X509(bio.get(), certificate) != 1) {
      return {};
    }

    return extract_pem(bio.get());
  }

  std::string private_key_to_pem(EVP_PKEY *key) {
    std::unique_ptr<BIO, BioDeleter> bio(BIO_new(BIO_s_mem()));
    if (bio == nullptr || PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      return {};
    }

    return extract_pem(bio.get());
  }

  std::unique_ptr<X509, X509Deleter> load_certificate(std::string_view certificatePem) {
    std::unique_ptr<BIO, BioDeleter> bio(BIO_new_mem_buf(certificatePem.data(), static_cast<int>(certificatePem.size())));
    if (bio == nullptr) {
      return nullptr;
    }

    return std::unique_ptr<X509, X509Deleter>(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  }

  std::unique_ptr<EVP_PKEY, PkeyDeleter> load_private_key(std::string_view privateKeyPem) {
    std::unique_ptr<BIO, BioDeleter> bio(BIO_new_mem_buf(privateKeyPem.data(), static_cast<int>(privateKeyPem.size())));
    if (bio == nullptr) {
      return nullptr;
    }

    return std::unique_ptr<EVP_PKEY, PkeyDeleter>(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  }

  bool configure_tls_pairing_identity(SSL_CTX *context, const network::PairingIdentity &identity, std::string *errorMessage) {
    if (context == nullptr) {
      return append_error(errorMessage, "Failed to initialize the TLS client identity for pairing");
    }

    std::unique_ptr<X509, X509Deleter> certificate = load_certificate(identity.certificatePem);
    if (certificate == nullptr) {
      return append_openssl_error(errorMessage, "Failed to load the client pairing certificate for TLS");
    }

    std::unique_ptr<EVP_PKEY, PkeyDeleter> privateKey = load_private_key(identity.privateKeyPem);
    if (privateKey == nullptr) {
      return append_openssl_error(errorMessage, "Failed to load the client pairing private key for TLS");
    }

    ERR_clear_error();
    if (SSL_CTX_use_certificate(context, certificate.get()) != 1) {
      return append_openssl_error(errorMessage, "Failed to attach the client pairing certificate to TLS");
    }
    if (SSL_CTX_use_PrivateKey(context, privateKey.get()) != 1) {
      return append_openssl_error(errorMessage, "Failed to attach the client pairing private key to TLS");
    }
    if (SSL_CTX_check_private_key(context) != 1) {
      return append_openssl_error(errorMessage, "The client pairing certificate and private key do not match");
    }

    return true;
  }

  bool verify_tls_peer_certificate(SSL *ssl, std::string_view expectedCertificatePem, std::string *errorMessage) {
    if (ssl == nullptr || expectedCertificatePem.empty()) {
      return true;
    }

    std::unique_ptr<X509, X509Deleter> expectedCertificate = load_certificate(expectedCertificatePem);
    if (expectedCertificate == nullptr) {
      return append_error(errorMessage, "The saved host pairing certificate was invalid before the TLS verification step");
    }

    std::unique_ptr<X509, X509Deleter> peerCertificate(SSL_get_peer_certificate(ssl));
    if (peerCertificate == nullptr) {
      return append_error(errorMessage, "The host did not present a TLS certificate during pairing");
    }

    if (X509_cmp(peerCertificate.get(), expectedCertificate.get()) != 0) {
      return append_error(errorMessage, "The host presented an unexpected TLS certificate during pairing");
    }

    return true;
  }

  bool create_self_signed_certificate(network::PairingIdentity *identity, std::string *errorMessage) {
    if (!ensure_pairing_entropy(errorMessage)) {
      return false;
    }

    ERR_clear_error();

#ifdef NXDK
    std::unique_ptr<BIGNUM, BignumDeleter> exponent(BN_new());
    if (exponent == nullptr || BN_set_word(exponent.get(), RSA_F4) != 1) {
      return append_openssl_error(errorMessage, "Failed to initialize the client key generation exponent for pairing");
    }

    std::unique_ptr<RSA, RsaDeleter> rsa(RSA_new());
    if (rsa == nullptr || RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr) != 1) {
      return append_openssl_error(errorMessage, "Failed to generate the client key used for pairing");
    }

    EVP_PKEY *rawKey = EVP_PKEY_new();
    if (rawKey == nullptr) {
      return append_openssl_error(errorMessage, "Failed to allocate the client key container used for pairing");
    }

    std::unique_ptr<EVP_PKEY, PkeyDeleter> key(rawKey);
    if (EVP_PKEY_assign_RSA(key.get(), rsa.release()) != 1) {
      return append_openssl_error(errorMessage, "Failed to attach the generated client RSA key for pairing");
    }
#else
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> keyContext(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), &EVP_PKEY_CTX_free);
    if (keyContext == nullptr || EVP_PKEY_keygen_init(keyContext.get()) != 1 || EVP_PKEY_CTX_set_rsa_keygen_bits(keyContext.get(), 2048) != 1) {
      return append_openssl_error(errorMessage, "Failed to initialize client key generation for pairing");
    }

    EVP_PKEY *rawKey = nullptr;
    if (EVP_PKEY_keygen(keyContext.get(), &rawKey) != 1 || rawKey == nullptr) {
      return append_openssl_error(errorMessage, "Failed to generate the client key used for pairing");
    }
    std::unique_ptr<EVP_PKEY, PkeyDeleter> key(rawKey);
#endif

    std::unique_ptr<X509, X509Deleter> certificate(X509_new());
    if (certificate == nullptr) {
      return append_openssl_error(errorMessage, "Failed to allocate the client certificate used for pairing");
    }

    X509_set_version(certificate.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1);
    X509_gmtime_adj(X509_get_notBefore(certificate.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(certificate.get()), 60L * 60L * 24L * 365L * 10L);
    X509_set_pubkey(certificate.get(), key.get());

    X509_NAME *subject = X509_get_subject_name(certificate.get());
    if (subject == nullptr) {
      return append_openssl_error(errorMessage, "Failed to populate the client certificate subject for pairing");
    }

    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("NVIDIA GameStream Client"), -1, -1, 0);
    X509_set_issuer_name(certificate.get(), subject);

    if (X509_sign(certificate.get(), key.get(), EVP_sha256()) == 0) {
      return append_openssl_error(errorMessage, "Failed to sign the client certificate used for pairing");
    }

    identity->certificatePem = x509_to_pem(certificate.get());
    identity->privateKeyPem = private_key_to_pem(key.get());
    if (identity->certificatePem.empty() || identity->privateKeyPem.empty()) {
      return append_openssl_error(errorMessage, "Failed to serialize the client pairing certificate or private key");
    }

    return true;
  }

  std::string certificate_hex(std::string_view certificatePem) {
    return hex_encode(reinterpret_cast<const unsigned char *>(certificatePem.data()), certificatePem.size());
  }

  bool extract_xml_tag_value(std::string_view xml, std::string_view tagName, std::string *value) {
    const std::string openTag = "<" + std::string(tagName) + ">";
    const std::string closeTag = "</" + std::string(tagName) + ">";
    const std::size_t openIndex = xml.find(openTag);
    if (openIndex == std::string_view::npos) {
      return false;
    }

    const std::size_t contentStart = openIndex + openTag.size();
    const std::size_t closeIndex = xml.find(closeTag, contentStart);
    if (closeIndex == std::string_view::npos) {
      return false;
    }

    if (value != nullptr) {
      *value = std::string(xml.substr(contentStart, closeIndex - contentStart));
    }
    return true;
  }

  bool try_parse_port(std::string_view text, uint16_t *port) {
    if (text.empty()) {
      return false;
    }

    unsigned long value = 0;
    for (char character : text) {
      if (character < '0' || character > '9') {
        return false;
      }
      value = (value * 10UL) + static_cast<unsigned long>(character - '0');
      if (value > 65535UL) {
        return false;
      }
    }

    if (port != nullptr) {
      *port = static_cast<uint16_t>(value);
    }
    return value != 0;
  }

  bool connect_socket(const std::string &address, uint16_t port, SocketGuard *socketGuard, std::string *errorMessage) {
    if (socketGuard == nullptr) {
      return append_error(errorMessage, "Internal pairing error while preparing the host connection");
    }

    trace_pairing_phase("checking runtime network state");
    if (!network::runtime_network_ready()) {
      return append_error(errorMessage, network::runtime_network_status().summary);
    }

    trace_pairing_phase("creating socket");
    socketGuard->handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketGuard->handle == INVALID_SOCKET) {
      return append_error(errorMessage, "Failed to create the host pairing socket");
    }
    trace_pairing_phase("socket created");

    trace_pairing_phase("preparing IPv4 socket address");
    sockaddr_in socketAddress {};
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(port);
    socketAddress.sin_addr.s_addr = inet_addr(address.c_str());
    if (socketAddress.sin_addr.s_addr == INADDR_NONE && address != "255.255.255.255") {
      return append_error(errorMessage, "Pairing currently requires a dotted IPv4 host address");
    }
    trace_pairing_phase("IPv4 socket address ready");

    u_long nonBlockingMode = 1;
    trace_pairing_phase("setting non-blocking connect mode");
    if (ioctlsocket(socketGuard->handle, FIONBIO, &nonBlockingMode) != 0) {
      return append_error(errorMessage, "Failed to configure the host pairing socket for a timed connect (Winsock error " + std::to_string(WSAGetLastError()) + ")");
    }

    trace_pairing_detail("connecting to " + address + ":" + std::to_string(port));
    const int connectResult = connect(socketGuard->handle, reinterpret_cast<const sockaddr *>(&socketAddress), sizeof(socketAddress));
    if (connectResult == SOCKET_ERROR) {
      const int connectError = WSAGetLastError();
      if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS && connectError != WSAEALREADY) {
        return append_error(errorMessage, "Failed to connect to the host pairing endpoint at " + address + ":" + std::to_string(port) + " (Winsock error " + std::to_string(connectError) + ")");
      }

      fd_set writeSet;
      FD_ZERO(&writeSet);
      FD_SET(socketGuard->handle, &writeSet);
      timeval timeout {
        SOCKET_TIMEOUT_MILLISECONDS / 1000,
        (SOCKET_TIMEOUT_MILLISECONDS % 1000) * 1000,
      };

      trace_pairing_phase("waiting for timed connect completion");
      const int selectResult = select(static_cast<int>(socketGuard->handle) + 1, nullptr, &writeSet, nullptr, &timeout);
      if (selectResult <= 0) {
        return append_error(errorMessage, selectResult == 0
          ? "Timed out connecting to the host pairing endpoint at " + address + ":" + std::to_string(port)
          : "Connection test failed while waiting for the host pairing endpoint at " + address + ":" + std::to_string(port));
      }

      int socketError = 0;
#ifdef NXDK
      socklen_t socketErrorLength = sizeof(socketError);
#else
      int socketErrorLength = sizeof(socketError);
#endif
      if (getsockopt(socketGuard->handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socketError), &socketErrorLength) != 0) {
        return append_error(errorMessage, "Failed to query the host pairing socket status after connect (Winsock error " + std::to_string(WSAGetLastError()) + ")");
      }
      if (socketError != 0) {
        return append_error(errorMessage, "Host refused the pairing connection on " + address + ":" + std::to_string(port) + " (Winsock error " + std::to_string(socketError) + ")");
      }
    }

    nonBlockingMode = 0;
    trace_pairing_phase("restoring blocking mode after connect");
    if (ioctlsocket(socketGuard->handle, FIONBIO, &nonBlockingMode) != 0) {
      return append_error(errorMessage, "Failed to restore the host pairing socket to blocking mode after connect (Winsock error " + std::to_string(WSAGetLastError()) + ")");
    }

    const DWORD timeoutMilliseconds = SOCKET_TIMEOUT_MILLISECONDS;
    setsockopt(socketGuard->handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
    setsockopt(socketGuard->handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
    trace_pairing_phase("socket connected");

    return true;
  }

  bool recv_all_plain(SOCKET socketHandle, std::string *response, std::string *errorMessage) {
    std::string received;
    char buffer[4096] = {};
    std::size_t completeLength = 0;

    while (true) {
      const int bytesRead = recv(socketHandle, buffer, sizeof(buffer), 0);
      if (bytesRead == 0) {
        break;
      }
      if (bytesRead < 0) {
        const int socketError = WSAGetLastError();
        return append_error(errorMessage, socketError == WSAETIMEDOUT
          ? "Timed out while reading the host pairing response"
          : "Failed while reading the host pairing response (Winsock error " + std::to_string(socketError) + ")");
      }
      received.append(buffer, buffer + bytesRead);

      std::string framingError;
      if (try_get_http_response_length(received, &completeLength, &framingError)) {
        received.resize(completeLength);
        break;
      }
      if (!framingError.empty()) {
        return append_error(errorMessage, framingError);
      }
    }

    if (response != nullptr) {
      *response = std::move(received);
    }
    return true;
  }

  bool recv_all_ssl(SSL *ssl, std::string *response, std::string *errorMessage) {
    std::string received;
    char buffer[4096] = {};
    std::size_t completeLength = 0;

    while (true) {
      const int bytesRead = SSL_read(ssl, buffer, sizeof(buffer));
      if (bytesRead == 0) {
        break;
      }
      if (bytesRead < 0) {
        const int errorCode = SSL_get_error(ssl, bytesRead);
        if (errorCode == SSL_ERROR_ZERO_RETURN) {
          break;
        }
        if (errorCode == SSL_ERROR_WANT_READ || errorCode == SSL_ERROR_WANT_WRITE) {
          continue;
        }
        return append_openssl_error(errorMessage, errorCode == SSL_ERROR_SYSCALL && WSAGetLastError() == WSAETIMEDOUT
          ? "Timed out while reading the encrypted host pairing response"
          : "Failed while reading the encrypted host pairing response");
      }
      received.append(buffer, buffer + bytesRead);

      std::string framingError;
      if (try_get_http_response_length(received, &completeLength, &framingError)) {
        received.resize(completeLength);
        break;
      }
      if (!framingError.empty()) {
        return append_error(errorMessage, framingError);
      }
    }

    if (response != nullptr) {
      *response = std::move(received);
    }
    return true;
  }

  bool send_all_plain(SOCKET socketHandle, std::string_view request, std::string *errorMessage) {
    std::size_t sent = 0;
    while (sent < request.size()) {
      const int bytesSent = send(socketHandle, request.data() + sent, static_cast<int>(request.size() - sent), 0);
      if (bytesSent <= 0) {
        return append_error(errorMessage, "Failed to send the host pairing request (Winsock error " + std::to_string(WSAGetLastError()) + ")");
      }
      sent += static_cast<std::size_t>(bytesSent);
    }

    return true;
  }

  bool send_all_ssl(SSL *ssl, std::string_view request, std::string *errorMessage) {
    std::size_t sent = 0;
    while (sent < request.size()) {
      const int bytesSent = SSL_write(ssl, request.data() + sent, static_cast<int>(request.size() - sent));
      if (bytesSent <= 0) {
        return append_openssl_error(errorMessage, "Failed to send the encrypted host pairing request");
      }
      sent += static_cast<std::size_t>(bytesSent);
    }

    return true;
  }

  bool parse_http_response(std::string_view responseText, HttpResponse *response, std::string *errorMessage) {
    if (responseText.empty()) {
      return append_error(errorMessage, "The host closed the connection without returning an HTTP response while pairing");
    }

    const std::size_t lineEnd = responseText.find("\r\n");
    if (lineEnd == std::string_view::npos) {
      return append_error(errorMessage, "Received a non-HTTP response while pairing (first bytes: " + summarize_http_payload_preview(responseText) + ")");
    }

    const std::string_view statusLine = responseText.substr(0, lineEnd);
    const std::size_t firstSpace = statusLine.find(' ');
    if (firstSpace == std::string_view::npos || firstSpace + 4 > statusLine.size()) {
      return append_error(errorMessage, "Received an invalid HTTP status line while pairing");
    }

    const int statusCode = std::atoi(std::string(statusLine.substr(firstSpace + 1, 3)).c_str());
    const std::size_t bodyStart = responseText.find("\r\n\r\n");
    if (bodyStart == std::string_view::npos) {
      return append_error(errorMessage, "Received an incomplete HTTP response while pairing");
    }

    if (response != nullptr) {
      response->statusCode = statusCode;
      response->body = std::string(responseText.substr(bodyStart + 4));
    }
    return true;
  }

  bool query_server_info_internal(const std::string &address, uint16_t preferredHttpPort, HttpResponse *response, network::HostPairingServerInfo *serverInfo, std::string *errorMessage) {
    if (address.empty()) {
      return append_error(errorMessage, "Pairing requires a valid host address");
    }

    const std::vector<uint16_t> candidatePorts = build_serverinfo_port_candidates(preferredHttpPort);
    const std::string serverInfoPath = "/serverinfo?uniqueid=0123456789ABCDEF&uuid=11111111-2222-3333-4444-555555555555";
    std::vector<std::string> attemptFailures;

    for (uint16_t candidatePort : candidatePorts) {
      trace_pairing_detail("query_server_info: trying port " + std::to_string(candidatePort));

      HttpResponse candidateResponse {};
      std::string attemptError;
      if (!http_get(address, candidatePort, serverInfoPath, false, nullptr, {}, &candidateResponse, &attemptError)) {
        attemptFailures.push_back(std::to_string(candidatePort) + ": " + attemptError);
        continue;
      }

      network::HostPairingServerInfo candidateServerInfo {};
      if (!network::parse_server_info_response(candidateResponse.body, candidatePort, &candidateServerInfo, &attemptError)) {
        attemptFailures.push_back(std::to_string(candidatePort) + ": " + attemptError);
        continue;
      }

      if (response != nullptr) {
        *response = std::move(candidateResponse);
      }
      if (serverInfo != nullptr) {
        *serverInfo = candidateServerInfo;
      }
      return true;
    }

    std::string combinedMessage = "Failed to query /serverinfo from " + address;
    if (!attemptFailures.empty()) {
      combinedMessage += " (";
      for (std::size_t index = 0; index < attemptFailures.size(); ++index) {
        if (index > 0) {
          combinedMessage += "; ";
        }
        combinedMessage += attemptFailures[index];
      }
      combinedMessage += ")";
    }
    return append_error(errorMessage, std::move(combinedMessage));
  }

  bool http_get(
    const std::string &address,
    uint16_t port,
    std::string_view pathAndQuery,
    bool useTls,
    const network::PairingIdentity *tlsClientIdentity,
    std::string_view expectedTlsCertificatePem,
    HttpResponse *response,
    std::string *errorMessage
  ) {
    trace_pairing_phase("http_get: WSAStartup");
    WsaGuard wsaGuard;
    if (!wsaGuard.initialized) {
      return append_error(errorMessage, "Failed to initialize Winsock for host pairing");
    }

    SocketGuard socketGuard;
    trace_pairing_phase("http_get: connect_socket");
    if (!connect_socket(address, port, &socketGuard, errorMessage)) {
      return false;
    }

    const std::string request =
      "GET " + std::string(pathAndQuery) + " HTTP/1.1\r\n"
      "Host: " + address + ":" + std::to_string(port) + "\r\n"
      "User-Agent: Moonlight-XboxOG\r\n"
      "Connection: close\r\n\r\n";

    std::string rawResponse;
    if (!useTls) {
      trace_pairing_phase("http_get: sending plain request");
      if (!send_all_plain(socketGuard.handle, request, errorMessage) || !recv_all_plain(socketGuard.handle, &rawResponse, errorMessage)) {
        return false;
      }
    }
    else {
      trace_pairing_phase("http_get: preparing TLS");
      initialize_openssl();
      std::unique_ptr<SSL_CTX, SslCtxDeleter> context(SSL_CTX_new(TLS_client_method()));
      if (context == nullptr) {
        return append_openssl_error(errorMessage, "Failed to create the TLS context for host pairing");
      }

      SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
      if (tlsClientIdentity != nullptr && !configure_tls_pairing_identity(context.get(), *tlsClientIdentity, errorMessage)) {
        return false;
      }

      std::unique_ptr<SSL, SslDeleter> ssl(SSL_new(context.get()));
      if (ssl == nullptr) {
        return append_openssl_error(errorMessage, "Failed to create the TLS session for host pairing");
      }

#ifdef NXDK
      std::unique_ptr<BIO, BioDeleter> socketBio(BIO_new_fd(static_cast<int>(socketGuard.handle), BIO_NOCLOSE));
      if (socketBio == nullptr || BIO_up_ref(socketBio.get()) != 1) {
        return append_openssl_error(errorMessage, "Failed to attach the host pairing socket to TLS");
      }

      SSL_set_bio(ssl.get(), socketBio.get(), socketBio.get());
      socketBio.release();
#else
      if (SSL_set_fd(ssl.get(), static_cast<int>(socketGuard.handle)) != 1) {
        return append_openssl_error(errorMessage, "Failed to attach the host pairing socket to TLS");
      }
#endif
      ERR_clear_error();
      trace_pairing_phase("http_get: SSL_connect");
      if (SSL_connect(ssl.get()) != 1) {
        return append_openssl_error(errorMessage, "Failed to establish the encrypted host pairing session");
      }
      if (!verify_tls_peer_certificate(ssl.get(), expectedTlsCertificatePem, errorMessage)) {
        return false;
      }

      trace_pairing_phase("http_get: sending TLS request");
      if (!send_all_ssl(ssl.get(), request, errorMessage) || !recv_all_ssl(ssl.get(), &rawResponse, errorMessage)) {
        return false;
      }
    }

    trace_pairing_phase("http_get: parsing HTTP response");
    return parse_http_response(rawResponse, response, errorMessage);
  }

  const EVP_MD *pairing_digest(int serverMajorVersion) {
    return serverMajorVersion >= 7 ? EVP_sha256() : EVP_sha1();
  }

  std::size_t pairing_hash_length(int serverMajorVersion) {
    return serverMajorVersion >= 7 ? 32U : 20U;
  }

  bool compute_digest(const unsigned char *data, std::size_t size, int serverMajorVersion, std::vector<unsigned char> *digest, std::string *errorMessage) {
    unsigned int digestLength = 0;
    std::vector<unsigned char> output(EVP_MAX_MD_SIZE);
    if (EVP_Digest(data, size, output.data(), &digestLength, pairing_digest(serverMajorVersion), nullptr) != 1) {
      return append_error(errorMessage, "Failed to compute the host pairing digest");
    }

    output.resize(digestLength);
    if (digest != nullptr) {
      *digest = std::move(output);
    }
    return true;
  }

  bool aes_ecb_encrypt(const unsigned char *plaintext, std::size_t size, const std::vector<unsigned char> &key, std::vector<unsigned char> *ciphertext, std::string *errorMessage) {
    std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter> context(EVP_CIPHER_CTX_new());
    if (context == nullptr) {
      return append_error(errorMessage, "Failed to initialize the pairing cipher");
    }

    if (EVP_EncryptInit_ex(context.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1 || EVP_CIPHER_CTX_set_padding(context.get(), 0) != 1) {
      return append_error(errorMessage, "Failed to configure the pairing cipher");
    }

    std::vector<unsigned char> output(size + 16U);
    int outputLength = 0;
    if (EVP_EncryptUpdate(context.get(), output.data(), &outputLength, plaintext, static_cast<int>(size)) != 1) {
      return append_error(errorMessage, "Failed to encrypt the pairing payload");
    }

    output.resize(static_cast<std::size_t>(outputLength));
    if (ciphertext != nullptr) {
      *ciphertext = std::move(output);
    }
    return true;
  }

  bool aes_ecb_decrypt(const std::vector<unsigned char> &ciphertext, const std::vector<unsigned char> &key, std::vector<unsigned char> *plaintext, std::string *errorMessage) {
    std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter> context(EVP_CIPHER_CTX_new());
    if (context == nullptr) {
      return append_error(errorMessage, "Failed to initialize the pairing decipher");
    }

    if (EVP_DecryptInit_ex(context.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1 || EVP_CIPHER_CTX_set_padding(context.get(), 0) != 1) {
      return append_error(errorMessage, "Failed to configure the pairing decipher");
    }

    std::vector<unsigned char> output(ciphertext.size());
    int outputLength = 0;
    if (EVP_DecryptUpdate(context.get(), output.data(), &outputLength, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
      return append_error(errorMessage, "Failed to decrypt the pairing payload");
    }

    output.resize(static_cast<std::size_t>(outputLength));
    if (plaintext != nullptr) {
      *plaintext = std::move(output);
    }
    return true;
  }

  bool sign_sha256(const std::vector<unsigned char> &data, EVP_PKEY *privateKey, std::vector<unsigned char> *signature, std::string *errorMessage) {
    std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
    if (context == nullptr || EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, privateKey) != 1 || EVP_DigestSignUpdate(context.get(), data.data(), data.size()) != 1) {
      return append_error(errorMessage, "Failed to initialize the pairing signature");
    }

    std::size_t signatureLength = 0;
    if (EVP_DigestSignFinal(context.get(), nullptr, &signatureLength) != 1 || signatureLength == 0U) {
      return append_error(errorMessage, "Failed to size the pairing signature");
    }

    std::vector<unsigned char> generatedSignature(signatureLength);
    if (EVP_DigestSignFinal(context.get(), generatedSignature.data(), &signatureLength) != 1) {
      return append_error(errorMessage, "Failed to generate the pairing signature");
    }

    generatedSignature.resize(signatureLength);
    if (signature != nullptr) {
      *signature = std::move(generatedSignature);
    }
    return true;
  }

  bool verify_sha256_signature(const std::vector<unsigned char> &data, const std::vector<unsigned char> &signature, X509 *certificate, std::string *errorMessage) {
    std::unique_ptr<EVP_PKEY, PkeyDeleter> publicKey(X509_get_pubkey(certificate));
    std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> context(EVP_MD_CTX_new());
    if (publicKey == nullptr || context == nullptr || EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, publicKey.get()) != 1 || EVP_DigestVerifyUpdate(context.get(), data.data(), data.size()) != 1) {
      return append_error(errorMessage, "Failed to initialize server certificate verification for pairing");
    }

    if (EVP_DigestVerifyFinal(context.get(), signature.data(), signature.size()) != 1) {
      return append_error(errorMessage, "Host pairing verification failed");
    }
    return true;
  }

  bool derive_aes_key(std::string_view saltHex, std::string_view pin, int serverMajorVersion, std::vector<unsigned char> *key, std::string *errorMessage) {
    std::vector<unsigned char> saltBytes;
    if (!hex_decode(saltHex, &saltBytes, errorMessage) || saltBytes.size() != 16U || pin.size() != 4U) {
      return append_error(errorMessage, "The host pairing salt or PIN was invalid");
    }

    std::vector<unsigned char> combined;
    combined.reserve(saltBytes.size() + pin.size());
    combined.insert(combined.end(), saltBytes.begin(), saltBytes.end());
    combined.insert(combined.end(), pin.begin(), pin.end());
    return compute_digest(combined.data(), combined.size(), serverMajorVersion, key, errorMessage);
  }

  bool parse_pairing_tag(const HttpResponse &response, std::string_view tagName, std::string *value, std::string *errorMessage) {
    if (response.statusCode != 200) {
      return append_error(errorMessage, "The host returned HTTP " + std::to_string(response.statusCode) + " during pairing");
    }

    if (!extract_xml_tag_value(response.body, tagName, value)) {
      return append_error(errorMessage, "The host returned an unexpected pairing response");
    }
    return true;
  }

  bool load_certificate_signature(X509 *certificate, std::vector<unsigned char> *signature, std::string *errorMessage) {
    const ASN1_BIT_STRING *asn1Signature = nullptr;
    X509_get0_signature(&asn1Signature, nullptr, certificate);
    if (asn1Signature == nullptr || asn1Signature->data == nullptr || asn1Signature->length <= 0) {
      return append_error(errorMessage, "Failed to read the client certificate signature used for pairing");
    }

    if (signature != nullptr) {
      signature->assign(asn1Signature->data, asn1Signature->data + asn1Signature->length);
    }
    return true;
  }

}  // namespace

namespace network {

  bool is_valid_pairing_identity(const PairingIdentity &identity) {
    return !identity.uniqueId.empty() && load_certificate(identity.certificatePem) != nullptr && load_private_key(identity.privateKeyPem) != nullptr;
  }

  PairingIdentity create_pairing_identity(std::string *errorMessage) {
    PairingIdentity identity {};
    if (errorMessage != nullptr) {
      errorMessage->clear();
    }

    if (!generate_unique_id(&identity.uniqueId, errorMessage)) {
      return identity;
    }

    create_self_signed_certificate(&identity, errorMessage);
    return identity;
  }

  bool parse_server_info_response(std::string_view xml, uint16_t fallbackHttpPort, HostPairingServerInfo *serverInfo, std::string *errorMessage) {
    std::string appVersion;
    std::string httpPortText;
    std::string httpsPortText;
    std::string pairStatus;
    if (!extract_xml_tag_value(xml, "appversion", &appVersion) || !extract_xml_tag_value(xml, "PairStatus", &pairStatus)) {
      return append_error(errorMessage, "The host serverinfo response was missing required pairing fields");
    }

    uint16_t httpPort = fallbackHttpPort;
    if (extract_xml_tag_value(xml, "ExternalPort", &httpPortText) || extract_xml_tag_value(xml, "HttpPort", &httpPortText)) {
      try_parse_port(httpPortText, &httpPort);
    }

    uint16_t httpsPort = fallbackHttpPort;
    if (extract_xml_tag_value(xml, "HttpsPort", &httpsPortText)) {
      try_parse_port(httpsPortText, &httpsPort);
    }

    if (serverInfo != nullptr) {
      serverInfo->serverMajorVersion = std::atoi(appVersion.c_str());
      serverInfo->httpPort = httpPort == 0 ? fallbackHttpPort : httpPort;
      serverInfo->httpsPort = httpsPort == 0 ? fallbackHttpPort : httpsPort;
      serverInfo->paired = pairStatus == "1";
    }
    return true;
  }

  bool query_server_info(const std::string &address, uint16_t preferredHttpPort, HostPairingServerInfo *serverInfo, std::string *errorMessage) {
    return query_server_info_internal(address, preferredHttpPort, nullptr, serverInfo, errorMessage);
  }

  HostPairingResult pair_host(const HostPairingRequest &request) {
    trace_pairing_phase("pair_host entered");
    HostPairingResult result {false, false, "Pairing failed"};
    auto fail_with_phase = [&result](std::string_view phase, const std::string &message) {
      result.message = "Pairing failed during " + std::string(phase) + ": " + message;
      trace_pairing_detail(result.message);
      return result;
    };
    auto next_pairing_uuid = [&result](std::string *uuid, std::string *errorMessage) {
      if (generate_uuid(uuid, errorMessage)) {
        return true;
      }

      result.message = errorMessage != nullptr && !errorMessage->empty()
        ? *errorMessage
        : "Failed to generate the UUID used for pairing";
      return false;
    };

    if (request.address.empty()) {
      result.message = "Pairing requires a valid host address";
      return result;
    }
    if (request.pin.size() != 4U) {
      result.message = "Pairing requires a four-digit PIN";
      return result;
    }
    if (!is_valid_pairing_identity(request.identity)) {
      result.message = "Client pairing identity is missing or invalid";
      return result;
    }

    std::unique_ptr<X509, X509Deleter> clientCertificate = load_certificate(request.identity.certificatePem);
    std::unique_ptr<EVP_PKEY, PkeyDeleter> clientPrivateKey = load_private_key(request.identity.privateKeyPem);
    if (clientCertificate == nullptr || clientPrivateKey == nullptr) {
      result.message = "Client pairing credentials could not be loaded";
      return result;
    }

    const uint16_t httpPort = request.httpPort == 0 ? DEFAULT_SERVERINFO_HTTP_PORT : request.httpPort;
    const std::string uniqueId = request.identity.uniqueId;
    const std::string deviceName = request.deviceName.empty() ? "MoonlightXboxOG" : request.deviceName;

    std::string errorMessage;
    HttpResponse response {};
    std::string requestUuid;
    if (!next_pairing_uuid(&requestUuid, &errorMessage)) {
      return result;
    }

    trace_pairing_phase("requesting /serverinfo");
    HostPairingServerInfo serverInfo {};
    if (!query_server_info_internal(request.address, httpPort, &response, &serverInfo, &errorMessage)) {
      return fail_with_phase("serverinfo", errorMessage);
    }

    if (serverInfo.paired) {
      result.success = true;
      result.alreadyPaired = true;
      result.message = "The host already reports this client as paired";
      return result;
    }

    std::array<unsigned char, 16> saltBytes {};
    if (!fill_random_bytes(saltBytes.data(), saltBytes.size(), &errorMessage)) {
      result.message = errorMessage;
      return result;
    }
    const std::string saltHex = hex_encode(saltBytes.data(), saltBytes.size());
    const std::string certHex = certificate_hex(request.identity.certificatePem);

    std::string phaseValue;
    if (!next_pairing_uuid(&requestUuid, &errorMessage)) {
      return result;
    }
    const std::string phase1Path = "/pair?uniqueid=" + uniqueId + "&uuid=" + requestUuid + "&devicename=" + deviceName + "&updateState=1&phrase=getservercert&salt=" + saltHex + "&clientcert=" + certHex;
    trace_pairing_phase("phase 1 getservercert request");
    if (!http_get(request.address, serverInfo.httpPort, phase1Path, false, nullptr, {}, &response, &errorMessage) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 1 (getservercert)", errorMessage);
    }
    if (phaseValue != "1") {
      result.message = "The host rejected the initial pairing request";
      return result;
    }

    std::string plainCertHex;
    if (!parse_pairing_tag(response, "plaincert", &plainCertHex, &errorMessage)) {
      return fail_with_phase("phase 1 (getservercert)", errorMessage);
    }

    std::vector<unsigned char> plainCertBytes;
    if (!hex_decode(plainCertHex, &plainCertBytes, &errorMessage)) {
      return fail_with_phase("phase 1 (getservercert)", errorMessage);
    }
    const std::string plainCertPem(plainCertBytes.begin(), plainCertBytes.end());

    std::vector<unsigned char> aesKey;
    trace_pairing_phase("deriving AES key");
    if (!derive_aes_key(saltHex, request.pin, serverInfo.serverMajorVersion, &aesKey, &errorMessage)) {
      return fail_with_phase("phase 1 (derive AES key)", errorMessage);
    }

    std::array<unsigned char, CLIENT_CHALLENGE_BYTE_COUNT> clientChallengeBytes {};
    if (!fill_random_bytes(clientChallengeBytes.data(), clientChallengeBytes.size(), &errorMessage)) {
      return fail_with_phase("phase 2 (client challenge random)", errorMessage);
    }

    std::vector<unsigned char> encryptedClientChallenge;
    if (!aes_ecb_encrypt(clientChallengeBytes.data(), clientChallengeBytes.size(), aesKey, &encryptedClientChallenge, &errorMessage)) {
      return fail_with_phase("phase 2 (client challenge encrypt)", errorMessage);
    }

    if (!next_pairing_uuid(&requestUuid, &errorMessage)) {
      return result;
    }
    const std::string phase2Path = "/pair?uniqueid=" + uniqueId + "&uuid=" + requestUuid + "&devicename=" + deviceName + "&updateState=1&clientchallenge=" + hex_encode(encryptedClientChallenge.data(), encryptedClientChallenge.size());
    trace_pairing_phase("phase 2 clientchallenge request");
    if (!http_get(request.address, serverInfo.httpPort, phase2Path, false, nullptr, {}, &response, &errorMessage) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 2 (client challenge)", errorMessage);
    }
    if (phaseValue != "1") {
      result.message = "The host rejected the client challenge during pairing";
      return result;
    }

    std::string challengeResponseHex;
    if (!parse_pairing_tag(response, "challengeresponse", &challengeResponseHex, &errorMessage)) {
      return fail_with_phase("phase 2 (client challenge)", errorMessage);
    }

    std::vector<unsigned char> challengeResponseEncrypted;
    std::vector<unsigned char> challengeResponsePlaintext;
    if (!hex_decode(challengeResponseHex, &challengeResponseEncrypted, &errorMessage) || !aes_ecb_decrypt(challengeResponseEncrypted, aesKey, &challengeResponsePlaintext, &errorMessage)) {
      return fail_with_phase("phase 2 (client challenge)", errorMessage);
    }

    const std::size_t hashLength = pairing_hash_length(serverInfo.serverMajorVersion);
    if (challengeResponsePlaintext.size() < hashLength + 16U) {
      result.message = "The host returned an incomplete challenge response during pairing";
      return result;
    }

    std::vector<unsigned char> certificateSignature;
    if (!load_certificate_signature(clientCertificate.get(), &certificateSignature, &errorMessage)) {
      return fail_with_phase("phase 3 (server challenge response)", errorMessage);
    }

    std::array<unsigned char, CLIENT_SECRET_BYTE_COUNT> clientSecretBytes {};
    if (!fill_random_bytes(clientSecretBytes.data(), clientSecretBytes.size(), &errorMessage)) {
      return fail_with_phase("phase 3 (client secret random)", errorMessage);
    }

    std::vector<unsigned char> clientHashSource;
    clientHashSource.insert(clientHashSource.end(), challengeResponsePlaintext.begin() + static_cast<std::ptrdiff_t>(hashLength), challengeResponsePlaintext.begin() + static_cast<std::ptrdiff_t>(hashLength + 16U));
    clientHashSource.insert(clientHashSource.end(), certificateSignature.begin(), certificateSignature.end());
    clientHashSource.insert(clientHashSource.end(), clientSecretBytes.begin(), clientSecretBytes.end());

    std::vector<unsigned char> clientHash;
    if (!compute_digest(clientHashSource.data(), clientHashSource.size(), serverInfo.serverMajorVersion, &clientHash, &errorMessage)) {
      return fail_with_phase("phase 3 (server challenge response)", errorMessage);
    }

    std::vector<unsigned char> encryptedClientHash;
    if (!aes_ecb_encrypt(clientHash.data(), clientHash.size(), aesKey, &encryptedClientHash, &errorMessage)) {
      return fail_with_phase("phase 3 (server challenge response)", errorMessage);
    }

    if (!next_pairing_uuid(&requestUuid, &errorMessage)) {
      return result;
    }
    const std::string phase3Path = "/pair?uniqueid=" + uniqueId + "&uuid=" + requestUuid + "&devicename=" + deviceName + "&updateState=1&serverchallengeresp=" + hex_encode(encryptedClientHash.data(), encryptedClientHash.size());
    trace_pairing_phase("phase 3 serverchallengeresp request");
    if (!http_get(request.address, serverInfo.httpPort, phase3Path, false, nullptr, {}, &response, &errorMessage) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 3 (server challenge response)", errorMessage);
    }
    if (phaseValue != "1") {
      result.message = "The host rejected the server challenge response during pairing";
      return result;
    }

    std::string pairingSecretHex;
    if (!parse_pairing_tag(response, "pairingsecret", &pairingSecretHex, &errorMessage)) {
      return fail_with_phase("phase 3 (server challenge response)", errorMessage);
    }

    std::vector<unsigned char> pairingSecretBytes;
    if (!hex_decode(pairingSecretHex, &pairingSecretBytes, &errorMessage) || pairingSecretBytes.size() <= 16U) {
      return fail_with_phase("phase 4 (pairing secret)", "The host returned an invalid pairing secret");
    }

    std::unique_ptr<X509, X509Deleter> plainCertificate = load_certificate(plainCertPem);
    if (plainCertificate == nullptr) {
      return fail_with_phase("phase 4 (pairing secret)", "The host returned an invalid server certificate during pairing");
    }

    std::vector<unsigned char> serverSecret(pairingSecretBytes.begin(), pairingSecretBytes.begin() + 16);
    std::vector<unsigned char> serverSignature(pairingSecretBytes.begin() + 16, pairingSecretBytes.end());
    if (!verify_sha256_signature(serverSecret, serverSignature, plainCertificate.get(), &errorMessage)) {
      return fail_with_phase("phase 4 (pairing secret)", errorMessage);
    }

    std::vector<unsigned char> clientSecretVector(clientSecretBytes.begin(), clientSecretBytes.end());
    std::vector<unsigned char> clientPairingSignature;
    if (!sign_sha256(clientSecretVector, clientPrivateKey.get(), &clientPairingSignature, &errorMessage)) {
      return fail_with_phase("phase 4 (client pairing secret)", errorMessage);
    }

    std::vector<unsigned char> clientPairingSecret;
    clientPairingSecret.insert(clientPairingSecret.end(), clientSecretBytes.begin(), clientSecretBytes.end());
    clientPairingSecret.insert(clientPairingSecret.end(), clientPairingSignature.begin(), clientPairingSignature.end());

    if (!next_pairing_uuid(&requestUuid, &errorMessage)) {
      return result;
    }
    const std::string phase4Path = "/pair?uniqueid=" + uniqueId + "&uuid=" + requestUuid + "&devicename=" + deviceName + "&updateState=1&clientpairingsecret=" + hex_encode(clientPairingSecret.data(), clientPairingSecret.size());
    trace_pairing_phase("phase 4 clientpairingsecret request");
    if (!http_get(request.address, serverInfo.httpPort, phase4Path, false, nullptr, {}, &response, &errorMessage) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 4 (client pairing secret)", errorMessage);
    }
    if (phaseValue != "1") {
      result.message = "The host rejected the client pairing secret";
      return result;
    }

    if (!next_pairing_uuid(&requestUuid, &errorMessage)) {
      return result;
    }
    const std::string phase5Path = "/pair?uniqueid=" + uniqueId + "&uuid=" + requestUuid + "&devicename=" + deviceName + "&updateState=1&phrase=pairchallenge";
    trace_pairing_phase("phase 5 pairchallenge request");
    if (!http_get(request.address, serverInfo.httpsPort, phase5Path, true, &request.identity, plainCertPem, &response, &errorMessage) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 5 (pairchallenge)", errorMessage);
    }
    if (phaseValue != "1") {
      result.message = "The host rejected the final encrypted pairing challenge";
      return result;
    }

    result.success = true;
    result.message = "Paired successfully with " + request.address;
    trace_pairing_phase("pair_host succeeded");
    return result;
  }

}  // namespace network
