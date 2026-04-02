#!/usr/bin/env bash

set -euo pipefail

try_root() {
    local candidate_root="$1"

    if [[ -z "$candidate_root" ]]; then
        return 1
    fi

    if [[ -f "$candidate_root/msys2_shell.cmd" ]]; then
        printf '%s\n' "$candidate_root"
        return 0
    fi

    return 1
}

try_from_tool() {
    local tool_path="$1"
    local tool_dir

    if [[ -z "$tool_path" ]]; then
        return 1
    fi

    tool_dir="$(cd "$(dirname "$tool_path")" && pwd)"

    if [[ "$(basename "$tool_path")" == "msys2_shell.cmd" ]]; then
        try_root "$tool_dir" && return 0
    fi

    try_root "$(cd "$tool_dir/../.." 2>/dev/null && pwd)" && return 0
    try_root "$(cd "$tool_dir/.." 2>/dev/null && pwd)" && return 0
    return 1
}

if try_root "${MSYS2_ROOT:-}"; then
    exit 0
fi

if [[ -n "${SYSTEMDRIVE:-}" ]]; then
    system_drive_letter="$(printf '%s' "$SYSTEMDRIVE" | cut -c1 | tr '[:upper:]' '[:lower:]')"
    if try_root "/${system_drive_letter}/msys64"; then
        exit 0
    fi
fi

for candidate_root in /c/msys64 /c/tools/msys64; do
    if try_root "$candidate_root"; then
        exit 0
    fi
done

for tool_name in msys2_shell.cmd bash.exe mingw32-make.exe clang++.exe clang.exe; do
    if tool_path="$(command -v "$tool_name" 2>/dev/null)" && try_from_tool "$tool_path"; then
        exit 0
    fi
done

echo 'MSYS2 installation not found. Set MSYS2_ROOT or add MSYS2 tools to PATH.' >&2
exit 1
