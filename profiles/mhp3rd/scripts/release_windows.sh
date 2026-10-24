#!/usr/bin/env bash
# Package a Windows release (x86-64) from a finished build directory: the
# normal zip, which keeps its data in %APPDATA%\Yakumo\MHP3rd, and the
# portable zip, which keeps it in data\ next to Yakumo.exe.
#
#   release_windows.sh [--version VERSION] [--no-portable] [--no-installed]
#                      BUILD_DIR
#
# BUILD_DIR is a build of this checkout configured with -DMHP3RD_RELEASE=ON
# whose Yakumo.exe and overlay DLLs are built (bin/Yakumo.exe, bin/overlays,
# and the bundled FFmpeg's DLLs in bin). Nothing in it is rebuilt or changed.
#
# Run it in Git Bash started from an "x64 Native Tools Command Prompt" of
# Visual Studio or its Build Tools, like the build (docs/BUILDING.md): it needs
# dumpbin, and the Visual C++ runtime DLLs it ships come from that
# toolchain's redistributable folder ($VCToolsRedistDir). SDL3.dll is the one
# the build found (SDL3_DIR in the build's CMake cache).
#
# The script stages Yakumo.exe, the overlay DLLs, SDL3, FFmpeg, the Visual C++
# runtime, the licenses, README.txt and BUILDINFO.txt; checks that every DLL
# the executables import is in the package or in System32; packs the zips
# (the portable one adds portable.txt, which switches portable mode on);
# checks that no artifact contains game data or a path of this machine; and
# prints their SHA-256 checksums. Everything lands in out/release-windows,
# the artifacts in out/release-windows/dist.
#
#   --version VERSION  name the artifacts after VERSION instead of git describe
#   --no-portable      do not pack the portable zip
#   --no-installed     do not pack the normal zip
set -euo pipefail
export LC_ALL=C

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
work="${YAKUMO_WORK:-$repo_dir/out/release-windows}"
stage="$work/stage"
dist="$work/dist"

version=""
make_portable=1
make_installed=1
build_dir=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) version="${2:?--version needs a value}"; shift 2 ;;
        --no-portable) make_portable=0; shift ;;
        --no-installed) make_installed=0; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) [[ -z "$build_dir" ]] || { echo "only one build directory" >&2; exit 2; }; build_dir="$1"; shift ;;
    esac
done
[[ -n "$build_dir" ]] || { echo "usage: $0 [options] BUILD_DIR (see --help)" >&2; exit 2; }
(( make_portable || make_installed )) || { echo "nothing to pack" >&2; exit 2; }
build_dir="$(cd "$build_dir" && pwd)"
if [[ -z "$version" ]]; then
    version="$(git -C "$repo_dir" describe --tags --always --dirty)"
    version="${version#v}"
fi
name="yakumo-$version-windows-x86_64"

step() { printf '\n=== %s\n' "$*"; }
fail() { echo "error: $*" >&2; exit 1; }
sha256() { sha256sum "$1" | cut -d' ' -f1; }
cache_value() { sed -n "s/^$1:[A-Z]*=//p" "$build_dir/CMakeCache.txt" | head -1; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) fail "run this in Git Bash on Windows" ;; esac
command -v dumpbin > /dev/null || fail "dumpbin not found: start Git Bash from an x64 Native Tools Command Prompt"
[[ -n "${VCToolsRedistDir:-}" ]] || fail "VCToolsRedistDir is not set: start Git Bash from an x64 Native Tools Command Prompt"
windows_tar="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32/tar.exe"
[[ -x "$windows_tar" ]] || fail "$windows_tar not found (it packs the zip archives)"

