/**
 * @file src/platform/error_utils.h
 * @brief Declares error formatting helpers.
 */
#pragma once

// standard includes
#include <string>
#include <utility>

namespace platform {

  /**
   * @brief Store an error message when requested and return false for chaining.
   *
   * @param errorMessage Optional destination for the error text.
   * @param message Error message to store.
   * @return Always false.
   */
  inline bool append_error(std::string *errorMessage, std::string message) {
    if (errorMessage != nullptr) {
      *errorMessage = std::move(message);
    }

    return false;
  }

}  // namespace platform
