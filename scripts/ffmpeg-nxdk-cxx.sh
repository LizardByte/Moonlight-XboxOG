#!/usr/bin/env sh

set -eu

unset MSYS2_ARG_CONV_EXCL
compat_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/../src/_nxdk_compat" && pwd)

link=1
for arg in "$@"; do
  case "$arg" in
    -c|-E|-S|-M|-MM) link=0 ;;
  esac
done

if [ "$link" -eq 1 ]; then
  # Configure probes must resolve symbols against the actual Xbox libraries.
  # These cross-compiled probes use main as their entry point and are never run.
  set -- "$@" -Wl,-entry:main,-subsystem:windows \
    "${NXDK_DIR}/lib/libpdclib.lib" \
    "${NXDK_DIR}/lib/libxboxrt.lib" \
    "${NXDK_DIR}/lib/libwinapi.lib" \
    "${NXDK_DIR}/lib/libnxdk_hal.lib" \
    "${NXDK_DIR}/lib/xboxkrnl/libxboxkrnl.lib" \
    "${NXDK_DIR}/lib/libc++.lib"
fi

# Retain the compiler's COFF and Win32 platform definitions for nxdk libc++.
exec clang \
  -target i386-pc-win32 \
  -march=pentium3 \
  -fuse-ld=lld \
  -ffreestanding \
  -nostdlib \
  -fno-builtin \
  -I"${NXDK_DIR}/lib/libcxx/include" \
  -I"${compat_dir}/include" \
  -include "${compat_dir}/ffmpeg_compat.h" \
  -I"${NXDK_DIR}/lib" \
  -I"${NXDK_DIR}/lib/xboxrt/libc_extensions" \
  -isystem "${NXDK_DIR}/lib/pdclib/include" \
  -I"${NXDK_DIR}/lib/pdclib/platform/xbox/include" \
  -I"${NXDK_DIR}/lib/winapi" \
  -I"${NXDK_DIR}/lib/xboxrt/vcruntime" \
  -Wno-builtin-macro-redefined \
  -DNXDK \
  -D__STDC__=1 \
  -U__STDC_NO_THREADS__ \
  -fno-exceptions \
  "$@"
