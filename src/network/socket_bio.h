/**
 * @file src/network/socket_bio.h
 * @brief Declare an OpenSSL BIO backed by caller-owned socket operations.
 */
#pragma once
#include <openssl/bio.h>

namespace network {

  /** @brief Socket operations and state that outlive the returned BIO. */
  struct SocketBioTransport {
    void *context;  ///< Caller-owned socket state; never freed by the BIO.
    int (*receive)(void *, char *, int);  ///< Receive bytes, returning zero at EOF or a negative error.
    int (*send)(void *, const char *, int);  ///< Send bytes, returning a negative value on error.
    bool (*retry)(void *);  ///< Whether the last failure permits a later retry.
  };

  /**
   * @brief Create a socket BIO without requiring OpenSSL's POSIX descriptor support.
   *
   * The caller retains ownership of the socket. BIO_free never closes it.
   * @param transport Socket callbacks and state, valid until BIO_free.
   * @return Owned BIO, or nullptr when callbacks are missing or allocation fails.
   */
  BIO *make_socket_bio(SocketBioTransport transport);

}  // namespace network
