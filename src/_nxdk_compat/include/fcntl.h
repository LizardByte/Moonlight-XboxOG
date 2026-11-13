/**
 * @file src/_nxdk_compat/include/fcntl.h
 * @brief Provide the flags needed by dependency code and native lwIP fcntl.
 */
#pragma once
/** @brief Read-only file access. */
#ifndef O_RDONLY
  #define O_RDONLY 2
#endif
/** @brief Write-only file access. */
#ifndef O_WRONLY
  #define O_WRONLY 4
#endif
/** @brief Read/write file access. */
#ifndef O_RDWR
  #define O_RDWR (O_RDONLY | O_WRONLY)
#endif
/** @brief Create a file. */
#define O_CREAT 0x100
/** @brief Truncate a file. */
#define O_TRUNC 0x200
/** @brief Append to a file. */
#define O_APPEND 0x400
/** @brief Require exclusive creation. */
#define O_EXCL 0x800
