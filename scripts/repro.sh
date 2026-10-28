#!/bin/sh
# Builds the ROM and the driver package twice and requires byte-identical outputs:
#   run 1: this source tree, built in build/repro/a
#   run 2: a copy of the source tree at another path, built in a build directory
#          with a different, longer name
# Different source and build paths catch either path leaking into the outputs.
# The copy holds the files git tracks or would track (untracked files that are
# not ignored included), so uncommitted edits are tested too; not the 86Box
# submodule, which the ROM and the package do not use. Both builds get
# the same build stamp and time (release.sh's, or this tree's), since the
# copy has no git history.
#
# Run from an MSYS2 UCRT64 shell (cmake, ninja, gcc, nasm and git on PATH).
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
work="$root/build/repro"
copy="$work/source-copy/VGPU98 elsewhere"
files="rom/vgpu98.bin rom/manifest.txt driver/vgpu98.drv minivdd/vgpu98.vxd driver32/vgpu98dx.dll glide/glide2x.dll glide/glide3x.dll opengl/vgpu98gl.dll cpl/vgpu98cp.cpl package/vgpu98-driver/vgpu98.inf package/vgpu98-driver/vgpu98.drv package/vgpu98-driver/vgpu98.vxd package/vgpu98-driver/vgpu98dx.dll package/vgpu98-driver/glide2x.dll package/vgpu98-driver/glide3x.dll package/vgpu98-driver/vgpu98gl.dll package/vgpu98-driver/vgpu98cp.cpl package/vgpu98-driver.iso gen/stamp.txt gen/vid_vgpu98_card.h gen/shared.inc gen/card.inc gen/vgamodes.inc gen/palettes.inc gen/font8x8.inc gen/font8x14.inc gen/font8x16.inc gen/vbemodes.inc gen/drvmodes.inc"

if [ -z "${VGPU98_BUILD_STAMP:-}" ]; then
    VGPU98_BUILD_STAMP=$(git -C "$root" rev-parse --short=7 HEAD)
    if ! git -C "$root" diff --quiet --ignore-submodules HEAD --; then
        VGPU98_BUILD_STAMP="$VGPU98_BUILD_STAMP-dirty"
    fi
fi
if [ -z "${VGPU98_BUILD_TIME:-}" ]; then
    VGPU98_BUILD_TIME=$(git -C "$root" log -1 --format=%ct)
fi

rm -rf "$work"
mkdir -p "$copy"
(cd "$root" && git ls-files --cached --others --exclude-standard -z -- . ':(exclude)86Box' | xargs -0 tar -cf - --) | (cd "$copy" && tar -xf -)

build() {
    echo "repro: building $1 in $2"
    cmake -S "$1" -B "$2" -G Ninja -DCMAKE_BUILD_TYPE=Release -DVGPU98_BUILD_STAMP="$VGPU98_BUILD_STAMP" \
        -DVGPU98_BUILD_TIME="$VGPU98_BUILD_TIME" >/dev/null
    cmake --build "$2" --target rom --target driver --target minivdd --target driver_package >/dev/null
}

first="$work/a"
second="$work/second-build-with-a-longer-name"
build "$root" "$first"
build "$copy" "$second"

status=0
for f in $files; do
    if cmake -E compare_files "$first/$f" "$second/$f" >/dev/null 2>&1; then
        echo "  same       $f"
    else
        echo "  DIFFERENT  $f"
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "repro: identical. ROM SHA-256: $(sha256sum "$first/rom/vgpu98.bin" | cut -d' ' -f1)"
else
    echo "repro: outputs differ between builds" >&2
fi
exit "$status"
