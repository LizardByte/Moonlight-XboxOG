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
