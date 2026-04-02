#!/bin/sh

set -eu

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
MSYS2_ROOT="$(sh "${SCRIPT_DIR}/scripts/find-msys2.sh")"
MSYS2_SHELL="${MSYS2_ROOT}/msys2_shell.cmd"

if [ ! -f "${MSYS2_SHELL}" ]; then
	echo "MSYS2 shell not found at ${MSYS2_SHELL}"
	exit 1
fi

BUILD_ARGS=""
for ARG in "$@"; do
	ESCAPED_ARG=$(printf "%s" "${ARG}" | sed "s/'/'\\''/g")
	BUILD_ARGS="${BUILD_ARGS} '${ESCAPED_ARG}'"
done

cd "${SCRIPT_DIR}"
exec "${MSYS2_SHELL}" -defterm -here -no-start -mingw64 -c "./build.sh${BUILD_ARGS}"
