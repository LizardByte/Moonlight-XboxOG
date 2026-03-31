#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 ]]; then
    printf 'Usage: %s <nxdk-dir> <tool-name> [args...]\n' "$0" >&2
    exit 2
fi

export PATH="/mingw64/bin:/usr/bin:${PATH}"

WRAPPER_DIR="$(cd "$(dirname "$0")" && pwd)"
NXDK_DIR="$(cygpath -u "$1")"
TOOL_NAME="$2"
shift 2

export MSYSTEM="MINGW64"
export CHERE_INVOKING="1"
export NXDK_DIR
export PATH="${WRAPPER_DIR}:${NXDK_DIR}/bin:${PATH}"

exec "${NXDK_DIR}/bin/${TOOL_NAME}" "$@"
