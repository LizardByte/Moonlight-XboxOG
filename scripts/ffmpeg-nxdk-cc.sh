#!/usr/bin/env sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ffmpeg_compat_header="${script_dir}/../src/_nxdk_compat/ffmpeg_compat.h"
ffmpeg_compat_include_dir="${script_dir}/../src/_nxdk_compat"

compile_only=0
output_path=
previous_arg=
for arg in "$@"; do
  case "$arg" in
    -c|-E)
      compile_only=1
      ;;
  esac

  if [ "$previous_arg" = "-o" ]; then
    output_path="$arg"
    previous_arg=
    continue
  fi

  previous_arg="$arg"
done

if [ "$compile_only" -eq 1 ]; then
  exec clang \
    -target i386-pc-win32 \
    -march=pentium3 \
    -ffreestanding \
    -nostdlib \
    -fno-builtin \
    -include "${ffmpeg_compat_header}" \
    -I"${ffmpeg_compat_include_dir}" \
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
    "$@"
fi

if [ -n "$output_path" ]; then
  : > "$output_path"
  exit 0
fi

exec clang \
  -target i386-pc-win32 \
  -march=pentium3 \
  -ffreestanding \
  -nostdlib \
  -fno-builtin \
  -include "${ffmpeg_compat_header}" \
  -I"${ffmpeg_compat_include_dir}" \
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
  "$@"
