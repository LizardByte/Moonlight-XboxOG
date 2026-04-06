#pragma once

// standard includes
#include <string>
#include <vector>

// local includes
#include "src/app/host_records.h"

namespace startup {

  struct LoadSavedHostsResult {
    std::vector<app::HostRecord> hosts;
    std::vector<std::string> warnings;
    bool fileFound;
  };

  struct SaveSavedHostsResult {
    bool success;
    std::string errorMessage;
  };

  std::string default_host_storage_path();

  LoadSavedHostsResult load_saved_hosts(const std::string &filePath = default_host_storage_path());

  SaveSavedHostsResult save_saved_hosts(
    const std::vector<app::HostRecord> &hosts,
    const std::string &filePath = default_host_storage_path()
  );

}  // namespace startup
