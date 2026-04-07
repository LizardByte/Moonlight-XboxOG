#pragma once

// standard includes
#include <string>
#include <string_view>
#include <vector>

namespace startup {

  /**
   * @brief Result of loading cached cover art bytes from disk.
   */
  struct LoadCoverArtResult {
    std::vector<unsigned char> bytes;  ///< Loaded cover art bytes.
    bool fileFound = false;  ///< True when a cached file existed on disk.
    std::string errorMessage;  ///< Error detail when loading failed.
  };

  /**
   * @brief Result of saving cached cover art bytes to disk.
   */
  struct SaveCoverArtResult {
    bool success = false;  ///< True when the cover art was saved successfully.
    std::string errorMessage;  ///< Error detail when saving failed.
  };

  /**
   * @brief Return the default root directory for cached cover art.
   *
   * @return Default cover-art cache root path.
   */
  std::string default_cover_art_cache_root();

  /**
   * @brief Build a stable cache key for one host app's cover art.
   *
   * @param hostUuid Host UUID when available.
   * @param hostAddress Host address used as a fallback discriminator.
   * @param appId Host application identifier.
   * @return Stable cache key suitable for file-system storage.
   */
  std::string build_cover_art_cache_key(std::string_view hostUuid, std::string_view hostAddress, int appId);

  /**
   * @brief Return whether cached cover art exists for a given key.
   *
   * @param cacheKey Stable cover-art cache key.
   * @param cacheRoot Cache root directory to inspect.
   * @return true when cached art exists on disk.
   */
  bool cover_art_exists(std::string_view cacheKey, const std::string &cacheRoot = default_cover_art_cache_root());

  /**
   * @brief Delete cached cover art for a given key.
   *
   * @param cacheKey Stable cover-art cache key.
   * @param errorMessage Optional output for deletion failures.
   * @param cacheRoot Cache root directory containing the artifact.
   * @return true when the artifact was deleted or was already absent.
   */
  bool delete_cover_art(
    std::string_view cacheKey,
    std::string *errorMessage = nullptr,
    const std::string &cacheRoot = default_cover_art_cache_root()
  );

  /**
   * @brief Load cached cover art bytes for a given key.
   *
   * @param cacheKey Stable cover-art cache key.
   * @param cacheRoot Cache root directory containing the artifact.
   * @return Loaded cover-art bytes plus file-existence and error details.
   */
  LoadCoverArtResult load_cover_art(std::string_view cacheKey, const std::string &cacheRoot = default_cover_art_cache_root());

  /**
   * @brief Save cover art bytes for a given cache key.
   *
   * @param cacheKey Stable cover-art cache key.
   * @param bytes Cover art bytes to persist.
   * @param cacheRoot Cache root directory where the artifact should be written.
   * @return Save result including success state and error detail.
   */
  SaveCoverArtResult save_cover_art(std::string_view cacheKey, const std::vector<unsigned char> &bytes, const std::string &cacheRoot = default_cover_art_cache_root());

}  // namespace startup