step "Checking the build in $build_dir"
[[ -f "$build_dir/CMakeCache.txt" ]] || fail "$build_dir is not a CMake build directory"
[[ "$(cache_value MHP3RD_RELEASE)" == ON ]] || fail "$build_dir is not configured with -DMHP3RD_RELEASE=ON"
bin="$build_dir/bin"
[[ -f "$bin/Yakumo.exe" ]] || fail "$bin/Yakumo.exe is missing: build the Yakumo target"
expected_overlays=$(find "$profile_dir/overlays" -mindepth 2 -maxdepth 2 -name meta.txt 2> /dev/null | wc -l)
built_overlays=$(find "$bin/overlays" -maxdepth 1 -name 'ovl*.dll' 2> /dev/null | wc -l)
(( expected_overlays > 0 )) || fail "no overlay corpora in $profile_dir/overlays"
(( built_overlays == expected_overlays )) ||
    fail "$built_overlays overlay DLLs in $bin/overlays, but $expected_overlays corpora: build them all first"
[[ ! "$bin/Yakumo.exe" -ot "$(ls -t "$bin"/overlays/ovl*.dll | tail -1)" ]] ||
    echo "note: some overlay DLLs are newer than Yakumo.exe; that is fine as long as both come from this checkout"
ffmpeg_dlls=(avcodec-61.dll avutil-59.dll swresample-5.dll)
for dll in "${ffmpeg_dlls[@]}" FFmpeg-LICENSE.txt FFmpeg-SOURCE.txt; do
    [[ -f "$bin/$dll" ]] || fail "$bin/$dll is missing: configure with the bundled FFmpeg (the default)"
done
sdl3_dir="$(cache_value SDL3_DIR)"
[[ -n "$sdl3_dir" ]] || fail "the build did not find SDL3 (SDL3_DIR is empty in its CMake cache)"
sdl3_root="$(cd "$(cygpath -u "$sdl3_dir")/.." && pwd)"
sdl3_dll="$sdl3_root/lib/x64/SDL3.dll"
[[ -f "$sdl3_dll" ]] || fail "$sdl3_dll not found (expected the official SDL3-devel-*-VC layout)"
sdl3_version="$(sed -n 's/^#define SDL_MAJOR_VERSION *//p; s/^#define SDL_MINOR_VERSION *//p; s/^#define SDL_MICRO_VERSION *//p' \
    "$sdl3_root/include/SDL3/SDL_version.h" | paste -sd. -)"
