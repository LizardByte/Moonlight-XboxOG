// class header include
#include "src/network/host_pairing.h"

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

// platform includes
#ifdef NXDK
  #include <errno.h>
  #include <hal/debug.h>
  #include <lwip/inet.h>
  #include <lwip/sockets.h>
#elif defined(_WIN32)
// clang-format off
  // winsock2 must be included before windows.h
  #include <winsock2.h>
  #include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names
// clang-format on
#else
  #include <arpa/inet.h>
  #include <cerrno>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif

#if defined(NXDK) || !defined(_WIN32)
using SOCKET = int;

  #ifndef INVALID_SOCKET
    #define INVALID_SOCKET (-1)
  #endif

  #ifndef SOCKET_ERROR
    #define SOCKET_ERROR (-1)
  #endif
#endif

#define OPENSSL_SUPPRESS_DEPRECATED

#ifdef NXDK
  #define _CRT_RAND_S
#endif

#ifdef NXDK
extern "C" int rand_s(unsigned int *randomValue);
#endif

namespace {

  void trace_pairing_phase(const char *message) {
    (void) message;
  }

  void trace_pairing_detail(const std::string &message) {
    (void) message;
  }

  constexpr std::size_t UNIQUE_ID_BYTE_COUNT = 8;
  constexpr std::size_t CLIENT_CHALLENGE_BYTE_COUNT = 16;
  constexpr std::size_t CLIENT_SECRET_BYTE_COUNT = 16;
  constexpr int SOCKET_TIMEOUT_MILLISECONDS = 5000;
  constexpr uint16_t DEFAULT_SERVERINFO_HTTP_PORT = 47989;
  constexpr uint16_t FALLBACK_SERVERINFO_HTTP_PORT = 47984;
  constexpr std::string_view DEFAULT_SERVERINFO_UNIQUE_ID = "0123456789ABCDEF";
  constexpr std::string_view DEFAULT_SERVERINFO_UUID = "11111111-2222-3333-4444-555555555555";
  constexpr std::string_view UNPAIRED_CLIENT_ERROR_MESSAGE = "The host reports that this client is no longer paired. Pair the host again.";

  struct WsaGuard {
    WsaGuard() {
#if defined(NXDK) || !defined(_WIN32)
      initialized = true;
#else
      WSADATA wsaData {};
      initialized = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#endif
    }

    ~WsaGuard() {
#if defined(_WIN32) && !defined(NXDK)
      if (initialized) {
        WSACleanup();
      }
#endif
    }

    bool initialized = false;
  };

  struct SocketGuard {
    SocketGuard() = default;
    SocketGuard(const SocketGuard &) = delete;
    SocketGuard &operator=(const SocketGuard &) = delete;
    SocketGuard(SocketGuard &&) = delete;
    SocketGuard &operator=(SocketGuard &&) = delete;

    ~SocketGuard() {
      if (handle != INVALID_SOCKET) {
#if defined(_WIN32) && !defined(NXDK)
        closesocket(handle);
#else
        close(handle);
#endif
      }
    }

    SOCKET handle = INVALID_SOCKET;
  };

  bool append_error(std::string *errorMessage, std::string message);

  bool pairing_cancel_requested(const std::atomic<bool> *cancelRequested) {
    return cancelRequested != nullptr && cancelRequested->load(std::memory_order_acquire);
  }

  bool append_cancelled_pairing_error(std::string *errorMessage) {
    return append_error(errorMessage, "Pairing cancelled");
  }

  void append_hash_bytes(uint64_t *hash, const unsigned char *bytes, std::size_t byteCount) {
    if (hash == nullptr || bytes == nullptr) {
      return;
    }

    for (std::size_t index = 0; index < byteCount; ++index) {
      *hash ^= bytes[index];
      *hash *= 1099511628211ULL;
    }
  }

  void append_hash_string(uint64_t *hash, std::string_view text) {
    append_hash_bytes(hash, reinterpret_cast<const unsigned char *>(text.data()), text.size());
    static constexpr unsigned char delimiter = 0x1F;
    append_hash_bytes(hash, &delimiter, 1U);
  }

  int last_socket_error() {
#if defined(NXDK) || !defined(_WIN32)
    return errno;
#else
    return WSAGetLastError();
#endif
  }

  bool is_connect_in_progress_error(int errorCode) {
#if defined(NXDK) || !defined(_WIN32)
    return errorCode == EWOULDBLOCK || errorCode == EINPROGRESS || errorCode == EALREADY;
#else
    return errorCode == WSAEWOULDBLOCK || errorCode == WSAEINPROGRESS || errorCode == WSAEALREADY;
#endif
  }

  bool is_timeout_error(int errorCode) {
#if defined(NXDK) || !defined(_WIN32)
    return errorCode == ETIMEDOUT;
#else
    return errorCode == WSAETIMEDOUT;
#endif
  }

  bool set_socket_non_blocking(SOCKET socketHandle, bool enabled, std::string *errorMessage) {
#ifdef NXDK
    int nonBlockingMode = enabled ? 1 : 0;
#elif defined(_WIN32)
    u_long nonBlockingMode = enabled ? 1UL : 0UL;
#endif

#if defined(NXDK) || defined(_WIN32)
    if (ioctlsocket(socketHandle, FIONBIO, &nonBlockingMode) != 0) {  // NOSONAR(cpp:S6004) cannot init variable inside if statement due to macros
      return append_error(errorMessage, std::string("Failed to configure the host pairing socket mode (socket error ") + std::to_string(last_socket_error()) + ")");
    }
#else
    const int currentFlags = fcntl(socketHandle, F_GETFL, 0);
    if (currentFlags < 0) {
      return append_error(errorMessage, std::string("Failed to query the host pairing socket mode (socket error ") + std::to_string(last_socket_error()) + ")");
    }

    const int updatedFlags = enabled ? (currentFlags | O_NONBLOCK) : (currentFlags & ~O_NONBLOCK);
    if (fcntl(socketHandle, F_SETFL, updatedFlags) != 0) {
      return append_error(errorMessage, std::string("Failed to configure the host pairing socket mode (socket error ") + std::to_string(last_socket_error()) + ")");
    }
#endif

    return true;
  }

