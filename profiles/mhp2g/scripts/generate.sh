#!/usr/bin/env bash
# Generate MHP2G AOT code locally from the user's decrypted executable.
#
# Usage: generate.sh [build_dir] [decrypted_elf]
#
# Game-derived analysis and generated sources must never be committed.
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
build_dir="${1:-$repo_dir/out/mhp2g-renderer}"
elf="${2:-$repo_dir/mhp2g-test-game/EBOOT.ELF}"

if [[ ! -f "$elf" ]]; then
    echo "error: missing decrypted MHP2G executable: $elf" >&2
    exit 1
fi

/usr/bin/cmake --build "$build_dir" --target psp_analyze psp_recomp

mkdir -p "$profile_dir/analysis"

"$build_dir/psp_analyze" \
    "$elf" \
    "$profile_dir/analysis/report.json"

"$build_dir/psp_recomp" \
    "$elf" \
    --auto "$profile_dir/generated"

echo "MHP2G AOT generation complete."
