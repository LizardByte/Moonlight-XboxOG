#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -euo pipefail

usage() {
  echo "Usage: $0 [--build-dir <dir> | <dir>] [--clean|clean] [--distclean|distclean]"
  return 0
}

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="build"
CLEAN_BUILD=0
DISTCLEAN_BUILD=0
POSITIONAL_BUILD_DIR_SET=0

while [[ "$#" -gt 0 ]]; do
  case "$1" in
    --build-dir|-B)
      if [[ "$#" -lt 2 ]]; then
        usage
        exit 1
      fi
      BUILD_DIR="$2"
      POSITIONAL_BUILD_DIR_SET=1
      shift 2
      ;;
    --clean|clean)
      CLEAN_BUILD=1
      shift
      ;;
    --distclean|distclean)
      DISTCLEAN_BUILD=1
      CLEAN_BUILD=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      if [[ "${POSITIONAL_BUILD_DIR_SET}" -eq 0 ]]; then
        BUILD_DIR="$1"
        POSITIONAL_BUILD_DIR_SET=1
        shift
      else
        echo "Unknown argument: $1"
        usage
        exit 1
      fi
      ;;
  esac
done

case "${BUILD_DIR}" in
  /*)
    BUILD_DIR_PATH="${BUILD_DIR}"
    ;;
  *)
    BUILD_DIR_PATH="${PROJECT_ROOT}/${BUILD_DIR}"
    ;;
esac

# Set the NXDK_DIR environment variable
export NXDK_DIR="${NXDK_DIR:-${PROJECT_ROOT}/third-party/nxdk}"

if [[ ! -d "${NXDK_DIR}" ]]; then
  echo "NXDK directory not found: ${NXDK_DIR}"
  echo "Run: git submodule update --init --recursive"
  exit 1
fi

# Activate the nxdk environment
eval "$("${NXDK_DIR}/bin/activate" -s)"

# Navigate to the nxdk directory
cd "${NXDK_DIR}"

if [[ "${DISTCLEAN_BUILD}" -eq 1 ]]; then
  make distclean
fi

# Build nxdk with the specified options
make NXDK_ONLY=y
make tools

cd "${PROJECT_ROOT}"

if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
  rm -rf "${BUILD_DIR_PATH}"
fi

# Configure the project
cmake \
  -G "Unix Makefiles" \
  -B "${BUILD_DIR_PATH}" \
  -S . \
  -DBUILD_DOCS=OFF \
  -DCMAKE_TOOLCHAIN_FILE="${NXDK_DIR}/share/toolchain-nxdk.cmake" \
  -DCMAKE_DEPENDS_USE_COMPILER=FALSE

# Build the project
cmake --build "${BUILD_DIR_PATH}"
