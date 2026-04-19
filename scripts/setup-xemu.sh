#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: setup-xemu.sh [--force] [--skip-support-files]

Downloads a portable xemu build into .local/xemu and refreshes launcher manifests.
Existing .local/xemu files are preserved unless --force is passed.
EOF
    return 0
}

is_windows() {
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) return 0 ;;
        *) return 1 ;;
    esac
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

download_file() {
    local uri="$1"
    local destination="$2"
    local force_download="$3"

    if [[ "$force_download" -eq 0 && -f "$destination" ]]; then
        return 0
    fi

    curl -fsSL -A 'Moonlight-XboxOG setup-xemu' -o "$destination" "$uri"
}

extract_archive() {
    local archive="$1"
    local destination="$2"

    rm -rf "$destination"
    mkdir -p "$destination"

    if command -v unzip >/dev/null 2>&1; then
        unzip -oq "$archive" -d "$destination"
        return 0
    fi

    if command -v bsdtar >/dev/null 2>&1; then
        bsdtar -xf "$archive" -C "$destination"
        return 0
    fi

    if command -v tar >/dev/null 2>&1; then
        tar -xf "$archive" -C "$destination"
        return 0
    fi

    echo 'Could not extract archive: no unzip, bsdtar, or tar found.' >&2
    return 1
}

copy_tree_preserving_existing() {
    local source_root="$1"
    local destination_root="$2"
    local source_path relative_path destination_path

    mkdir -p "$destination_root"

    while IFS= read -r -d '' source_path; do
        relative_path="${source_path#"$source_root"/}"
        destination_path="$destination_root/$relative_path"

        if [[ -d "$source_path" ]]; then
            mkdir -p "$destination_path"
            continue
        fi

        if [[ -e "$destination_path" ]]; then
            continue
        fi

        mkdir -p "$(dirname "$destination_path")"
        cp -p "$source_path" "$destination_path"
    done < <(find "$source_root" -mindepth 1 -print0)

    return 0
}

latest_xemu_tag() {
    curl -fsSL -A 'Moonlight-XboxOG setup-xemu' \
        'https://api.github.com/repos/xemu-project/xemu/releases/latest' |
        sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' |
        head -n 1

    return 0
}

select_xemu_asset() {
    local os_name="$1"
    local arch_name="$2"
    local tag="$3"

    case "$os_name:$arch_name" in
        windows:x86_64|windows:amd64)
            printf 'xemu-win-x86_64-release.zip\n'
            ;;
        windows:arm64|windows:aarch64)
            printf 'xemu-win-aarch64-release.zip\n'
            ;;
        macos:*)
            printf 'xemu-%s-macos-universal.zip\n' "$tag"
            ;;
        linux:x86_64|linux:amd64)
            printf 'xemu-%s-x86_64.AppImage\n' "$tag"
            ;;
        linux:arm64|linux:aarch64)
            printf 'xemu-%s-aarch64.AppImage\n' "$tag"
            ;;
        *)
            return 1
            ;;
    esac
}

detect_platform() {
    local uname_s uname_m os_name arch_name
    uname_s="$(uname -s)"
    uname_m="$(uname -m)"

    case "$uname_s" in
        MINGW*|MSYS*|CYGWIN*) os_name='windows' ;;
        Darwin) os_name='macos' ;;
        Linux) os_name='linux' ;;
        *)
            echo "Unsupported host OS: $uname_s" >&2
            return 1
            ;;
    esac

    case "$uname_m" in
        x86_64|amd64) arch_name='x86_64' ;;
        arm64|aarch64) arch_name='arm64' ;;
        *)
            echo "Unsupported host architecture: $uname_m" >&2
            return 1
            ;;
    esac

    printf '%s\n%s\n' "$os_name" "$arch_name"
}

find_first_file() {
    local root="$1"
    shift
    local patterns=("$@")
    local pattern

    for pattern in "${patterns[@]}"; do
        while IFS= read -r match; do
            printf '%s\n' "$match"
            return 0
        done < <(find "$root" -type f \( -iname "$pattern" \) 2>/dev/null | sort)
    done

    return 1
}

find_existing_xemu_executable() {
    local root="$1"

    find_first_file "$root" 'xemu.exe' 'xemu' 'xemu.AppImage'
    return $?
}

support_assets_ready() {
    local root="$1"

    [[ -n "$(find_first_file "$root" 'mcpx*.bin' 2>/dev/null || true)" ]] || return 1
    [[ -n "$(find_first_file "$root" 'Complex*.bin' '*4627*.bin' 2>/dev/null || true)" ]] || return 1
    [[ -n "$(find_first_file "$root" 'xbox_hdd.qcow2' '*.qcow2' 2>/dev/null || true)" ]] || return 1
    return 0
}

