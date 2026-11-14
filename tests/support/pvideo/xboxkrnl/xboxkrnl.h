/**
 * @file tests/support/pvideo/xboxkrnl/xboxkrnl.h
 * @brief Declares tracked contiguous-memory operations for presenter lifecycle tests.
 */
#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Allocate tracked DMA storage.
 * @param size Allocation size.
 * @param minimum Minimum physical address.
 * @param maximum Maximum physical address.
 * @param alignment Requested alignment.
 * @param flags Memory attributes.
 * @return Allocated storage or null when failure is injected.
 */
void *MmAllocateContiguousMemoryEx(std::size_t size, unsigned int minimum, unsigned int maximum, unsigned int alignment, unsigned int flags);
/**
 * @brief Free tracked DMA storage.
 *
 * @param buffer Allocation to release.
 */
void MmFreeContiguousMemory(void *buffer);
/**
 * @brief Get a deterministic physical address.
 *
 * @param buffer Allocated storage.
 * @return Fake DMA address.
 */
std::uintptr_t MmGetPhysicalAddress(void *buffer);
