/**
 * @file src/streaming/aligned_media_buffer.h
 * @brief Provides aligned, padded storage for buffers passed to FFmpeg SIMD routines.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace streaming {
  /**
   * @brief Owns byte storage with a 64-byte-aligned start and zeroed read padding.
   */
  class AlignedMediaBuffer {
  public:
    static constexpr std::size_t PADDING = 64;  ///< FFmpeg's required input overread padding.

    /**
     * @brief Construct an empty media buffer.
     */
    AlignedMediaBuffer() = default;
    AlignedMediaBuffer(const AlignedMediaBuffer &) = delete;
    AlignedMediaBuffer &operator=(const AlignedMediaBuffer &) = delete;

    /**
     * @brief Transfer an aligned allocation without relocating its payload.
     *
     * @param other Buffer relinquishing its allocation.
     */
    AlignedMediaBuffer(AlignedMediaBuffer &&other) noexcept:
        storage_(std::move(other.storage_)),
        size_(std::exchange(other.size_, 0)) {
    }

    /**
     * @brief Replace this allocation with another buffer's aligned storage.
     *
     * @param other Buffer relinquishing its allocation.
     *
     * @return This buffer after ownership transfer.
     */
    AlignedMediaBuffer &operator=(AlignedMediaBuffer &&other) noexcept {
      if (this != &other) {
        storage_ = std::move(other.storage_);
        size_ = std::exchange(other.size_, 0);
      }
      return *this;
    }

    /**
     * @brief Resize storage, discarding prior payload contents and zeroing trailing read padding.
     *
     * @param size Required payload bytes.
     */
    void resize(std::size_t size) {
      if (size > std::numeric_limits<std::size_t>::max() - PADDING - 63) {
        std::abort();
      }
      storage_.resize(size + PADDING + 63);
      size_ = size;
      std::memset(data() + size_, 0, PADDING);
    }

    /**
     * @brief Return the aligned payload address.
     * @return Payload storage, or nullptr for an empty buffer.
     */
    std::uint8_t *data() {
      return storage_.empty() ? nullptr : reinterpret_cast<std::uint8_t *>((reinterpret_cast<std::uintptr_t>(storage_.data()) + 63U) & ~std::uintptr_t {63});
    }

    /**
     * @brief Return the aligned read-only payload address.
     * @return Payload storage, or nullptr for an empty buffer.
     */
    const std::uint8_t *data() const {
      return storage_.empty() ? nullptr : reinterpret_cast<const std::uint8_t *>((reinterpret_cast<std::uintptr_t>(storage_.data()) + 63U) & ~std::uintptr_t {63});
    }

    /**
     * @brief Return the payload size excluding padding.
     * @return Usable byte count.
     */
    std::size_t size() const {
      return size_;
    }

    /**
     * @brief Clear the payload while retaining reusable allocation capacity.
     */
    void clear() {
      storage_.clear();
      size_ = 0;
    }

  private:
    std::vector<std::uint8_t> storage_;  ///< Overallocated backing storage.
    std::size_t size_ = 0;  ///< Payload length before the zero padding.
  };
}  // namespace streaming
