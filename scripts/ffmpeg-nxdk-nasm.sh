#!/usr/bin/env sh

set -eu

# FFmpeg's freestanding target defaults to ELF. Xbox libraries require i386 COFF.
# A final format option overrides configure's default without changing its probes.
exec nasm "$@" -f win32
