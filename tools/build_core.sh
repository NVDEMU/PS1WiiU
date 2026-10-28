#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CORE="$ROOT/vendor/pcsx_rearmed"
POINTER_PATCH="$ROOT/patches/pcsx-rearmed-wiiu-pointer-casts.patch"
SYSCONF_PATCH="$ROOT/patches/pcsx-rearmed-wiiu-sysconf.patch"

if [ ! -d "$CORE/.git" ] && [ ! -f "$CORE/.git" ]; then
    echo "PCSX-ReARMed submodule is missing. Clone with --recurse-submodules." >&2
    exit 1
fi

apply_patch_idempotently() {
    patch_file="$1"
    if git -C "$CORE" apply --check "$patch_file"; then
        git -C "$CORE" apply "$patch_file"
    elif git -C "$CORE" apply --reverse --check "$patch_file"; then
        echo "Already applied: $(basename "$patch_file")"
    else
        echo "Could not apply compatibility patch: $patch_file" >&2
        exit 1
    fi
}

apply_patch_idempotently "$POINTER_PATCH"
apply_patch_idempotently "$SYSCONF_PATCH"

make -C "$CORE" -f Makefile.libretro platform=wiiu
