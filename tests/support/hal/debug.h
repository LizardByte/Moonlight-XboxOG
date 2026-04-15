/**
 * @file tests/support/hal/debug.h
 * @brief Provides test support for a host-side nxdk debug shim.
 */
#pragma once

// standard includes
#include <cstdio>

#define debugPrint(...) std::printf(__VA_ARGS__)