  void set_socket_timeouts(SOCKET socketHandle) {
#if defined(NXDK) || !defined(_WIN32)
    timeval timeout {
      SOCKET_TIMEOUT_MILLISECONDS / 1000,
      (SOCKET_TIMEOUT_MILLISECONDS % 1000) * 1000,
    };
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
    const DWORD timeoutMilliseconds = SOCKET_TIMEOUT_MILLISECONDS;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
#endif
  }

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
    std::string *errorMessage,
    const std::atomic<bool> *cancelRequested = nullptr
  );

  std::string summarize_http_payload_preview(std::string_view text) {
    constexpr std::size_t MAX_PREVIEW_BYTES = 48;

    if (text.empty()) {
      return "<empty>";
    }

    std::string preview;
    const std::size_t previewLength = std::min(text.size(), MAX_PREVIEW_BYTES);
    for (std::size_t index = 0; index < previewLength; ++index) {
      const auto character = static_cast<unsigned char>(text[index]);
      if (character >= 0x20U && character <= 0x7EU) {
        preview.push_back(static_cast<char>(character));
      } else if (character == '\r') {
        preview += "\\r";
      } else if (character == '\n') {
        preview += "\\n";
      } else if (character == '\t') {
        preview += "\\t";
      } else {
        std::array<char, 5> buffer {};
        std::snprintf(buffer.data(), buffer.size(), "\\x%02X", character);
        preview += buffer.data();
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

  std::string build_serverinfo_path(std::string_view uniqueId) {
    const std::string_view resolvedUniqueId = uniqueId.empty() ? DEFAULT_SERVERINFO_UNIQUE_ID : uniqueId;
    return "/serverinfo?uniqueid=" + std::string(resolvedUniqueId) + "&uuid=" + std::string(DEFAULT_SERVERINFO_UUID);
  }

  std::string build_applist_path(std::string_view uniqueId) {
    const std::string_view resolvedUniqueId = uniqueId.empty() ? DEFAULT_SERVERINFO_UNIQUE_ID : uniqueId;
    return "/applist?uniqueid=" + std::string(resolvedUniqueId) + "&uuid=" + std::string(DEFAULT_SERVERINFO_UUID);
  }

  std::string_view resolve_client_unique_id(const network::PairingIdentity *clientIdentity) {
    if (clientIdentity == nullptr || clientIdentity->uniqueId.empty()) {
      return DEFAULT_SERVERINFO_UNIQUE_ID;
    }

    return clientIdentity->uniqueId;
  }

  std::vector<std::string> build_app_asset_paths(std::string_view uniqueId, int appId) {
    const std::string_view resolvedUniqueId = uniqueId.empty() ? DEFAULT_SERVERINFO_UNIQUE_ID : uniqueId;
    const std::string appIdText = std::to_string(appId);
    const std::string queryPrefix = "?uniqueid=" + std::string(resolvedUniqueId) + "&uuid=" + std::string(DEFAULT_SERVERINFO_UUID);
    return {
      "/appasset" + queryPrefix + "&appid=" + appIdText + "&AssetType=2&AssetIdx=0",
      "/appasset" + queryPrefix + "&appid=" + appIdText + "&AssetType=2",
      "/appasset" + queryPrefix + "&appId=" + appIdText + "&AssetType=2&AssetIdx=0",
      "/appasset" + queryPrefix + "&appid=" + appIdText,
      "/appasset?appid=" + appIdText + "&AssetType=2&AssetIdx=0",
      "/appasset?appId=" + appIdText + "&AssetType=2&AssetIdx=0",
    };
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
      if (const std::string_view item = trim_ascii_whitespace(value.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start)); ascii_iequals(item, token)) {
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
      if (const std::size_t chunkExtensionSeparator = chunkSizeText.find(';'); chunkExtensionSeparator != std::string_view::npos) {
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
      if (const std::size_t separator = headerLine.find(':'); separator != std::string_view::npos) {
        const std::string_view headerName = trim_ascii_whitespace(headerLine.substr(0, separator));
        const std::string_view headerValue = trim_ascii_whitespace(headerLine.substr(separator + 1));

        if (ascii_iequals(headerName, "Content-Length")) {
          hasContentLength = try_parse_decimal_size(headerValue, &contentLength);  // NOSONAR(cpp:S134) header parsing keeps validation adjacent to the matching header branch
          if (!hasContentLength) {
            return append_error(errorMessage, "Received an invalid Content-Length header while pairing");
          }
        } else if (ascii_iequals(headerName, "Transfer-Encoding") && header_value_contains_token(headerValue, "chunked")) {
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
      std::array<char, 256> errorBuffer {};
      ERR_error_string_n(errorCode, errorBuffer.data(), errorBuffer.size());
      if (!details.empty()) {
        details += "; ";
      }
      details += errorBuffer.data();
    }
    return details;
  }

  bool append_openssl_error(std::string *errorMessage, std::string message) {
    if (const std::string details = take_openssl_error_queue(); !details.empty()) {
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
  int nxdk_rand_seed(const void *, int) {  // NOSONAR(cpp:S5008) signature required by OpenSSL RAND_METHOD
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
    // intentionally empty - no cleanup required for nxdk random method
  }

  int nxdk_rand_add(const void *, int, double) {  // NOSONAR(cpp:S5008) signature required by OpenSSL RAND_METHOD
    return 1;
  }

  int nxdk_rand_status() {
    return 1;
  }

  const RAND_METHOD g_nxdk_rand_method = {
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
      output[index * 2] = HEX_DIGITS[(data[index] >> 4) & 0x0F];  // NOSONAR(cpp:S6022) hex encoding is byte-oriented by design
      output[(index * 2) + 1] = HEX_DIGITS[data[index] & 0x0F];  // NOSONAR(cpp:S6022) hex encoding is byte-oriented by design
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
      decoded.push_back(static_cast<unsigned char>((upper << 4) | lower));  // NOSONAR(cpp:S6022) hex decoding is byte-oriented by design
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

    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);  // NOSONAR(cpp:S6022) UUID version bits are byte-oriented by definition
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);  // NOSONAR(cpp:S6022) UUID variant bits are byte-oriented by definition

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

  bool verify_tls_peer_certificate(const SSL *ssl, std::string_view expectedCertificatePem, std::string *errorMessage) {
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

    if (auto *certificateSubject = X509_get_subject_name(certificate.get()); certificateSubject == nullptr) {
      return append_openssl_error(errorMessage, "Failed to populate the client certificate subject for pairing");
    } else {
      X509_NAME_add_entry_by_txt(certificateSubject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("NVIDIA GameStream Client"), -1, -1, 0);
      X509_set_issuer_name(certificate.get(), certificateSubject);
    }

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

  struct XmlElementView {
    std::string_view openTag;
    std::string_view innerXml;
  };

  bool try_parse_flag(std::string_view text, bool *value);
  bool try_parse_uint32(std::string_view text, uint32_t *value);

  bool extract_xml_attribute_value(std::string_view openTag, std::string_view attributeName, std::string *value) {  // NOSONAR(cpp:S3776) permissive host XML parsing stays centralized here
    std::size_t cursor = 0;
    while (cursor < openTag.size()) {
      const std::size_t nameIndex = openTag.find(attributeName, cursor);
      if (nameIndex == std::string_view::npos) {
        return false;
      }

      if (nameIndex > 0) {
        const char previousCharacter = openTag[nameIndex - 1];
        if (previousCharacter != '<' && !std::isspace(static_cast<unsigned char>(previousCharacter))) {
          cursor = nameIndex + 1;
          continue;
        }
      }

      std::size_t separatorIndex = nameIndex + attributeName.size();
      while (separatorIndex < openTag.size() && std::isspace(static_cast<unsigned char>(openTag[separatorIndex]))) {
        ++separatorIndex;
      }
      if (separatorIndex >= openTag.size() || openTag[separatorIndex] != '=') {
        cursor = nameIndex + 1;
        continue;
      }

      ++separatorIndex;
      while (separatorIndex < openTag.size() && std::isspace(static_cast<unsigned char>(openTag[separatorIndex]))) {
        ++separatorIndex;
      }
      if (separatorIndex >= openTag.size() || (openTag[separatorIndex] != '"' && openTag[separatorIndex] != '\'')) {
        cursor = nameIndex + 1;
        continue;
      }

      const char quoteCharacter = openTag[separatorIndex];
      const std::size_t valueStart = separatorIndex + 1;
      const std::size_t valueEnd = openTag.find(quoteCharacter, valueStart);
      if (valueEnd == std::string_view::npos) {
        return false;
      }

      if (value != nullptr) {
        *value = std::string(openTag.substr(valueStart, valueEnd - valueStart));
      }
      return true;
    }

    return false;
  }

  std::vector<XmlElementView> extract_xml_elements(std::string_view xml, std::string_view tagName) {
    std::vector<XmlElementView> elements;
    const std::string openPrefix = "<" + std::string(tagName);
    const std::string closeTag = "</" + std::string(tagName) + ">";
    std::size_t cursor = 0;

    while (cursor < xml.size()) {  // NOSONAR(cpp:S924) permissive XML scanning uses multiple early breaks for malformed payloads
      const std::size_t openIndex = xml.find(openPrefix, cursor);
      if (openIndex == std::string_view::npos) {
        break;
      }

      const std::size_t tagNameEnd = openIndex + openPrefix.size();
      if (tagNameEnd < xml.size() && xml[tagNameEnd] != '>' && xml[tagNameEnd] != '/' && !std::isspace(static_cast<unsigned char>(xml[tagNameEnd]))) {
        cursor = openIndex + 1;
        continue;
      }

      const std::size_t openEnd = xml.find('>', tagNameEnd);
      if (openEnd == std::string_view::npos) {
        break;
      }

      const std::string_view openTag = xml.substr(openIndex, openEnd - openIndex + 1);
      if (openEnd > openIndex && xml[openEnd - 1] == '/') {
        elements.push_back({openTag, {}});
        cursor = openEnd + 1;
        continue;
      }

      const std::size_t closeIndex = xml.find(closeTag, openEnd + 1);
      if (closeIndex == std::string_view::npos) {
        break;
      }

      elements.push_back({openTag, xml.substr(openEnd + 1, closeIndex - openEnd - 1)});
      cursor = closeIndex + closeTag.size();
    }

    return elements;
  }

  void skip_json_whitespace(std::string_view json, std::size_t *cursor) {
    if (cursor == nullptr) {
      return;
    }

    while (*cursor < json.size() && std::isspace(static_cast<unsigned char>(json[*cursor]))) {
      ++(*cursor);
    }
  }

  bool parse_json_string_literal(std::string_view json, std::size_t *cursor, std::string *value) {
    if (cursor == nullptr || *cursor >= json.size() || json[*cursor] != '"') {
      return false;
    }

    ++(*cursor);
    std::string parsedValue;
    while (*cursor < json.size()) {
      const char character = json[*cursor];
      ++(*cursor);
      if (character == '"') {
        if (value != nullptr) {
          *value = std::move(parsedValue);
        }
        return true;
      }
      if (character != '\\') {
        parsedValue.push_back(character);
        continue;
      }

      if (*cursor >= json.size()) {
        return false;
      }

      const char escaped = json[*cursor];
      ++(*cursor);
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          parsedValue.push_back(escaped);
          break;
        case 'b':
          parsedValue.push_back('\b');
          break;
        case 'f':
          parsedValue.push_back('\f');
          break;
        case 'n':
          parsedValue.push_back('\n');
          break;
        case 'r':
          parsedValue.push_back('\r');
          break;
        case 't':
          parsedValue.push_back('\t');
          break;
        case 'u':
          if ((*cursor + 4U) > json.size()) {
            return false;
          }
          parsedValue.push_back('?');
          *cursor += 4U;
          break;
        default:
          parsedValue.push_back(escaped);
          break;
      }
    }

    return false;
  }

  bool find_matching_json_delimiter(std::string_view json, std::size_t openIndex, char openCharacter, char closeCharacter, std::size_t *closeIndex) {
    if (openIndex >= json.size() || json[openIndex] != openCharacter) {
      return false;
    }

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (std::size_t index = openIndex; index < json.size(); ++index) {
      const char character = json[index];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (character == '\\') {
          escaped = true;
        } else if (character == '"') {
          inString = false;
        }
        continue;
      }

      if (character == '"') {
        inString = true;
        continue;
      }
      if (character == openCharacter) {
        ++depth;
        continue;
      }
      if (character == closeCharacter) {
        --depth;
        if (depth == 0) {  // NOSONAR(cpp:S134) delimiter matching keeps nested parsing state local
          if (closeIndex != nullptr) {
            *closeIndex = index;
          }
          return true;
        }
      }
    }

    return false;
  }

  bool extract_json_named_array(std::string_view json, std::string_view fieldName, std::string_view *arrayView) {
    const std::string keyToken = "\"" + std::string(fieldName) + "\"";
    std::size_t cursor = 0;
    while (cursor < json.size()) {
      const std::size_t keyIndex = json.find(keyToken, cursor);
      if (keyIndex == std::string_view::npos) {
        return false;
      }

      std::size_t separatorIndex = keyIndex + keyToken.size();
      skip_json_whitespace(json, &separatorIndex);
      if (separatorIndex >= json.size() || json[separatorIndex] != ':') {
        cursor = keyIndex + 1;
        continue;
      }

      ++separatorIndex;
      skip_json_whitespace(json, &separatorIndex);
      if (separatorIndex >= json.size() || json[separatorIndex] != '[') {
        cursor = keyIndex + 1;
        continue;
      }

      std::size_t arrayEnd = separatorIndex;
      if (!find_matching_json_delimiter(json, separatorIndex, '[', ']', &arrayEnd)) {
        return false;
      }

      if (arrayView != nullptr) {
        *arrayView = json.substr(separatorIndex, arrayEnd - separatorIndex + 1U);
      }
      return true;
    }

    return false;
  }

  std::vector<std::string_view> extract_json_object_blocks(std::string_view arrayView) {
    std::vector<std::string_view> blocks;
    if (arrayView.size() < 2U || arrayView.front() != '[' || arrayView.back() != ']') {
      return blocks;
    }

    std::size_t cursor = 1;
    while (cursor + 1U < arrayView.size()) {  // NOSONAR(cpp:S924) permissive JSON scanning uses multiple early breaks for malformed payloads
      skip_json_whitespace(arrayView, &cursor);
      if (cursor + 1U >= arrayView.size()) {
        break;
      }
      if (arrayView[cursor] == ',') {
        ++cursor;
        continue;
      }
      if (arrayView[cursor] != '{') {
        ++cursor;
        continue;
      }

      std::size_t objectEnd = cursor;
      if (!find_matching_json_delimiter(arrayView, cursor, '{', '}', &objectEnd)) {
        break;
      }

      blocks.push_back(arrayView.substr(cursor, objectEnd - cursor + 1U));
      cursor = objectEnd + 1U;
    }

    return blocks;
  }

  bool extract_json_field_value(std::string_view object, std::string_view fieldName, std::string_view *valueView, bool *isStringValue = nullptr) {  // NOSONAR(cpp:S3776) permissive JSON parsing stays centralized here
    if (object.empty() || object.front() != '{') {
      return false;
    }

    std::size_t cursor = 1;
    while (cursor < object.size()) {
      skip_json_whitespace(object, &cursor);
      if (cursor >= object.size() || object[cursor] == '}') {
        return false;
      }

      std::size_t keyCursor = cursor;
      std::string key;
      if (!parse_json_string_literal(object, &keyCursor, &key)) {
        return false;
      }

      skip_json_whitespace(object, &keyCursor);
      if (keyCursor >= object.size() || object[keyCursor] != ':') {
        return false;
      }

      ++keyCursor;
      skip_json_whitespace(object, &keyCursor);
      if (keyCursor >= object.size()) {
        return false;
      }

      const std::size_t valueStart = keyCursor;
      std::size_t valueEnd = valueStart;
      bool valueIsString = false;
      if (object[keyCursor] == '"') {
        valueIsString = true;
        if (std::string ignored; !parse_json_string_literal(object, &keyCursor, &ignored)) {
          return false;
        }
        valueEnd = keyCursor;
      } else if (object[keyCursor] == '{') {
        if (!find_matching_json_delimiter(object, keyCursor, '{', '}', &valueEnd)) {
          return false;
        }
        keyCursor = valueEnd + 1U;
      } else if (object[keyCursor] == '[') {
        if (!find_matching_json_delimiter(object, keyCursor, '[', ']', &valueEnd)) {
          return false;
        }
        keyCursor = valueEnd + 1U;
      } else {
        while (keyCursor < object.size() && object[keyCursor] != ',' && object[keyCursor] != '}') {
          ++keyCursor;
        }
        valueEnd = keyCursor;
      }

      if (key == fieldName) {
        if (valueView != nullptr) {
          *valueView = trim_ascii_whitespace(object.substr(valueStart, valueEnd - valueStart));
        }
        if (isStringValue != nullptr) {
          *isStringValue = valueIsString;
        }
        return true;
      }

      cursor = keyCursor;
      if (cursor < object.size() && object[cursor] == ',') {
        ++cursor;
      }
    }

    return false;
  }

  bool extract_json_string_like_field(std::string_view object, const std::vector<std::string_view> &fieldNames, std::string *value) {
    for (std::string_view fieldName : fieldNames) {
      std::string_view rawValue;
      bool isStringValue = false;
      if (!extract_json_field_value(object, fieldName, &rawValue, &isStringValue)) {
        continue;
      }

      if (isStringValue) {
        std::size_t cursor = 0;
        std::string parsedValue;
        if (parse_json_string_literal(rawValue, &cursor, &parsedValue)) {
          if (value != nullptr) {  // NOSONAR(cpp:S134) JSON field extraction keeps conversion adjacent to the matching field
            *value = std::move(parsedValue);
          }
          return true;
        }
      } else if (!rawValue.empty()) {
        if (value != nullptr) {
          *value = std::string(trim_ascii_whitespace(rawValue));
        }
        return true;
      }
    }

    return false;
  }

  bool extract_json_bool_like_field(std::string_view object, const std::vector<std::string_view> &fieldNames, bool *value) {
    for (std::string_view fieldName : fieldNames) {
      std::string text;
      if (!extract_json_string_like_field(object, {fieldName}, &text)) {
        continue;
      }

      if (try_parse_flag(text, value)) {
        return true;
      }
    }

    return false;
  }

  std::vector<XmlElementView> extract_candidate_app_elements(std::string_view xml) {
    for (std::string_view tagName : {std::string_view("App"), std::string_view("app"), std::string_view("Game"), std::string_view("game"), std::string_view("Application"), std::string_view("application"), std::string_view("Program"), std::string_view("program")}) {
      std::vector<XmlElementView> elements = extract_xml_elements(xml, tagName);
      if (!elements.empty()) {
        return elements;
      }
    }

    return {};
  }

  bool parse_json_app_list_response(std::string_view json, std::vector<network::HostAppEntry> *apps, std::string *errorMessage) {
    const std::string_view trimmed = trim_ascii_whitespace(json);
    std::string_view appArray;
    if (!trimmed.empty() && trimmed.front() == '[') {
      appArray = trimmed;
    } else {
      for (std::string_view fieldName : {std::string_view("apps"), std::string_view("Applications"), std::string_view("applications"), std::string_view("games"), std::string_view("Games"), std::string_view("applist"), std::string_view("data")}) {
        if (extract_json_named_array(trimmed, fieldName, &appArray)) {
          break;
        }
      }
    }

    if (appArray.empty()) {
      return append_error(errorMessage, "The host applist response did not contain any app entries");
    }

    const std::vector<std::string_view> appObjects = extract_json_object_blocks(appArray);
    if (appObjects.empty()) {
      return append_error(errorMessage, "The host applist response did not contain any app entries");
    }

    std::vector<network::HostAppEntry> parsedApps;
    parsedApps.reserve(appObjects.size());
    for (std::string_view appObject : appObjects) {
      std::string name;
      std::string idText;
      extract_json_string_like_field(appObject, {"AppTitle", "title", "Title", "name", "Name", "displayName", "DisplayName"}, &name);
      extract_json_string_like_field(appObject, {"ID", "id", "Id", "appid", "appId", "AppId"}, &idText);

      uint32_t parsedId = 0;
      if (name.empty() || !try_parse_uint32(trim_ascii_whitespace(idText), &parsedId) || parsedId == 0U) {
        continue;
      }

      bool hdrSupported = false;
      bool hidden = false;
      extract_json_bool_like_field(appObject, {"IsHdrSupported", "HDRSupported", "isHdrSupported", "hdrSupported"}, &hdrSupported);
      extract_json_bool_like_field(appObject, {"Hidden", "hidden", "IsHidden", "isHidden"}, &hidden);
      parsedApps.push_back({name, static_cast<int>(parsedId), hdrSupported, hidden});
    }

    if (parsedApps.empty()) {
      return append_error(errorMessage, "The host applist response did not include any valid app IDs");
    }

    if (apps != nullptr) {
      *apps = std::move(parsedApps);
    }
    return true;
  }

  bool append_applist_status_error(std::string_view responseBody, std::string *errorMessage) {
    const std::vector<XmlElementView> roots = extract_xml_elements(responseBody, "root");
    if (roots.empty()) {
      return append_error(errorMessage, "The host applist response did not contain any app entries (payload preview: " + summarize_http_payload_preview(responseBody) + ")");
    }

    std::string statusCodeText;
    std::string statusMessage;
    extract_xml_attribute_value(roots.front().openTag, "status_code", &statusCodeText);
    extract_xml_attribute_value(roots.front().openTag, "status_message", &statusMessage);

    if (uint32_t statusCode = 200; !statusCodeText.empty() && try_parse_uint32(trim_ascii_whitespace(statusCodeText), &statusCode) && statusCode != 200U) {
      const std::string normalizedStatusMessage = statusMessage.empty() ? "The host returned status " + std::to_string(statusCode) + " while requesting /applist" : statusMessage;
      return append_error(errorMessage, normalizedStatusMessage);
    }

    return append_error(errorMessage, "The host applist response did not contain any app entries (payload preview: " + summarize_http_payload_preview(responseBody) + ")");
  }

  bool extract_root_status(std::string_view responseBody, uint32_t *statusCode, std::string *statusMessage) {
    const std::vector<XmlElementView> roots = extract_xml_elements(responseBody, "root");
    if (roots.empty()) {
      return false;
    }

    std::string statusCodeText;
    extract_xml_attribute_value(roots.front().openTag, "status_code", &statusCodeText);
    if (statusMessage != nullptr) {
      extract_xml_attribute_value(roots.front().openTag, "status_message", statusMessage);
    }

    uint32_t parsedStatusCode = 200;
    if (!statusCodeText.empty() && !try_parse_uint32(trim_ascii_whitespace(statusCodeText), &parsedStatusCode)) {
      return false;
    }

    if (statusCode != nullptr) {
      *statusCode = parsedStatusCode;
    }
    return true;
  }

  bool response_indicates_unpaired_client(const HttpResponse &response) {
    if (response.statusCode == 401 || response.statusCode == 403) {
      return true;
    }

    uint32_t rootStatusCode = 200;
    std::string rootStatusMessage;
    if (!extract_root_status(response.body, &rootStatusCode, &rootStatusMessage)) {
      return false;
    }

    return rootStatusCode == 401U || rootStatusCode == 403U || network::error_indicates_unpaired_client(rootStatusMessage);
  }

  std::vector<std::string_view> extract_xml_element_blocks(std::string_view xml, std::string_view tagName) {
    std::vector<std::string_view> blocks;
    const std::string openPrefix = "<" + std::string(tagName);
    const std::string closeTag = "</" + std::string(tagName) + ">";
    std::size_t cursor = 0;

    while (cursor < xml.size()) {  // NOSONAR(cpp:S924) permissive XML scanning uses multiple early breaks for malformed payloads
      const std::size_t openIndex = xml.find(openPrefix, cursor);
      if (openIndex == std::string_view::npos) {
        break;
      }

      const std::size_t tagNameEnd = openIndex + openPrefix.size();
      if (tagNameEnd < xml.size() && xml[tagNameEnd] != '>' && !std::isspace(static_cast<unsigned char>(xml[tagNameEnd]))) {
        cursor = openIndex + 1;
        continue;
      }

      const std::size_t openEnd = xml.find('>', tagNameEnd);
      if (openEnd == std::string_view::npos) {
        break;
      }

      const std::size_t closeIndex = xml.find(closeTag, openEnd + 1);
      if (closeIndex == std::string_view::npos) {
        break;
      }

      blocks.push_back(xml.substr(openEnd + 1, closeIndex - openEnd - 1));
      cursor = closeIndex + closeTag.size();
    }

    return blocks;
  }

  bool try_parse_flag(std::string_view text, bool *value) {
    const std::string_view trimmed = trim_ascii_whitespace(text);
    if (trimmed == "1" || ascii_iequals(trimmed, "true") || ascii_iequals(trimmed, "yes")) {
      if (value != nullptr) {
        *value = true;
      }
      return true;
    }

    if (trimmed == "0" || ascii_iequals(trimmed, "false") || ascii_iequals(trimmed, "no")) {
      if (value != nullptr) {
        *value = false;
      }
      return true;
    }

    return false;
  }

  bool body_looks_like_xml(std::string_view body) {
    const std::string_view trimmed = trim_ascii_whitespace(body);
    return !trimmed.empty() && trimmed.front() == '<';
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

  bool try_parse_uint32(std::string_view text, uint32_t *value) {
    if (text.empty()) {
      return false;
    }

    unsigned long parsedValue = 0;
    for (char character : text) {
      if (character < '0' || character > '9') {
        return false;
      }
      parsedValue = (parsedValue * 10UL) + static_cast<unsigned long>(character - '0');
      if (parsedValue > 0xFFFFFFFFUL) {
        return false;
      }
    }

    if (value != nullptr) {
      *value = static_cast<uint32_t>(parsedValue);
    }
    return true;
  }

  bool connect_socket(const std::string &address, uint16_t port, SocketGuard *socketGuard, std::string *errorMessage, const std::atomic<bool> *cancelRequested = nullptr) {  // NOSONAR(cpp:S3776) connection setup keeps platform-specific error handling in one place
    if (socketGuard == nullptr) {
      return append_error(errorMessage, "Internal pairing error while preparing the host connection");
    }
    if (pairing_cancel_requested(cancelRequested)) {
      return append_cancelled_pairing_error(errorMessage);
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

    trace_pairing_phase("setting non-blocking connect mode");
    if (!set_socket_non_blocking(socketGuard->handle, true, errorMessage)) {
      return false;
    }

    trace_pairing_detail("connecting to " + address + ":" + std::to_string(port));
    if (const int connectResult = connect(socketGuard->handle, reinterpret_cast<const sockaddr *>(&socketAddress), sizeof(socketAddress)); connectResult == SOCKET_ERROR) {
      if (const int connectError = last_socket_error(); !is_connect_in_progress_error(connectError)) {
        return append_error(errorMessage, "Failed to connect to the host pairing endpoint at " + address + ":" + std::to_string(port) + " (socket error " + std::to_string(connectError) + ")");
      }

      trace_pairing_phase("waiting for timed connect completion");
      constexpr int CONNECT_POLL_INTERVAL_MILLISECONDS = 100;
      int remainingWaitMilliseconds = SOCKET_TIMEOUT_MILLISECONDS;
      int selectResult = 0;
      while (remainingWaitMilliseconds > 0) {  // NOSONAR(cpp:S924) timed connect polling intentionally uses multiple early breaks and returns
        if (pairing_cancel_requested(cancelRequested)) {
          return append_cancelled_pairing_error(errorMessage);
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(socketGuard->handle, &writeSet);
        const int waitMilliseconds = std::min(remainingWaitMilliseconds, CONNECT_POLL_INTERVAL_MILLISECONDS);
        timeval timeout {
          waitMilliseconds / 1000,
          (waitMilliseconds % 1000) * 1000,
        };

        selectResult = select(static_cast<int>(socketGuard->handle) + 1, nullptr, &writeSet, nullptr, &timeout);
        if (selectResult != 0) {
          break;
        }

        remainingWaitMilliseconds -= waitMilliseconds;
      }
      if (selectResult <= 0) {
        if (pairing_cancel_requested(cancelRequested)) {
          return append_cancelled_pairing_error(errorMessage);
        }
        return append_error(errorMessage, selectResult == 0 ? "Timed out connecting to the host pairing endpoint at " + address + ":" + std::to_string(port) : "Connection test failed while waiting for the host pairing endpoint at " + address + ":" + std::to_string(port));
      }

      int socketError = 0;
#if defined(_WIN32) && !defined(NXDK)
      if (int socketErrorLength = sizeof(socketError); getsockopt(socketGuard->handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socketError), &socketErrorLength) != 0) {
#else
      if (socklen_t socketErrorLength = sizeof(socketError); getsockopt(socketGuard->handle, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorLength) != 0) {
#endif
        return append_error(errorMessage, "Failed to query the host pairing socket status after connect (socket error " + std::to_string(last_socket_error()) + ")");
      }
      if (socketError != 0) {
        return append_error(errorMessage, "Host refused the pairing connection on " + address + ":" + std::to_string(port) + " (socket error " + std::to_string(socketError) + ")");
      }
    }

    trace_pairing_phase("restoring blocking mode after connect");
    if (!set_socket_non_blocking(socketGuard->handle, false, errorMessage)) {
      return false;
    }

    set_socket_timeouts(socketGuard->handle);
    trace_pairing_phase("socket connected");

    return true;
  }

  bool recv_all_plain(SOCKET socketHandle, std::string *response, std::string *errorMessage, const std::atomic<bool> *cancelRequested = nullptr) {
    std::string received;
    std::array<char, 4096> buffer {};
    std::size_t completeLength = 0;

    while (true) {  // NOSONAR(cpp:S924) response framing intentionally uses multiple early breaks and returns
      if (pairing_cancel_requested(cancelRequested)) {
        return append_cancelled_pairing_error(errorMessage);
      }

      const int bytesRead = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
      if (bytesRead == 0) {
        break;
      }
      if (bytesRead < 0) {
        const int socketError = last_socket_error();
        return append_error(errorMessage, is_timeout_error(socketError) ? "Timed out while reading the host pairing response" : "Failed while reading the host pairing response (socket error " + std::to_string(socketError) + ")");
      }
      received.append(buffer.data(), buffer.data() + bytesRead);

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

  bool recv_all_ssl(SSL *ssl, std::string *response, std::string *errorMessage, const std::atomic<bool> *cancelRequested = nullptr) {
    std::string received;
    std::array<char, 4096> buffer {};
    std::size_t completeLength = 0;

    while (true) {  // NOSONAR(cpp:S924) response framing intentionally uses multiple early breaks and returns
      if (pairing_cancel_requested(cancelRequested)) {
        return append_cancelled_pairing_error(errorMessage);
      }

      const int bytesRead = SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
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
        return append_openssl_error(errorMessage, errorCode == SSL_ERROR_SYSCALL && is_timeout_error(last_socket_error()) ? "Timed out while reading the encrypted host pairing response" : "Failed while reading the encrypted host pairing response");
      }
      received.append(buffer.data(), buffer.data() + bytesRead);

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

  bool send_all_plain(SOCKET socketHandle, std::string_view request, std::string *errorMessage, const std::atomic<bool> *cancelRequested = nullptr) {
    std::size_t sent = 0;
    while (sent < request.size()) {
      if (pairing_cancel_requested(cancelRequested)) {
        return append_cancelled_pairing_error(errorMessage);
      }

      const int bytesSent = send(socketHandle, request.data() + sent, static_cast<int>(request.size() - sent), 0);
      if (bytesSent <= 0) {
        return append_error(errorMessage, "Failed to send the host pairing request (socket error " + std::to_string(last_socket_error()) + ")");
      }
      sent += static_cast<std::size_t>(bytesSent);
    }

    return true;
  }

  bool send_all_ssl(SSL *ssl, std::string_view request, std::string *errorMessage, const std::atomic<bool> *cancelRequested = nullptr) {
    std::size_t sent = 0;
    while (sent < request.size()) {
      if (pairing_cancel_requested(cancelRequested)) {
        return append_cancelled_pairing_error(errorMessage);
      }

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

  bool query_server_info_internal(
    const std::string &address,
    uint16_t preferredHttpPort,
    std::string_view uniqueId,
    HttpResponse *response,
    network::HostPairingServerInfo *serverInfo,
    std::string *errorMessage,
    const std::atomic<bool> *cancelRequested = nullptr
  ) {
    if (address.empty()) {
      return append_error(errorMessage, "Pairing requires a valid host address");
    }
    if (pairing_cancel_requested(cancelRequested)) {
      return append_cancelled_pairing_error(errorMessage);
    }

    const std::vector<uint16_t> candidatePorts = build_serverinfo_port_candidates(preferredHttpPort);
    const std::string serverInfoPath = build_serverinfo_path(uniqueId);
    std::vector<std::string> attemptFailures;

    for (uint16_t candidatePort : candidatePorts) {
      trace_pairing_detail("query_server_info: trying port " + std::to_string(candidatePort));

      HttpResponse candidateResponse {};
      std::string attemptError;
      if (!http_get(address, candidatePort, serverInfoPath, false, nullptr, {}, &candidateResponse, &attemptError, cancelRequested)) {
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

  bool http_get(  // NOSONAR(cpp:S3776,cpp:S107) HTTP transport keeps TLS/plain fallback and error reporting in one place
    const std::string &address,
    uint16_t port,
    std::string_view pathAndQuery,
    bool useTls,
    const network::PairingIdentity *tlsClientIdentity,
    std::string_view expectedTlsCertificatePem,
    HttpResponse *response,
    std::string *errorMessage,
    const std::atomic<bool> *cancelRequested
  ) {
    if (pairing_cancel_requested(cancelRequested)) {
      return append_cancelled_pairing_error(errorMessage);
    }

    trace_pairing_phase("http_get: socket initialization");
    if (WsaGuard wsaGuard; !wsaGuard.initialized) {
      return append_error(errorMessage, "Failed to initialize socket support for host pairing");
    }

    SocketGuard socketGuard;
    trace_pairing_phase("http_get: connect_socket");
    if (!connect_socket(address, port, &socketGuard, errorMessage, cancelRequested)) {
      return false;
    }

    const std::string request =
      "GET " + std::string(pathAndQuery) +
      " HTTP/1.1\r\n"
      "Host: " +
      address + ":" + std::to_string(port) +
      "\r\n"
      "User-Agent: Moonlight-XboxOG\r\n"
      "Connection: close\r\n\r\n";

    std::string rawResponse;
    if (!useTls) {
      trace_pairing_phase("http_get: sending plain request");
      if (!send_all_plain(socketGuard.handle, request, errorMessage, cancelRequested) || !recv_all_plain(socketGuard.handle, &rawResponse, errorMessage, cancelRequested)) {
        return false;
      }
    } else {
      if (pairing_cancel_requested(cancelRequested)) {
        return append_cancelled_pairing_error(errorMessage);
      }

      trace_pairing_phase("http_get: preparing TLS");
      if (!ensure_pairing_entropy(errorMessage)) {
        return false;
      }

      std::unique_ptr<SSL_CTX, SslCtxDeleter> context(SSL_CTX_new(TLS_client_method()));
      if (context == nullptr) {
        return append_openssl_error(errorMessage, "Failed to create the TLS context for host pairing");
      }

      SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);  // NOSONAR(cpp:S4830) certificate pinning is enforced by verify_tls_peer_certificate()
      if (tlsClientIdentity != nullptr && !configure_tls_pairing_identity(context.get(), *tlsClientIdentity, errorMessage)) {
        return false;
      }
      if (pairing_cancel_requested(cancelRequested)) {
        return append_cancelled_pairing_error(errorMessage);
      }

      std::unique_ptr<SSL, SslDeleter> ssl(SSL_new(context.get()));
      if (ssl == nullptr) {
        return append_openssl_error(errorMessage, "Failed to create the TLS session for host pairing");
      }

#ifdef NXDK
      std::unique_ptr<BIO, BioDeleter> socketBio(BIO_new_fd(socketGuard.handle, BIO_NOCLOSE));
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
      if (const int connectResult = SSL_connect(ssl.get()); connectResult != 1) {
        const int sslError = SSL_get_error(ssl.get(), connectResult);
        std::string tlsError = "Failed to establish the encrypted host pairing session (SSL error " + std::to_string(sslError);
        if (sslError == SSL_ERROR_SYSCALL) {
          tlsError += ", socket error " + std::to_string(last_socket_error());
        }
        tlsError += ")";
        return append_openssl_error(errorMessage, std::move(tlsError));
      }
      if (!verify_tls_peer_certificate(ssl.get(), expectedTlsCertificatePem, errorMessage)) {
        return false;
      }

      trace_pairing_phase("http_get: sending TLS request");
      if (!send_all_ssl(ssl.get(), request, errorMessage, cancelRequested) || !recv_all_ssl(ssl.get(), &rawResponse, errorMessage, cancelRequested)) {
        return false;
      }
    }

    trace_pairing_phase("http_get: parsing HTTP response");
    return parse_http_response(rawResponse, response, errorMessage);
  }

  const EVP_MD *pairing_digest(int serverMajorVersion) {
    // TODO: remove legacy support... it is not needed
    return serverMajorVersion >= 7 ? EVP_sha256() : EVP_sha1();  // NOSONAR(cpp:S4790) legacy servers require SHA-1 compatibility
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

    if (EVP_EncryptInit_ex(context.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1 || EVP_CIPHER_CTX_set_padding(context.get(), 0) != 1) {  // NOSONAR(cpp:S5542) Moonlight pairing protocol requires AES-ECB with no padding
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

    if (EVP_DecryptInit_ex(context.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1 || EVP_CIPHER_CTX_set_padding(context.get(), 0) != 1) {  // NOSONAR(cpp:S5542) Moonlight pairing protocol requires AES-ECB with no padding
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

  bool load_certificate_signature(const X509 *certificate, std::vector<unsigned char> *signature, std::string *errorMessage) {
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

  bool generate_pairing_pin(std::string *pin, std::string *errorMessage) {
    static constexpr std::size_t PAIRING_PIN_LENGTH = 4U;
    static constexpr unsigned char DECIMAL_REJECTION_LIMIT = 250U;

    if (pin == nullptr) {
      return append_error(errorMessage, "A pairing PIN output buffer is required");
    }
    if (errorMessage != nullptr) {
      errorMessage->clear();
    }

    pin->clear();
    pin->reserve(PAIRING_PIN_LENGTH);
    while (pin->size() < PAIRING_PIN_LENGTH) {
      unsigned char randomByte = 0;
      if (!fill_random_bytes(&randomByte, sizeof(randomByte), errorMessage)) {
        pin->clear();
        return false;
      }
      if (randomByte >= DECIMAL_REJECTION_LIMIT) {
        continue;
      }

      pin->push_back(static_cast<char>('0' + (randomByte % 10U)));
    }

    return true;
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

    std::string hostName;
    extract_xml_tag_value(xml, "hostname", &hostName) || extract_xml_tag_value(xml, "HostName", &hostName);

    std::string uuid;
    extract_xml_tag_value(xml, "uuid", &uuid) || extract_xml_tag_value(xml, "UniqueId", &uuid);

    std::string localAddress;
    extract_xml_tag_value(xml, "LocalIP", &localAddress) || extract_xml_tag_value(xml, "localip", &localAddress);

    std::string remoteAddress;
    extract_xml_tag_value(xml, "ExternalIP", &remoteAddress) || extract_xml_tag_value(xml, "externalip", &remoteAddress);

    std::string ipv6Address;
    extract_xml_tag_value(xml, "IPv6", &ipv6Address) || extract_xml_tag_value(xml, "LocalIP6", &ipv6Address);

    std::string macAddress;
    extract_xml_tag_value(xml, "mac", &macAddress) || extract_xml_tag_value(xml, "MacAddress", &macAddress);

    std::string activeAddress;
    if (!localAddress.empty()) {
      activeAddress = localAddress;
    } else if (!remoteAddress.empty()) {
      activeAddress = remoteAddress;
    }

    std::string runningGameIdText;
    uint32_t runningGameId = 0;
    if (extract_xml_tag_value(xml, "currentgame", &runningGameIdText) || extract_xml_tag_value(xml, "CurrentGame", &runningGameIdText)) {
      try_parse_uint32(runningGameIdText, &runningGameId);
    }

    if (serverInfo != nullptr) {
      serverInfo->serverMajorVersion = std::atoi(appVersion.c_str());
      serverInfo->httpPort = httpPort == 0 ? fallbackHttpPort : httpPort;
      serverInfo->httpsPort = httpsPort == 0 ? fallbackHttpPort : httpsPort;
      serverInfo->paired = pairStatus == "1";
      serverInfo->hostName = std::move(hostName);
      serverInfo->uuid = std::move(uuid);
      serverInfo->activeAddress = std::move(activeAddress);
      serverInfo->localAddress = std::move(localAddress);
      serverInfo->remoteAddress = std::move(remoteAddress);
      serverInfo->ipv6Address = std::move(ipv6Address);
      serverInfo->macAddress = std::move(macAddress);
      serverInfo->runningGameId = runningGameId;
    }
    return true;
  }

  bool parse_app_list_response(std::string_view xml, std::vector<HostAppEntry> *apps, std::string *errorMessage) {
    const std::string_view trimmedResponse = trim_ascii_whitespace(xml);
    const std::vector<XmlElementView> appElements = extract_candidate_app_elements(trimmedResponse);

    if (appElements.empty()) {
      if (!trimmedResponse.empty() && trimmedResponse.front() == '<') {
        return append_applist_status_error(trimmedResponse, errorMessage);
      }
      return parse_json_app_list_response(trimmedResponse, apps, errorMessage);
    }

    std::vector<HostAppEntry> parsedApps;
    parsedApps.reserve(appElements.size());
    for (const XmlElementView &appElement : appElements) {
      std::string name;
      std::string idText;
      extract_xml_tag_value(appElement.innerXml, "AppTitle", &name) || extract_xml_tag_value(appElement.innerXml, "Title", &name) || extract_xml_tag_value(appElement.innerXml, "Name", &name) || extract_xml_tag_value(appElement.innerXml, "title", &name) || extract_xml_tag_value(appElement.innerXml, "name", &name) || extract_xml_attribute_value(appElement.openTag, "AppTitle", &name) || extract_xml_attribute_value(appElement.openTag, "Title", &name) || extract_xml_attribute_value(appElement.openTag, "Name", &name) || extract_xml_attribute_value(appElement.openTag, "title", &name) || extract_xml_attribute_value(appElement.openTag, "name", &name);
      extract_xml_tag_value(appElement.innerXml, "ID", &idText) || extract_xml_tag_value(appElement.innerXml, "Id", &idText) || extract_xml_tag_value(appElement.innerXml, "id", &idText) || extract_xml_attribute_value(appElement.openTag, "ID", &idText) || extract_xml_attribute_value(appElement.openTag, "Id", &idText) || extract_xml_attribute_value(appElement.openTag, "id", &idText) || extract_xml_attribute_value(appElement.openTag, "appid", &idText) || extract_xml_attribute_value(appElement.openTag, "appId", &idText);

      uint32_t parsedId = 0;
      if (name.empty() || !try_parse_uint32(trim_ascii_whitespace(idText), &parsedId) || parsedId == 0U) {
        continue;
      }

      bool hdrSupported = false;
      bool hidden = false;
      std::string hdrText;
      std::string hiddenText;
      extract_xml_tag_value(appElement.innerXml, "IsHdrSupported", &hdrText) || extract_xml_tag_value(appElement.innerXml, "HDRSupported", &hdrText) || extract_xml_tag_value(appElement.innerXml, "isHdrSupported", &hdrText) || extract_xml_attribute_value(appElement.openTag, "IsHdrSupported", &hdrText) || extract_xml_attribute_value(appElement.openTag, "HDRSupported", &hdrText) || extract_xml_attribute_value(appElement.openTag, "isHdrSupported", &hdrText);
      extract_xml_tag_value(appElement.innerXml, "Hidden", &hiddenText) || extract_xml_tag_value(appElement.innerXml, "IsHidden", &hiddenText) || extract_xml_tag_value(appElement.innerXml, "hidden", &hiddenText) || extract_xml_attribute_value(appElement.openTag, "Hidden", &hiddenText) || extract_xml_attribute_value(appElement.openTag, "IsHidden", &hiddenText) || extract_xml_attribute_value(appElement.openTag, "hidden", &hiddenText);
      try_parse_flag(hdrText, &hdrSupported);
      try_parse_flag(hiddenText, &hidden);

      parsedApps.push_back({name, static_cast<int>(parsedId), hdrSupported, hidden});
    }

    if (parsedApps.empty()) {
      return append_error(errorMessage, "The host applist response did not include any valid app IDs");
    }

    if (apps != nullptr) {
      *apps = std::move(parsedApps);
    }
    return true;
  }

  bool query_server_info(
    const std::string &address,
    uint16_t preferredHttpPort,
    const PairingIdentity *clientIdentity,
    HostPairingServerInfo *serverInfo,
    std::string *errorMessage
  ) {
    const bool canCheckCurrentClientPairing = clientIdentity != nullptr && is_valid_pairing_identity(*clientIdentity);
    if (!query_server_info_internal(address, preferredHttpPort, resolve_client_unique_id(clientIdentity), nullptr, serverInfo, errorMessage)) {
      return false;
    }

    if (serverInfo != nullptr) {
      serverInfo->pairingStatusCurrentClientKnown = false;
      serverInfo->pairingStatusCurrentClient = false;
    }

    if (!canCheckCurrentClientPairing || serverInfo == nullptr || serverInfo->httpsPort == 0) {
      return true;
    }

    HttpResponse authorizationResponse {};
    if (std::string authorizationError; !http_get(address, serverInfo->httpsPort, build_serverinfo_path(resolve_client_unique_id(clientIdentity)), true, clientIdentity, {}, &authorizationResponse, &authorizationError)) {
      return true;
    }

    serverInfo->pairingStatusCurrentClientKnown = true;
    if (response_indicates_unpaired_client(authorizationResponse)) {
      serverInfo->pairingStatusCurrentClient = false;
      serverInfo->paired = false;
      return true;
    }

    HostPairingServerInfo authorizedServerInfo {};
    if (!network::parse_server_info_response(authorizationResponse.body, serverInfo->httpPort == 0 ? preferredHttpPort : serverInfo->httpPort, &authorizedServerInfo, nullptr)) {
      serverInfo->pairingStatusCurrentClientKnown = false;
      return true;
    }

    authorizedServerInfo.pairingStatusCurrentClientKnown = true;
    authorizedServerInfo.pairingStatusCurrentClient = true;
    *serverInfo = std::move(authorizedServerInfo);
    return true;
  }

  bool query_server_info(const std::string &address, uint16_t preferredHttpPort, HostPairingServerInfo *serverInfo, std::string *errorMessage) {
    return query_server_info(address, preferredHttpPort, nullptr, serverInfo, errorMessage);
  }

  bool error_indicates_unpaired_client(std::string_view errorMessage) {
    std::string normalized;
    normalized.reserve(errorMessage.size());
    for (char character : errorMessage) {
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }

    return normalized.find("no longer paired") != std::string::npos || normalized.find("pair the host again") != std::string::npos || normalized.find("not authorized") != std::string::npos || normalized.find("unauthorized") != std::string::npos || normalized.find("http 401") != std::string::npos || normalized.find("http 403") != std::string::npos;
  }

  std::string resolve_reachable_address(const std::string &requestedAddress, const HostPairingServerInfo &serverInfo) {
    if (!requestedAddress.empty()) {
      return requestedAddress;
    }
    if (!serverInfo.activeAddress.empty()) {
      return serverInfo.activeAddress;
    }
    if (!serverInfo.localAddress.empty()) {
      return serverInfo.localAddress;
    }
    if (!serverInfo.remoteAddress.empty()) {
      return serverInfo.remoteAddress;
    }

    return {};
  }

  bool query_app_list(
    const std::string &address,
    uint16_t preferredHttpPort,
    const PairingIdentity *clientIdentity,
    std::vector<HostAppEntry> *apps,
    HostPairingServerInfo *serverInfo,
    std::string *errorMessage
  ) {
    HostPairingServerInfo resolvedServerInfo {};
    if (!query_server_info(address, preferredHttpPort, clientIdentity, &resolvedServerInfo, errorMessage)) {
      return false;
    }

    if (serverInfo != nullptr) {
      *serverInfo = resolvedServerInfo;
    }

    HttpResponse response {};
    const uint16_t appListPort = resolvedServerInfo.httpsPort == 0 ? resolvedServerInfo.httpPort : resolvedServerInfo.httpsPort;
    if (const std::string appListAddress = resolve_reachable_address(address, resolvedServerInfo); !http_get(appListAddress, appListPort, build_applist_path(resolve_client_unique_id(clientIdentity)), true, clientIdentity, {}, &response, errorMessage)) {
      return false;
    }

    if (response.statusCode != 200) {
      if (response.statusCode == 401 || response.statusCode == 403) {
        return append_error(errorMessage, std::string(UNPAIRED_CLIENT_ERROR_MESSAGE));
      }
      return append_error(errorMessage, "The host returned HTTP " + std::to_string(response.statusCode) + " while requesting /applist");
    }

    if (std::string parseError; !parse_app_list_response(response.body, apps, &parseError)) {
      if (error_indicates_unpaired_client(parseError)) {
        return append_error(errorMessage, std::string(UNPAIRED_CLIENT_ERROR_MESSAGE));
      }
      return append_error(errorMessage, std::move(parseError));
    }

    if (serverInfo != nullptr) {
      serverInfo->pairingStatusCurrentClient = resolvedServerInfo.paired;
    }
    return true;
  }

  uint64_t hash_app_list_entries(const std::vector<HostAppEntry> &apps) {
    uint64_t hash = 1469598103934665603ULL;
    for (const HostAppEntry &entry : apps) {
      append_hash_string(&hash, entry.name);
      append_hash_bytes(&hash, reinterpret_cast<const unsigned char *>(&entry.id), sizeof(entry.id));
      append_hash_bytes(&hash, reinterpret_cast<const unsigned char *>(&entry.hdrSupported), sizeof(entry.hdrSupported));
      append_hash_bytes(&hash, reinterpret_cast<const unsigned char *>(&entry.hidden), sizeof(entry.hidden));
    }
    return hash;
  }

  bool query_app_asset(
    const std::string &address,
    uint16_t httpsPort,
    const PairingIdentity *clientIdentity,
    int appId,
    std::vector<unsigned char> *assetBytes,
    std::string *errorMessage
  ) {
    if (address.empty() || httpsPort == 0 || appId <= 0) {
      return append_error(errorMessage, "The app-asset request requires a valid host address, port, and app ID");
    }

    std::vector<std::string> attemptFailures;
    for (const std::string &path : build_app_asset_paths(resolve_client_unique_id(clientIdentity), appId)) {
      HttpResponse response {};
      if (std::string attemptError; !http_get(address, httpsPort, path, true, clientIdentity, {}, &response, &attemptError)) {
        attemptFailures.push_back(path + ": " + attemptError);
        continue;
      }

      if (response.statusCode != 200) {
        attemptFailures.push_back(path + ": HTTP " + std::to_string(response.statusCode));
        continue;
      }

      if (response.body.empty() || body_looks_like_xml(response.body)) {
        attemptFailures.push_back(path + ": returned placeholder XML instead of image data");
        continue;
      }

      if (assetBytes != nullptr) {
        assetBytes->assign(response.body.begin(), response.body.end());
      }
      return true;
    }

    std::string combinedMessage = "Failed to fetch app artwork for app ID " + std::to_string(appId);
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

  HostPairingResult pair_host(const HostPairingRequest &request, const std::atomic<bool> *cancelRequested) {  // NOSONAR(cpp:S3776) pairing protocol phases intentionally remain linear and explicit here
    trace_pairing_phase("pair_host entered");
    HostPairingResult result {false, false, "Pairing failed"};
    auto fail_if_cancelled = [&cancelRequested, &result]() {
      if (!pairing_cancel_requested(cancelRequested)) {
        return false;
      }

      result = {false, false, "Pairing cancelled"};
      trace_pairing_detail(result.message);
      return true;
    };
    auto fail_with_phase = [&result](std::string_view phase, const std::string &message) {
      result.message = "Pairing failed during " + std::string(phase) + ": " + message;
      trace_pairing_detail(result.message);
      return result;
    };
    auto next_pairing_uuid = [&result](std::string *uuid, std::string *errorMessage) {
      if (generate_uuid(uuid, errorMessage)) {
        return true;
      }

      result.message = errorMessage != nullptr && !errorMessage->empty() ? *errorMessage : "Failed to generate the UUID used for pairing";
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
    if (fail_if_cancelled()) {
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
    if (!query_server_info_internal(request.address, httpPort, uniqueId, &response, &serverInfo, &errorMessage, cancelRequested)) {
      return fail_with_phase("serverinfo", errorMessage);
    }
    if (fail_if_cancelled()) {
      return result;
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
    if (fail_if_cancelled()) {
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
    if (!http_get(request.address, serverInfo.httpPort, phase1Path, false, nullptr, {}, &response, &errorMessage, cancelRequested) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 1 (getservercert)", errorMessage);
    }
    if (fail_if_cancelled()) {
      return result;
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
    if (fail_if_cancelled()) {
      return result;
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
    if (!http_get(request.address, serverInfo.httpPort, phase2Path, false, nullptr, {}, &response, &errorMessage, cancelRequested) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 2 (client challenge)", errorMessage);
    }
    if (fail_if_cancelled()) {
      return result;
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
    if (!http_get(request.address, serverInfo.httpPort, phase3Path, false, nullptr, {}, &response, &errorMessage, cancelRequested) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 3 (server challenge response)", errorMessage);
    }
    if (fail_if_cancelled()) {
      return result;
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
    if (std::vector<unsigned char> serverSignature(pairingSecretBytes.begin() + 16, pairingSecretBytes.end()); !verify_sha256_signature(serverSecret, serverSignature, plainCertificate.get(), &errorMessage)) {
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
    if (!http_get(request.address, serverInfo.httpPort, phase4Path, false, nullptr, {}, &response, &errorMessage, cancelRequested) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 4 (client pairing secret)", errorMessage);
    }
    if (fail_if_cancelled()) {
      return result;
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
    if (!http_get(request.address, serverInfo.httpsPort, phase5Path, true, &request.identity, plainCertPem, &response, &errorMessage, cancelRequested) || !parse_pairing_tag(response, "paired", &phaseValue, &errorMessage)) {
      return fail_with_phase("phase 5 (pairchallenge)", errorMessage);
    }
    if (fail_if_cancelled()) {
      return result;
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
