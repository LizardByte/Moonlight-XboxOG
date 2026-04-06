#pragma once

// standard includes
#include <string>
#include <string_view>
#include <vector>

namespace startup {

  struct LoadCoverArtResult {
    std::vector<unsigned char> bytes;
    bool fileFound = false;
    std::string errorMessage;
  };

  struct SaveCoverArtResult {
    bool success = false;
    std::string errorMessage;
  };

  std::string default_cover_art_cache_root();

  std::string build_cover_art_cache_key(std::string_view hostUuid, std::string_view hostAddress, int appId);

  bool cover_art_exists(std::string_view cacheKey, const std::string &cacheRoot = default_cover_art_cache_root());

  LoadCoverArtResult load_cover_art(std::string_view cacheKey, const std::string &cacheRoot = default_cover_art_cache_root());

  SaveCoverArtResult save_cover_art(std::string_view cacheKey, const std::vector<unsigned char> &bytes, const std::string &cacheRoot = default_cover_art_cache_root());

}  // namespace startup

