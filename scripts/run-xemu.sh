#!/usr/bin/env bash

# Launch xemu against an explicit ISO path or a CMake build directory that
# contains the generated Xbox ISO.

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: run-xemu.sh [--check] [--build-dir dir] [--iso path] [--network mode] [--tap-ifname name] [path]

Environment overrides:
  MOONLIGHT_XEMU_BUILD_DIR
  MOONLIGHT_XEMU_ISO_PATH
  MOONLIGHT_XEMU_TARGET_PATH
  MOONLIGHT_XEMU_NETWORK
  MOONLIGHT_XEMU_TAP_IFNAME
EOF
    return 0
}

is_windows() {
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) return 0 ;;
        *) return 1 ;;
    esac
}

is_unresolved_ide_macro() {
    local value="$1"

    if [[ "$value" =~ ^\$[A-Za-z_][A-Za-z0-9_]*\$$ ]]; then
        return 0
    fi

    return 1
}

to_native_path() {
    local path="$1"

    if is_windows; then
        cygpath -w "$path"
    else
        printf '%s\n' "$path"
    fi

    return 0
}

escape_toml_string() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    printf '%s' "$value"
    return 0
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
        printf '\n[net]\n'
        printf 'enable = true\n'
        printf '\n[sys.files]\n'
        printf 'bootrom_path = "%s"\n' "$(escape_toml_string "$bootrom_path")"
        printf 'flashrom_path = "%s"\n' "$(escape_toml_string "$flashrom_path")"
        printf 'eeprom_path = "%s"\n' "$(escape_toml_string "$eeprom_path")"
        printf 'hdd_path = "%s"\n' "$(escape_toml_string "$hdd_path")"
    } > "$config_path"

    return 0
}

prepare_xemu_runtime_environment() {
    if is_windows; then
        if [[ -n "${XEMU_APPDATA:-}" ]]; then
            export APPDATA="$(to_native_path "$XEMU_APPDATA")"
        fi
        if [[ -n "${XEMU_LOCALAPPDATA:-}" ]]; then
            export LOCALAPPDATA="$(to_native_path "$XEMU_LOCALAPPDATA")"
        fi
        return 0
    fi

    if [[ -n "${XEMU_HOME:-}" ]]; then
        export HOME="$XEMU_HOME"
    fi
    if [[ -n "${XEMU_CONFIG_HOME:-}" ]]; then
        export XDG_CONFIG_HOME="$XEMU_CONFIG_HOME"
    fi
    if [[ -n "${XEMU_DATA_HOME:-}" ]]; then
        export XDG_DATA_HOME="$XEMU_DATA_HOME"
    fi
    if [[ -n "${XEMU_CACHE_HOME:-}" ]]; then
        export XDG_CACHE_HOME="$XEMU_CACHE_HOME"
    fi
    if [[ -n "${XEMU_STATE_HOME:-}" ]]; then
        export XDG_STATE_HOME="$XEMU_STATE_HOME"
    fi

    return 0
}

build_network_args() {
    case "$network_mode" in
        user)
            return 0
            ;;
        none)
            xemu_network_args=(-nic none)
            return 0
            ;;
        tap)
            if [[ -z "$tap_ifname" ]]; then
                echo 'The tap network mode requires --tap-ifname or MOONLIGHT_XEMU_TAP_IFNAME.' >&2
                exit 2
            fi
            xemu_network_args=(-nic "tap,ifname=$tap_ifname")
            return 0
            ;;
        *)
            echo "Unsupported network mode: $network_mode" >&2
            exit 2
            ;;
    esac
}

require_file() {
    local label="$1"
    local path="$2"

    if [[ -z "$path" || ! -f "$path" ]]; then
        printf '%s not found: %s\n' "$label" "$path" >&2
        return 1
    fi
}

normalize_cli_path() {
    local path="$1"

    if [[ -z "$path" ]]; then
        printf '\n'
        return 0
    fi

    if is_unresolved_ide_macro "$path"; then
        printf '\n'
        return 0
    fi

    if is_windows && [[ "$path" =~ ^[A-Za-z]:\\ ]]; then
        cygpath -u "$path"
        return 0
    fi

    printf '%s\n' "${path//\\//}"
    return 0
}

default_iso_path() {
    local project_root="$1"
    local build_dir="${2:-}"

    if [[ -n "$build_dir" ]]; then
        local build_dir_iso="$build_dir/xbox/Moonlight.iso"
        if [[ -f "$build_dir/Moonlight.iso" ]]; then
            printf '%s\n' "$build_dir/Moonlight.iso"
        else
            printf '%s\n' "$build_dir_iso"
        fi
        return 0
    fi

    local preferred_path="$project_root/cmake-build-release/xbox/Moonlight.iso"
    local candidate
    local newest_candidate=""

    shopt -s nullglob
    for candidate in \
        "$project_root"/cmake-build-*/xbox/Moonlight.iso \
        "$project_root"/cmake-build-*/Moonlight.iso; do
        if [[ -z "$newest_candidate" || "$candidate" -nt "$newest_candidate" ]]; then
            newest_candidate="$candidate"
        fi
    done
    shopt -u nullglob

    if [[ -n "$newest_candidate" ]]; then
        printf '%s\n' "$newest_candidate"
        return 0
    fi

    printf '%s\n' "$preferred_path"
    return 0
}

