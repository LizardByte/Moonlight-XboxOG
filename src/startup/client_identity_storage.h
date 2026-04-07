#pragma once

// standard includes
#include <string>
#include <vector>

// local includes
#include "src/network/host_pairing.h"

namespace startup {

  /**
   * @brief Result of loading persisted client pairing identity material.
   */
  struct LoadClientIdentityResult {
    network::PairingIdentity identity;  ///< Loaded client identity, or an empty identity when unavailable.
    std::vector<std::string> warnings;  ///< Non-fatal warnings encountered while loading.
    bool fileFound;  ///< True when persisted identity files were present.
  };

  /**
   * @brief Result of saving client pairing identity material.
   */
  struct SaveClientIdentityResult {
    bool success;  ///< True when the identity was saved successfully.
    std::string errorMessage;  ///< Error detail when saving failed.
  };

  /**
   * @brief Return the default directory used to store pairing identity files.
   *
   * @return Default client identity directory path.
   */
  std::string default_client_identity_directory();

  /**
   * @brief Load persisted client pairing identity material from disk.
   *
   * @param directoryPath Directory containing the identity files.
   * @return Loaded identity plus any non-fatal warnings.
   */
  LoadClientIdentityResult load_client_identity(const std::string &directoryPath = default_client_identity_directory());

  /**
   * @brief Delete persisted client identity material from disk.
   *
   * @param errorMessage Optional output for deletion failures.
   * @param directoryPath Directory containing the identity files.
   * @return true when the files were removed or were already absent.
   */
  bool delete_client_identity(
    std::string *errorMessage = nullptr,
    const std::string &directoryPath = default_client_identity_directory()
  );

  /**
   * @brief Save client pairing identity material to disk.
   *
   * @param identity Client identity to persist.
   * @param directoryPath Directory where the identity files should be written.
   * @return Save result including success state and error detail.
   */
  SaveClientIdentityResult save_client_identity(
    const network::PairingIdentity &identity,
    const std::string &directoryPath = default_client_identity_directory()
  );

}  // namespace startup
