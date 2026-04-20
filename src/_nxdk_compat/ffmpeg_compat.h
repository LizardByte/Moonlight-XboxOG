/**
 * @file src/_nxdk_compat/ffmpeg_compat.h
 * @brief Provides nxdk compatibility shims for the FFmpeg Xbox build.
 */

#pragma once

#ifdef NXDK

  #include <errno.h>
  #include <stdarg.h>
  #include <stddef.h>
  #include <stdint.h>
  #include <stdio.h>
  #include <string.h>
  #include <sys/types.h>
  #include <wchar.h>

  #ifdef __cplusplus
extern "C" {
  #endif

  #ifndef ENOSYS
    #define ENOSYS 38
  #endif

  #ifndef O_BINARY
    #define O_BINARY 0
  #endif

  #ifndef F_SETFD
    #define F_SETFD 2
  #endif

  #ifndef FD_CLOEXEC
    #define FD_CLOEXEC 1
  #endif

  #ifndef CP_ACP
    #define CP_ACP 0U
  #endif

  #ifndef CP_UTF8
    #define CP_UTF8 65001U
  #endif

  #ifndef MB_ERR_INVALID_CHARS
    #define MB_ERR_INVALID_CHARS 0x00000008UL
  #endif

  #ifndef WC_ERR_INVALID_CHARS
    #define WC_ERR_INVALID_CHARS 0x00000080UL
  #endif

  /** @brief Redirect access to the nxdk FFmpeg compatibility shim. */
  #define access moonlight_nxdk_ffmpeg_access
  /** @brief Redirect close to the nxdk FFmpeg compatibility shim. */
  #define close moonlight_nxdk_ffmpeg_close
  /** @brief Redirect fcntl to the nxdk FFmpeg compatibility shim. */
  #define fcntl moonlight_nxdk_ffmpeg_fcntl
  /** @brief Redirect fdopen to the nxdk FFmpeg compatibility shim. */
  #define fdopen moonlight_nxdk_ffmpeg_fdopen
  /** @brief Redirect isatty to the nxdk FFmpeg compatibility shim. */
  #define isatty moonlight_nxdk_ffmpeg_isatty
  /** @brief Redirect GetFullPathNameW to the nxdk FFmpeg compatibility shim. */
  #define GetFullPathNameW moonlight_nxdk_ffmpeg_GetFullPathNameW
  /** @brief Redirect mkstemp to the nxdk FFmpeg compatibility shim. */
  #define mkstemp moonlight_nxdk_ffmpeg_mkstemp
  /** @brief Redirect MultiByteToWideChar to the nxdk FFmpeg compatibility shim. */
  #define MultiByteToWideChar moonlight_nxdk_ffmpeg_MultiByteToWideChar
  /** @brief Redirect open to the nxdk FFmpeg compatibility shim. */
  #define open moonlight_nxdk_ffmpeg_open
  /** @brief Redirect strerror_r to the nxdk FFmpeg compatibility shim. */
  #define strerror_r moonlight_nxdk_ffmpeg_strerror_r
  /** @brief Redirect tempnam to the nxdk FFmpeg compatibility shim. */
  #define tempnam moonlight_nxdk_ffmpeg_tempnam
  /** @brief Redirect usleep to the nxdk FFmpeg compatibility shim. */
  #define usleep moonlight_nxdk_ffmpeg_usleep
  /** @brief Redirect WideCharToMultiByte to the nxdk FFmpeg compatibility shim. */
  #define WideCharToMultiByte moonlight_nxdk_ffmpeg_WideCharToMultiByte

  /**
   * @brief Report unsupported path access checks during FFmpeg builds.
   *
   * @param path Requested file system path.
   * @param mode Requested access mode.
   * @return Always -1 with errno set to ENOSYS.
   */
  static inline int moonlight_nxdk_ffmpeg_access(const char *path, int mode) {
    (void) path;
    (void) mode;
    errno = ENOSYS;
    return -1;
  }

  /**
   * @brief Treat descriptor close requests as successful during FFmpeg builds.
   *
   * @param fd Descriptor to close.
   * @return Always 0.
   */
  static inline int moonlight_nxdk_ffmpeg_close(int fd) {
    (void) fd;
    return 0;
  }

  /**
   * @brief Report unsupported fcntl requests during FFmpeg builds.
   *
   * @param fd Descriptor to operate on.
   * @param cmd fcntl command.
   * @param ... Ignored command arguments.
   * @return Always -1 with errno set to ENOSYS.
   */
  static inline int moonlight_nxdk_ffmpeg_fcntl(int fd, int cmd, ...) {
    (void) fd;
    (void) cmd;
    errno = ENOSYS;
    return -1;
  }

  /**
   * @brief Report unsupported descriptor-backed stdio requests during FFmpeg builds.
   *
   * @param fd Descriptor to convert.
   * @param mode Requested fopen mode string.
   * @return Always NULL with errno set to ENOSYS.
   */
  static inline FILE *moonlight_nxdk_ffmpeg_fdopen(int fd, const char *mode) {
    (void) fd;
    (void) mode;
    errno = ENOSYS;
    return NULL;
  }

  /**
   * @brief Report that FFmpeg output is not connected to a terminal.
   *
   * @param fd Descriptor to inspect.
   * @return Always 0.
   */
  static inline int moonlight_nxdk_ffmpeg_isatty(int fd) {
    (void) fd;
    return 0;
  }

  /**
   * @brief Provide a minimal high-resolution timer fallback for FFmpeg.
   *
   * @return A monotonic placeholder timestamp value.
   */
  static inline int64_t gethrtime(void) {
    return 0;
  }

  /**
   * @brief Provide a minimal GetFullPathNameW fallback for FFmpeg path normalization.
   *
   * @param path Source wide-character path.
   * @param buffer_size Destination buffer capacity in wide characters.
   * @param buffer Destination buffer.
   * @param file_part Optional pointer to the filename portion.
   * @return Required or written character count, including the terminating null.
   */
  static inline unsigned long moonlight_nxdk_ffmpeg_GetFullPathNameW(const wchar_t *path, unsigned long buffer_size, wchar_t *buffer, wchar_t **file_part) {
    size_t length;
    wchar_t *last_separator;

    if (path == NULL) {
      return 0;
    }

    length = wcslen(path);
    last_separator = NULL;
    for (size_t index = 0; index < length; ++index) {
      if (path[index] == L'\\' || path[index] == L'/') {
        last_separator = (wchar_t *) &path[index + 1U];
      }
    }

    if (file_part != NULL) {
      *file_part = last_separator;
    }

    if (buffer == NULL || buffer_size == 0U) {
      return (unsigned long) (length + 1U);
    }

    if (length + 1U > buffer_size) {
      if (buffer_size > 0U) {
        wcsncpy(buffer, path, buffer_size - 1U);
        buffer[buffer_size - 1U] = L'\0';
      }
      return (unsigned long) (length + 1U);
    }

    wcscpy(buffer, path);
    return (unsigned long) length;
  }

  /**
   * @brief Report unsupported temporary file creation during FFmpeg builds.
   *
   * @param pattern Writable mkstemp pattern.
   * @return Always -1 with errno set to ENOSYS.
   */
  static inline int moonlight_nxdk_ffmpeg_mkstemp(char *pattern) {
    (void) pattern;
    errno = ENOSYS;
    return -1;
  }

  /**
   * @brief Provide a minimal UTF-8 to wchar_t conversion fallback.
   *
   * @param code_page Requested Windows code page.
   * @param flags Requested conversion flags.
   * @param source Source multibyte string.
   * @param source_length Length of @p source or -1 for null-terminated input.
   * @param destination Destination wide-character buffer.
   * @param destination_length Capacity of @p destination in wide characters.
   * @return Required or written character count, including the terminating null.
   */
  static inline int moonlight_nxdk_ffmpeg_MultiByteToWideChar(unsigned int code_page, unsigned long flags, const char *source, int source_length, wchar_t *destination, int destination_length) {
    size_t length;

    (void) code_page;
    (void) flags;

    if (source == NULL) {
      return 0;
    }

    length = source_length >= 0 ? (size_t) source_length : strlen(source) + 1U;
    if (destination == NULL || destination_length <= 0) {
      return (int) length;
    }

    if ((size_t) destination_length < length) {
      return 0;
    }

    for (size_t index = 0; index < length; ++index) {
      destination[index] = (unsigned char) source[index];
    }

    return (int) length;
  }

  /**
   * @brief Report unsupported file opening during FFmpeg builds.
   *
   * @param path Requested file system path.
   * @param flags Requested open flags.
   * @param ... Ignored mode argument.
   * @return Always -1 with errno set to ENOSYS.
   */
  static inline int moonlight_nxdk_ffmpeg_open(const char *path, int flags, ...) {
    (void) path;
    (void) flags;
    errno = ENOSYS;
    return -1;
  }

  /**
   * @brief Provide a simple strerror_r fallback backed by strerror.
   *
   * @param errnum Error number to describe.
   * @param buffer Destination character buffer.
   * @param buffer_size Size of @p buffer in bytes.
   * @return Zero when the buffer is usable, otherwise -1.
   */
  static inline int moonlight_nxdk_ffmpeg_strerror_r(int errnum, char *buffer, size_t buffer_size) {
    const char *message;

    if (buffer == NULL || buffer_size == 0) {
      errno = EINVAL;
      return -1;
    }

    message = strerror(errnum);
    if (message == NULL) {
      message = "Unknown error";
    }

    strncpy(buffer, message, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
    return 0;
  }

  /**
   * @brief Report unsupported tempnam requests during FFmpeg builds.
   *
   * @param dir Ignored preferred directory.
   * @param prefix Ignored preferred file prefix.
   * @return Always NULL with errno set to ENOSYS.
   */
  static inline char *moonlight_nxdk_ffmpeg_tempnam(const char *dir, const char *prefix) {
    (void) dir;
    (void) prefix;
    errno = ENOSYS;
    return NULL;
  }

  /**
   * @brief Provide a no-op microsecond sleep fallback for FFmpeg.
   *
   * @param usec Requested sleep duration in microseconds.
   * @return Always 0.
   */
  static inline int moonlight_nxdk_ffmpeg_usleep(unsigned int usec) {
    (void) usec;
    return 0;
  }

  /**
   * @brief Provide a minimal wchar_t to multibyte conversion fallback.
   *
   * @param code_page Requested Windows code page.
   * @param flags Requested conversion flags.
   * @param source Source wide-character string.
   * @param source_length Length of @p source or -1 for null-terminated input.
   * @param destination Destination multibyte buffer.
   * @param destination_length Capacity of @p destination in bytes.
   * @param default_char Ignored Windows default character pointer.
   * @param used_default_char Ignored Windows default-character output flag.
   * @return Required or written character count, including the terminating null.
   */
  static inline int moonlight_nxdk_ffmpeg_WideCharToMultiByte(unsigned int code_page, unsigned long flags, const wchar_t *source, int source_length, char *destination, int destination_length, const char *default_char, int *used_default_char) {
    size_t length;

    (void) code_page;
    (void) flags;
    (void) default_char;
    if (used_default_char != NULL) {
      *used_default_char = 0;
    }

    if (source == NULL) {
      return 0;
    }

    length = source_length >= 0 ? (size_t) source_length : wcslen(source) + 1U;
    if (destination == NULL || destination_length <= 0) {
      return (int) length;
    }

    if ((size_t) destination_length < length) {
      return 0;
    }

    for (size_t index = 0; index < length; ++index) {
      destination[index] = (char) source[index];
    }

    return (int) length;
  }

  #ifdef __cplusplus
}
  #endif

#endif
