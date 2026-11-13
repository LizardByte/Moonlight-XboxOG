/**
 * @file src/network/socket_bio.cpp
 * @brief Implement a socket BIO independently of POSIX file descriptors.
 */
#include "src/network/socket_bio.h"

#include <memory>
#include <new>

namespace {

  /** @brief State owned by a BIO, referring to a caller-owned transport. */
  struct SocketBioState {
    network::SocketBioTransport transport;  ///< Socket callbacks and caller-owned context.
    bool eof = false;  ///< Whether a successful receive reported EOF.
  };

  /**
   * @brief Initialize an empty BIO with no ownership of its socket.
   * @param bio BIO to initialize.
   * @return One on success.
   */
  int create_socket_bio(BIO *bio) {
    BIO_set_data(bio, nullptr);
    BIO_set_init(bio, 0);
    BIO_set_shutdown(bio, BIO_NOCLOSE);
    return 1;
  }

  /**
   * @brief Release BIO state while leaving its socket with the caller.
   * @param bio BIO to release.
   * @return One when the BIO exists, otherwise zero.
   */
  int destroy_socket_bio(BIO *bio) {
    if (bio == nullptr) {
      return 0;
    }
    delete static_cast<SocketBioState *>(BIO_get_data(bio));
    BIO_set_data(bio, nullptr);
    BIO_set_init(bio, 0);
    return 1;
  }

  /**
   * @brief Receive bytes and expose retryable transport failures to OpenSSL.
   * @param bio Initialized socket BIO.
   * @param buffer Destination bytes.
   * @param size Destination capacity.
   * @return Received byte count, zero at EOF, or a negative error.
   */
  int read_socket_bio(BIO *bio, char *buffer, int size) {
    BIO_clear_retry_flags(bio);
    auto *state = static_cast<SocketBioState *>(BIO_get_data(bio));
    if (state == nullptr || buffer == nullptr || size <= 0) {
      return 0;
    }
    const int result = state->transport.receive(state->transport.context, buffer, size);
    if (result < 0 && state->transport.retry(state->transport.context)) {
      BIO_set_retry_read(bio);
    }
    if (result == 0) {
      state->eof = true;
    }
    return result;
  }

  /**
   * @brief Send bytes and expose retryable transport failures to OpenSSL.
   * @param bio Initialized socket BIO.
   * @param buffer Bytes to send.
   * @param size Number of bytes to send.
   * @return Sent byte count, or a negative error.
   */
  int write_socket_bio(BIO *bio, const char *buffer, int size) {
    BIO_clear_retry_flags(bio);
    auto *state = static_cast<SocketBioState *>(BIO_get_data(bio));
    if (state == nullptr || buffer == nullptr || size <= 0) {
      return 0;
    }
    const int result = state->transport.send(state->transport.context, buffer, size);
    if (result < 0 && state->transport.retry(state->transport.context)) {
      BIO_set_retry_write(bio);
    }
    return result;
  }

  /**
   * @brief Handle socket BIO state queries without providing seek or ownership transfer.
   * @param bio Initialized socket BIO.
   * @param command OpenSSL control command.
   * @param value Requested control value.
   * @param pointer Unused control data.
   * @return Control result, or zero for unsupported operations.
   */
  long control_socket_bio(BIO *bio, int command, long value, void *pointer) {
    (void) pointer;
    const auto *state = static_cast<SocketBioState *>(BIO_get_data(bio));
    switch (command) {
      case BIO_CTRL_FLUSH:
        return 1;
      case BIO_CTRL_EOF:
        return state != nullptr && state->eof;
      case BIO_CTRL_GET_CLOSE:
        return BIO_NOCLOSE;
      case BIO_CTRL_SET_CLOSE:
        return value == BIO_NOCLOSE;
      default:
        return 0;
    }
  }

  /**
   * @brief Return the shared method table, with cleanup at process exit.
   * @return Initialized method table, or nullptr on allocation/setup failure.
   */
  const BIO_METHOD *socket_bio_method() {
    static const std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> method([] {
      BIO_METHOD *result = BIO_meth_new(BIO_TYPE_SOURCE_SINK | BIO_get_new_index(), "Moonlight socket");
      if (result != nullptr && (!BIO_meth_set_create(result, create_socket_bio) || !BIO_meth_set_destroy(result, destroy_socket_bio) || !BIO_meth_set_read(result, read_socket_bio) || !BIO_meth_set_write(result, write_socket_bio) || !BIO_meth_set_ctrl(result, control_socket_bio))) {
        BIO_meth_free(result);
        return static_cast<BIO_METHOD *>(nullptr);
      }
      return result;
    }(),
                                                                              BIO_meth_free);
    return method.get();
  }

}  // namespace

namespace network {

  BIO *make_socket_bio(SocketBioTransport transport) {
    const BIO_METHOD *method = socket_bio_method();
    if (method == nullptr || transport.receive == nullptr || transport.send == nullptr || transport.retry == nullptr) {
      return nullptr;
    }
    auto state = std::unique_ptr<SocketBioState>(new (std::nothrow) SocketBioState {transport});
    if (state == nullptr) {
      return nullptr;
    }
    BIO *bio = BIO_new(method);
    if (bio != nullptr) {
      BIO_set_data(bio, state.release());
      BIO_set_init(bio, 1);
    }
    return bio;
  }

}  // namespace network
