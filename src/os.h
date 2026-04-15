/**
 * @file src/os.h
 * @brief Declares platform path constants for the Xbox target.
 */
#pragma once

inline constexpr auto PATH_SEP = "\\";  ///< Preferred path separator for persisted Xbox paths.
inline constexpr auto DATA_PATH = "D:\\";  ///< Root data partition used for persisted Moonlight files on Xbox.
