#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Set the NXDK_DIR environment variable
NXDK_DIR="$(pwd)/third-party/nxdk"
export NXDK_DIR

# Activate the nxdk environment
eval "$("${NXDK_DIR}/bin/activate" -s)"

# Navigate to the nxdk directory
cd "${NXDK_DIR}"

# Build nxdk with the specified options
make NXDK_ONLY=y
make tools

# Navigate back to the project root directory
cd -

# Create the build directory
mkdir -p build

# Configure the project
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="${NXDK_DIR}/share/toolchain-nxdk.cmake"

# Build the project
cmake --build build
