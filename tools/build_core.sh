#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CORE="$ROOT/vendor/pcsx_rearmed"
PATCH="$ROOT/patches/pcsx-rearmed-wiiu-pointer-casts.patch"

if [ ! -d "$CORE/.git" ] && [ ! -f "$CORE/.git" ]; then
    echo "PCSX-ReARMed submodule is missing. Clone with --recurse-submodules." >&2
    exit 1
fi

if git -C "$CORE" apply --check "$PATCH"; then
    git -C "$CORE" apply "$PATCH"
elif git -C "$CORE" apply --reverse --check "$PATCH"; then
    echo "Wii U Lightrec patch is already applied."
else
    echo "Could not apply the pinned PCSX-ReARMed compatibility patch." >&2
    exit 1
fi

make -C "$CORE" -f Makefile.libretro platform=wiiu