resolve_build_dir() {
    local path="$1"

    path="$(normalize_cli_path "$path")"

    if [[ -z "$path" ]]; then
        printf '\n'
        return 0
    fi

    case "$path" in
        /*)
            printf '%s\n' "$path"
            ;;
        [A-Za-z]:/*)
            printf '%s\n' "$path"
            ;;
        *)
            printf '%s\n' "$project_root/$path"
            ;;
    esac

    return 0
}

resolve_input_path() {
    local input_path="$1"

    resolve_build_dir "$input_path"
    return 0
}

apply_target_path() {
    local target_path="$1"
    local resolved_path

    resolved_path="$(resolve_input_path "$target_path")"

    if [[ -z "$resolved_path" ]]; then
        return 0
    fi

    if [[ -d "$resolved_path" ]]; then
        build_dir="$resolved_path"
        iso_path="$(default_iso_path "$project_root" "$build_dir")"
    else
        iso_path="$resolved_path"
    fi

    return 0
}

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
setup_script="$project_root/scripts/setup-xemu.sh"
manifest_sh="$project_root/.local/xemu/paths.sh"
build_dir=""
iso_path="$(default_iso_path "$project_root")"
check_only=0
target_path=""
network_mode="${MOONLIGHT_XEMU_NETWORK:-user}"
tap_ifname="${MOONLIGHT_XEMU_TAP_IFNAME:-}"
xemu_network_args=()

if [[ -n "${MOONLIGHT_XEMU_BUILD_DIR:-}" ]]; then
    build_dir="$(resolve_build_dir "$MOONLIGHT_XEMU_BUILD_DIR")"
    iso_path="$(default_iso_path "$project_root" "$build_dir")"
fi

if [[ -n "${MOONLIGHT_XEMU_ISO_PATH:-}" ]]; then
    iso_path="$(resolve_input_path "$MOONLIGHT_XEMU_ISO_PATH")"
fi

if [[ -n "${MOONLIGHT_XEMU_TARGET_PATH:-}" ]]; then
    target_path="$MOONLIGHT_XEMU_TARGET_PATH"
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check)
            check_only=1
            ;;
        --build-dir)
            shift
            if [[ $# -eq 0 ]]; then
                echo 'Missing value for --build-dir' >&2
                exit 2
            fi
            build_dir="$(resolve_build_dir "$1")"
            iso_path="$(default_iso_path "$project_root" "$build_dir")"
            ;;
        --iso)
            shift
            if [[ $# -eq 0 ]]; then
                echo 'Missing value for --iso' >&2
                exit 2
            fi
            iso_path="$(resolve_input_path "$1")"
            ;;
        --network)
            shift
            if [[ $# -eq 0 ]]; then
                echo 'Missing value for --network' >&2
                exit 2
            fi
            network_mode="$1"
            ;;
        --tap-ifname)
            shift
            if [[ $# -eq 0 ]]; then
                echo 'Missing value for --tap-ifname' >&2
                exit 2
            fi
            tap_ifname="$1"
            ;;
        --)
            shift
            if [[ $# -gt 0 ]]; then
                target_path="$1"
                shift
            fi
            if [[ $# -gt 0 ]]; then
                echo 'Only one positional path is supported' >&2
                exit 2
            fi
            break
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            if [[ -z "$target_path" ]]; then
                target_path="$1"
            else
                echo 'Only one positional path is supported' >&2
                exit 2
            fi
            ;;
    esac
    shift
done

if [[ -n "$target_path" ]]; then
    apply_target_path "$target_path"
fi

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
prepare_xemu_runtime_environment
build_network_args

if [[ "$check_only" -eq 1 ]]; then
    if [[ -n "$build_dir" ]]; then
        printf 'BUILD_DIR=%s\n' "$build_dir"
    fi
    printf 'XEMU_EXE=%s\n' "$xemu_exe"
    printf 'XEMU_CONFIG_PATH=%s\n' "$xemu_config_path"
    printf 'ISO_PATH=%s\n' "$iso_path"
    printf 'XEMU_BOOTROM_PATH=%s\n' "$bootrom_path"
    printf 'XEMU_FLASHROM_PATH=%s\n' "$flashrom_path"
    printf 'XEMU_EEPROM_PATH=%s\n' "$eeprom_path"
    printf 'XEMU_HDD_PATH=%s\n' "$hdd_path"
    printf 'XEMU_NETWORK_MODE=%s\n' "$network_mode"
    if [[ -n "$tap_ifname" ]]; then
        printf 'XEMU_TAP_IFNAME=%s\n' "$tap_ifname"
    fi
    exit 0
fi

exec "$xemu_exe" -config_path "$xemu_config_path" -dvd_path "$iso_path" -no-user-config "${xemu_network_args[@]}"