write_shell_manifest() {
    local manifest_path="$1"
    shift
    local manifest_entries=("$@")

    : > "$manifest_path"
    printf '#!/usr/bin/env bash\n' >> "$manifest_path"
    while [[ ${#manifest_entries[@]} -gt 1 ]]; do
        printf 'export %s=%q\n' "${manifest_entries[0]}" "${manifest_entries[1]}" >> "$manifest_path"
        manifest_entries=("${manifest_entries[@]:2}")
    done

    return 0
}

write_cmd_manifest() {
    local manifest_path="$1"
    shift
    local manifest_entries=("$@")

    : > "$manifest_path"
    printf '@echo off\n' >> "$manifest_path"
    while [[ ${#manifest_entries[@]} -gt 1 ]]; do
        printf 'set "%s=%s"\n' "${manifest_entries[0]}" "${manifest_entries[1]}" >> "$manifest_path"
        manifest_entries=("${manifest_entries[@]:2}")
    done

    return 0
}

force_download=0
skip_support_files=0
reused_existing_xemu=0
reused_existing_support=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --force)
            force_download=1
            ;;
        --skip-support-files)
            skip_support_files=1
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

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
local_root="$project_root/.local"
xemu_root="$local_root/xemu"
downloads_dir="$xemu_root/downloads"
app_dir="$xemu_root/app"
support_dir="$xemu_root/support"
temp_dir="$xemu_root/tmp"
portable_root="$xemu_root/portable"
manifest_sh="$xemu_root/paths.sh"
manifest_cmd="$xemu_root/paths.cmd"

mkdir -p "$downloads_dir" "$app_dir" "$support_dir" "$temp_dir" "$portable_root"

xemu_exe="$(find_existing_xemu_executable "$app_dir" || true)"
if [[ -n "$xemu_exe" && "$force_download" -eq 0 ]]; then
    reused_existing_xemu=1
else
    mapfile -t platform_info < <(detect_platform)
    os_name="${platform_info[0]}"
    arch_name="${platform_info[1]}"
    tag="$(latest_xemu_tag)"
    if [[ -z "$tag" ]]; then
        echo 'Could not determine the latest xemu release tag.' >&2
        exit 1
    fi

    asset_name="$(select_xemu_asset "$os_name" "$arch_name" "$tag")"
    asset_url="https://github.com/xemu-project/xemu/releases/latest/download/${asset_name}"
    asset_path="$downloads_dir/$asset_name"

    download_file "$asset_url" "$asset_path" "$force_download"

    case "$asset_name" in
        *.zip)
            extract_archive "$asset_path" "$temp_dir/xemu-extract"
            if [[ -z "$(find_existing_xemu_executable "$temp_dir/xemu-extract" || true)" ]]; then
                echo 'Could not find xemu executable in the downloaded archive.' >&2
                exit 1
            fi

            if [[ "$force_download" -eq 1 ]]; then
                rm -rf "$app_dir"
                mkdir -p "$app_dir"
                cp -R "$temp_dir/xemu-extract"/. "$app_dir"/
            else
                copy_tree_preserving_existing "$temp_dir/xemu-extract" "$app_dir"
            fi
            xemu_exe="$(find_existing_xemu_executable "$app_dir" || true)"
            ;;
        *.AppImage)
            if [[ "$force_download" -eq 1 || ! -f "$app_dir/xemu.AppImage" ]]; then
                cp "$asset_path" "$app_dir/xemu.AppImage"
            fi
            chmod +x "$app_dir/xemu.AppImage"
            xemu_exe="$app_dir/xemu.AppImage"
            ;;
        *)
            echo "Unsupported xemu asset type: $asset_name" >&2
            exit 1
            ;;
    esac

    if [[ -z "$xemu_exe" ]]; then
        echo 'Could not locate the xemu executable after installation.' >&2
        exit 1
    fi
fi

support_zip="$downloads_dir/Xbox-Emulator-Files.zip"
if [[ "$skip_support_files" -eq 0 ]]; then
    if [[ "$force_download" -eq 0 ]] && support_assets_ready "$support_dir"; then
        reused_existing_support=1
    else
        download_file \
            'https://github.com/K3V1991/Xbox-Emulator-Files/releases/download/v1/Xbox-Emulator-Files.zip' \
            "$support_zip" \
            "$force_download"
        extract_archive "$support_zip" "$temp_dir/support-extract"
        if [[ "$force_download" -eq 1 ]]; then
            rm -rf "$support_dir"
            mkdir -p "$support_dir"
            cp -R "$temp_dir/support-extract"/. "$support_dir"/
        else
            copy_tree_preserving_existing "$temp_dir/support-extract" "$support_dir"
        fi
    fi
fi

bootrom_path=''
flashrom_path=''
eeprom_path=''
hdd_path=''
default_eeprom_path="$xemu_root/eeprom.bin"

if [[ -d "$support_dir" ]]; then
    bootrom_path="$(find_first_file "$support_dir" 'mcpx*.bin' || true)"
    flashrom_path="$(find_first_file "$support_dir" 'Complex*.bin' '*4627*.bin' || true)"
    eeprom_path="$(find_first_file "$support_dir" 'EEPROM.bin' 'eeprom.bin' || true)"
    hdd_path="$(find_first_file "$support_dir" 'xbox_hdd.qcow2' '*.qcow2' || true)"
fi

if [[ -z "$eeprom_path" ]]; then
    eeprom_path="$default_eeprom_path"
fi

xemu_root_native="$(to_native_path "$xemu_root")"
xemu_exe_native="$(to_native_path "$xemu_exe")"
support_dir_native="$(to_native_path "$support_dir")"
bootrom_native="${bootrom_path:+$(to_native_path "$bootrom_path")}"
flashrom_native="${flashrom_path:+$(to_native_path "$flashrom_path")}"
eeprom_native="${eeprom_path:+$(to_native_path "$eeprom_path")}"
hdd_native="${hdd_path:+$(to_native_path "$hdd_path")}"

if is_windows; then
    xemu_appdata="$portable_root/AppData/Roaming"
    xemu_localappdata="$portable_root/AppData/Local"
    mkdir -p "$xemu_appdata" "$xemu_localappdata"

    write_shell_manifest "$manifest_sh" \
        XEMU_ROOT "$xemu_root" \
        XEMU_EXE "$xemu_exe" \
        XEMU_SUPPORT_DIR "$support_dir" \
        XEMU_BOOTROM_PATH "$bootrom_path" \
        XEMU_FLASHROM_PATH "$flashrom_path" \
        XEMU_EEPROM_PATH "$eeprom_path" \
        XEMU_HDD_PATH "$hdd_path" \
        XEMU_APPDATA "$xemu_appdata" \
        XEMU_LOCALAPPDATA "$xemu_localappdata"

    write_cmd_manifest "$manifest_cmd" \
        XEMU_ROOT "$xemu_root_native" \
        XEMU_EXE "$xemu_exe_native" \
        XEMU_SUPPORT_DIR "$support_dir_native" \
        XEMU_BOOTROM_PATH "$bootrom_native" \
        XEMU_FLASHROM_PATH "$flashrom_native" \
        XEMU_EEPROM_PATH "$eeprom_native" \
        XEMU_HDD_PATH "$hdd_native" \
        XEMU_APPDATA "$(to_native_path "$xemu_appdata")" \
        XEMU_LOCALAPPDATA "$(to_native_path "$xemu_localappdata")"
else
    xemu_home="$portable_root/home"
    xemu_config_home="$portable_root/config"
    xemu_data_home="$portable_root/data"
    xemu_cache_home="$portable_root/cache"
    xemu_state_home="$portable_root/state"
    mkdir -p "$xemu_home" "$xemu_config_home" "$xemu_data_home" "$xemu_cache_home" "$xemu_state_home"

    write_shell_manifest "$manifest_sh" \
        XEMU_ROOT "$xemu_root" \
        XEMU_EXE "$xemu_exe" \
        XEMU_SUPPORT_DIR "$support_dir" \
        XEMU_BOOTROM_PATH "$bootrom_path" \
        XEMU_FLASHROM_PATH "$flashrom_path" \
        XEMU_EEPROM_PATH "$eeprom_path" \
        XEMU_HDD_PATH "$hdd_path" \
        XEMU_HOME "$xemu_home" \
        XEMU_CONFIG_HOME "$xemu_config_home" \
        XEMU_DATA_HOME "$xemu_data_home" \
        XEMU_CACHE_HOME "$xemu_cache_home" \
        XEMU_STATE_HOME "$xemu_state_home"

    write_cmd_manifest "$manifest_cmd" \
        XEMU_ROOT "$xemu_root_native" \
        XEMU_EXE "$xemu_exe_native" \
        XEMU_SUPPORT_DIR "$support_dir_native" \
        XEMU_BOOTROM_PATH "$bootrom_native" \
        XEMU_FLASHROM_PATH "$flashrom_native" \
        XEMU_EEPROM_PATH "$eeprom_native" \
        XEMU_HDD_PATH "$hdd_native"
fi

chmod +x "$manifest_sh"

printf 'Portable xemu files are ready in %s\n' "$xemu_root"
if [[ "$reused_existing_xemu" -eq 1 ]]; then
    echo 'Reused the existing xemu application files in .local/xemu/app.'
fi
if [[ "$reused_existing_support" -eq 1 ]]; then
    echo 'Reused the existing xemu support files in .local/xemu/support.'
fi
if [[ "$skip_support_files" -eq 1 ]]; then
    echo 'Support files were skipped. Run setup-xemu.sh again without --skip-support-files to fetch them.'
elif [[ -z "$bootrom_path" || -z "$flashrom_path" || -z "$hdd_path" ]]; then
    echo 'Support files were downloaded, but some expected xemu ROM/HDD assets were not detected automatically.'
else
    echo 'Detected xemu ROM and HDD assets and refreshed the local launcher manifests.'
fi