crt="$(cygpath -u "$VCToolsRedistDir")/x64/Microsoft.VC143.CRT"
[[ -d "$crt" ]] || fail "$crt not found"
crt_dlls=(msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
compiler="$(cache_value CMAKE_CXX_COMPILER)"
compiler_version="$(cl 2>&1 | sed -n 's/.*Version \([0-9.]*\).*/\1/p' | head -1)"

step "Staging $name"
rm -rf "$stage"
mkdir -p "$stage/$name/overlays" "$stage/$name/licenses" "$dist"
root="$stage/$name"
cp "$bin/Yakumo.exe" "$root/"
cp "$bin"/overlays/ovl*.dll "$root/overlays/"
cp "$sdl3_dll" "$root/"
for dll in "${ffmpeg_dlls[@]}"; do cp "$bin/$dll" "$root/"; done
for dll in "${crt_dlls[@]}"; do cp "$crt/$dll" "$root/"; done
licenses="$root/licenses"
cp "$repo_dir/LICENSE" "$licenses/Yakumo-LICENSE.txt"
cp "$profile_dir/packaging/THIRD_PARTY_NOTICES.md" "$licenses/THIRD_PARTY_NOTICES.md"
cp "$sdl3_root/LICENSE.txt" "$licenses/SDL3-LICENSE.txt"
cp "$bin/FFmpeg-LICENSE.txt" "$bin/FFmpeg-SOURCE.txt" "$licenses/"
cp "$profile_dir/third_party/imgui/LICENSE.txt" "$licenses/DearImGui-LICENSE.txt"
cp "$profile_dir/third_party/tiny_aes/UNLICENSE" "$licenses/tiny-AES-c-UNLICENSE.txt"
cp "$profile_dir/third_party/xxhash/LICENSE" "$licenses/xxHash-LICENSE.txt"

cat > "$root/BUILDINFO.txt" << EOF
Version: v$version
Commit: $(git -C "$repo_dir" rev-parse HEAD)
Built: $(date -u '+%Y-%m-%d %H:%M:%S UTC')
Platform: Windows x86-64
Configuration: Release; MHP3RD_RELEASE=ON
Compiler: MSVC ${compiler_version:-$(basename "$compiler")}
SDL3: $sdl3_version
FFmpeg: bundled LGPL shared build ($(sed -n 's/^Build: *//p' "$bin/FFmpeg-SOURCE.txt" | sed 's#.*/##'))
Overlay DLLs: $built_overlays
EOF

step "Checking the DLLs every binary imports"
# Every import must be in the package or come with Windows. The Vulkan loader
# (vulkan-1.dll) comes with the graphics driver.
declare -A packaged=()
for file in "$root"/*.dll; do packaged["$(lower "$(basename "$file")")"]=1; done
packaged[yakumo.exe]=1
system32="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32"
missing=0
while IFS= read -r -d '' binary; do
    while IFS= read -r import; do
        key="$(lower "$import")"
        [[ -n "${packaged[$key]:-}" ]] && continue
        [[ "$key" == api-ms-win-* || "$key" == ext-ms-* ]] && continue
        [[ -f "$system32/$import" ]] && continue
        echo "  $(basename "$binary") imports $import, which is neither packaged nor in System32" >&2
        missing=1
    done < <(dumpbin -nologo -dependents "$(cygpath -w "$binary")" | tr -d '\r' |
                 sed -n 's/^ *\([A-Za-z0-9_.+-]*\.[Dd][Ll][Ll]\)$/\1/p')
done < <(find "$root" -type f \( -name '*.exe' -o -name '*.dll' \) -print0)
(( missing == 0 )) || fail "unresolved imports"

# The package must hold no game data and nothing that names this machine.
check_tree() {
    local tree="$1" bad=0 file base magic
    while IFS= read -r -d '' file; do
        base="$(lower "$(basename "$file")")"
        case "$base" in
            eboot*|*.iso|*.cso|*.bin|*.prx|*.pbp|*.elf|data.bin|param.sfo|*.ovl|*.sfo)
                echo "  game data by name: ${file#"$tree"/}" >&2; bad=1; continue ;;
        esac
        [[ "$file" == */ms0/* || "$file" == */SAVEDATA/* ]] && { echo "  save data: ${file#"$tree"/}" >&2; bad=1; }
        magic="$(head -c 4 "$file" | od -An -tx1 | tr -d ' \n')"
        case "$magic" in
            7f454c46) echo "  an ELF file: ${file#"$tree"/}" >&2; bad=1 ;;
            00504250|7e505350|00505346) echo "  a PSP file: ${file#"$tree"/}" >&2; bad=1 ;;
        esac
        [[ "$(dd if="$file" bs=2048 skip=16 count=1 2> /dev/null | head -c 6 | tail -c 5 | tr -d '\0')" == CD001 ]] &&
            { echo "  an ISO 9660 image: ${file#"$tree"/}" >&2; bad=1; }
    done < <(find "$tree" -type f -print0)
    local here
    for here in "$(cygpath -w "$repo_dir")" "$repo_dir" "${USERPROFILE:-}" "${USERNAME:+\\$USERNAME\\}" \
                "${COMPUTERNAME:-}"; do
        [[ -n "$here" ]] || continue
        if grep -r -l -a -F -i -- "$here" "$tree" > /dev/null 2>&1; then
            echo "  a path or name of this machine ($here) in: $(grep -r -l -a -F -i -- "$here" "$tree" | head -3 | tr '\n' ' ')" >&2
            bad=1
        fi
    done
    return $bad
}

write_readme() {
    local file="$1" portable="$2"
    {
        echo "Yakumo v$version for Windows x86-64"
        echo
        echo "1. Extract the entire folder."
        echo "2. Run Yakumo.exe."
        echo "3. On first start, choose the disc image from your own legally obtained copy of"
        echo "   Monster Hunter Portable 3rd HD Ver."
        echo
        if [[ "$portable" == 1 ]]; then
            echo "This is the portable version: because of portable.txt, Yakumo keeps its"
            echo "settings, saves and game data in the data folder next to Yakumo.exe, and"
            echo "nothing in %APPDATA%. Keep the folder somewhere you can write to (not in"
            echo "Program Files); a USB drive works. Saves are under data\\ms0\\PSP\\SAVEDATA."
            echo "If Yakumo is already set up on this computer, the first start offers to copy"
            echo "that data here; the original stays where it is."
            echo "Delete portable.txt and move the data folder away to use %APPDATA% instead."
        else
            echo "Yakumo stores settings and game data in %APPDATA%\\Yakumo\\MHP3rd."
            echo "Saves are under ms0\\PSP\\SAVEDATA in that directory."
            echo "For a portable copy that keeps everything next to Yakumo.exe, use the"
            echo "-portable zip, or create an empty file named portable.txt here."
        fi
        echo "Press Esc, or L3+R3 on a gamepad, to open the Yakumo menu."
        echo
        echo "A Vulkan-capable graphics driver is required. SDL3, FFmpeg and the Microsoft"
        echo "Visual C++ runtime DLLs are included."
        echo "This archive contains no game assets, disc image, executable from the game or saves."
    } | sed 's/$/\r/' > "$file"
}

pack() {
    local portable="$1" folder="$2" archive="$3"
    step "Packing $archive"
    local tree="$stage/pack"
    rm -rf "$tree"
    mkdir -p "$tree"
    cp -R "$root" "$tree/$folder"
    write_readme "$tree/$folder/README.txt" "$portable"
    if [[ "$portable" == 1 ]]; then
        printf '%s\r\n' "This file makes Yakumo portable: it keeps its settings, saves and game data" \
            "in the data folder next to Yakumo.exe. Delete it to use %APPDATA%\\Yakumo\\MHP3rd instead." \
            > "$tree/$folder/portable.txt"
    fi
    check_tree "$tree" || fail "$archive would contain game data or a path of this machine"
    rm -f "$dist/$archive"
    (cd "$tree" && "$windows_tar" -a -c -f "$(cygpath -w "$dist/$archive")" "$folder")
    # Unpack it again and check what a player gets.
    local check="$stage/check"
    rm -rf "$check"
    mkdir -p "$check"
    (cd "$check" && "$windows_tar" -x -f "$(cygpath -w "$dist/$archive")")
    check_tree "$check" || fail "$archive contains game data or a path of this machine"
    [[ -f "$check/$folder/Yakumo.exe" ]] || fail "$archive has no $folder/Yakumo.exe"
    if [[ "$portable" == 1 ]]; then
        [[ -f "$check/$folder/portable.txt" ]] || fail "$archive has no portable.txt"
    else
        [[ ! -e "$check/$folder/portable.txt" && ! -e "$check/$folder/data" ]] || fail "$archive would be portable"
    fi
    rm -rf "$check" "$tree"
}

artifacts=()
if (( make_installed )); then
    pack 0 "$name" "$name.zip"
    artifacts+=("$name.zip")
fi
if (( make_portable )); then
    pack 1 "$name-portable" "$name-portable.zip"
    artifacts+=("$name-portable.zip")
fi
cp "$root/BUILDINFO.txt" "$dist/BUILDINFO-windows.txt"

step "Artifacts in $dist"
for artifact in "${artifacts[@]}"; do
    printf '%s  %s\n' "$(sha256 "$dist/$artifact")" "$artifact"
done
