#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: run-xemu.sh [--check] [--iso path]
EOF
}

is_windows() {
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) return 0 ;;
        *) return 1 ;;
    esac
}

to_native_path() {
    if is_windows; then
        cygpath -w "$1"
    else
        printf '%s\n' "$1"
    fi
}

escape_toml_string() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    printf '%s' "$value"
}

write_xemu_config() {
    local config_path="$1"
    local games_dir="$2"
    local bootrom_path="$3"
    local flashrom_path="$4"
    local eeprom_path="$5"
    local hdd_path="$6"

    mkdir -p "$(dirname "$config_path")"

    {
        printf '[general]\n'
        printf 'show_welcome = false\n'
        printf 'games_dir = "%s"\n' "$(escape_toml_string "$games_dir")"
        printf 'skip_boot_anim = true\n'
        printf '\n[sys.files]\n'
        printf 'bootrom_path = "%s"\n' "$(escape_toml_string "$bootrom_path")"
        printf 'flashrom_path = "%s"\n' "$(escape_toml_string "$flashrom_path")"
        printf 'eeprom_path = "%s"\n' "$(escape_toml_string "$eeprom_path")"
        printf 'hdd_path = "%s"\n' "$(escape_toml_string "$hdd_path")"
    } > "$config_path"
}

require_file() {
    local label="$1"
    local path="$2"

    if [[ -z "$path" || ! -f "$path" ]]; then
        printf '%s not found: %s\n' "$label" "$path" >&2
        return 1
    fi
}

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
setup_script="$project_root/scripts/setup-xemu.sh"
manifest_sh="$project_root/.local/xemu/paths.sh"
iso_path="$project_root/Moonlight.iso"
check_only=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check)
            check_only=1
            ;;
        --iso)
            shift
            if [[ $# -eq 0 ]]; then
                echo 'Missing value for --iso' >&2
                exit 2
            fi
            iso_path="$1"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ ! -f "$manifest_sh" ]]; then
    "$setup_script"
fi

# shellcheck disable=SC1090
source "$manifest_sh"

if [[ -z "${XEMU_EXE:-}" || ! -e "$XEMU_EXE" ]]; then
    "$setup_script"
    # shellcheck disable=SC1090
    source "$manifest_sh"
fi

if [[ ! -f "$iso_path" ]]; then
    echo "ISO not found: $iso_path" >&2
    exit 1
fi

xemu_exe="$XEMU_EXE"
games_dir="$project_root"
bootrom_path="${XEMU_BOOTROM_PATH:-}"
flashrom_path="${XEMU_FLASHROM_PATH:-}"
eeprom_path="${XEMU_EEPROM_PATH:-}"
hdd_path="${XEMU_HDD_PATH:-}"
xemu_config_path="$project_root/.local/xemu/runtime/xemu.toml"

if [[ -z "$eeprom_path" ]]; then
    eeprom_path="$project_root/.local/xemu/eeprom.bin"
fi

if is_windows; then
    xemu_exe="$(to_native_path "$xemu_exe")"
    games_dir="$(to_native_path "$games_dir")"
    iso_path="$(to_native_path "$iso_path")"
    [[ -n "$bootrom_path" ]] && bootrom_path="$(to_native_path "$bootrom_path")"
    [[ -n "$flashrom_path" ]] && flashrom_path="$(to_native_path "$flashrom_path")"
    [[ -n "$eeprom_path" ]] && eeprom_path="$(to_native_path "$eeprom_path")"
    [[ -n "$hdd_path" ]] && hdd_path="$(to_native_path "$hdd_path")"
    xemu_config_path="$(to_native_path "$xemu_config_path")"
fi

require_file 'xemu executable' "$xemu_exe"
require_file 'xemu boot ROM' "$bootrom_path"
require_file 'xemu flash ROM' "$flashrom_path"
require_file 'xemu hard disk image' "$hdd_path"

write_xemu_config "$xemu_config_path" "$games_dir" "$bootrom_path" "$flashrom_path" "$eeprom_path" "$hdd_path"

if [[ "$check_only" -eq 1 ]]; then
    printf 'XEMU_EXE=%s\n' "$xemu_exe"
    printf 'XEMU_CONFIG_PATH=%s\n' "$xemu_config_path"
    printf 'ISO_PATH=%s\n' "$iso_path"
    printf 'XEMU_BOOTROM_PATH=%s\n' "$bootrom_path"
    printf 'XEMU_FLASHROM_PATH=%s\n' "$flashrom_path"
    printf 'XEMU_EEPROM_PATH=%s\n' "$eeprom_path"
    printf 'XEMU_HDD_PATH=%s\n' "$hdd_path"
    exit 0
fi

exec "$xemu_exe" -config_path "$xemu_config_path" -dvd_path "$iso_path" -no-user-config
