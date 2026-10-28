#!/bin/sh
# Builds everything needed to test VGPU98 in 86Box. It never starts 86Box.
#   1. checks that this repository and the 86Box clone have no uncommitted
#      changes (--allow-dirty skips this), so the build matches its commits,
#      and that bin/86Box.exe is not running (it is rebuilt)
#   2. deletes both build trees (build/ and 86Box/build/release/), so every
#      release is a clean rebuild, then configures and builds the tools, the
#      generated files, the ROM (romfix and romlint run as part of the build),
#      the Windows 98 driver (drvlint runs as part of the build), the driver
#      package and the build manifest; the host-side BIOS and driver tests are
#      required
#   3. runs the test suite
#   4. checks that the 86Box tree carries the current card header
#   5. copies the ROM to bin/roms/video/vgpu98/vgpu98.bin
#   6. builds the 86Box release preset, which writes bin/86Box.exe, and checks
#      that the exe is new
#   7. copies the driver package to dist/vgpu98-driver/ (INF, display driver,
#      mini-VDD, DirectX driver, Glide DLLs, OpenGL ICD, control program), a
#      folder 86Box can mount as a CD, and its CD image to
#      dist/vgpu98-driver.iso
#   8. writes the release zip, dist/VGPU98-2000-<version>-win64.zip: 86Box.exe,
#      the ROM, the CD image, a quick start, the licences and third-party
#      notices, where the source is, and the card's changes to 86Box as one diff
#      (package/release/; zipgen, read back and checked)
#   9. writes build/release.txt: both commits, the drivers' build stamp and the
#      SHA-256 of the ROM, of every driver file, of the CD image and of the zip
# Every build gets the same build stamp and time (cmake/build-stamp.cmake),
# the reproducibility check's copies outside git included.
# With --repro it also runs scripts/repro.sh before staging.
#
# Run from an MSYS2 UCRT64 shell (cmake, ninja, gcc, nasm and git on PATH).
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
box="$root/86Box"
repro=0
allow_dirty=0

for arg in "$@"; do
    case "$arg" in
        --repro) repro=1 ;;
        --allow-dirty) allow_dirty=1 ;;
        *) echo "usage: release.sh [--repro] [--allow-dirty]" >&2; exit 2 ;;
    esac
done

step() {
    echo
    echo "== release: $*"
}

# Prints "<short commit>" or "<short commit> + uncommitted changes" for a repository.
describe() {
    if [ -n "$(git -C "$1" status --porcelain --untracked-files=no)" ]; then
        echo "$(git -C "$1" rev-parse --short HEAD) + uncommitted changes"
    else
        git -C "$1" rev-parse --short HEAD
    fi
}

step "working trees"
for repo in "$root" "$box"; do
    if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
        if [ "$allow_dirty" -eq 0 ]; then
            echo "release: $repo has uncommitted changes; commit them or pass --allow-dirty" >&2
            git -C "$repo" status --short --untracked-files=no >&2
            exit 1
        fi
        echo "release: building $repo with uncommitted changes (--allow-dirty)"
    fi
done
project_rev=$(describe "$root")
box_rev="$(git -C "$box" rev-parse --abbrev-ref HEAD) $(describe "$box")"
echo "project: $project_rev"
echo "86Box:   $box_rev"

# The drivers' build stamp, the same for every build (repro.sh reads it too).
VGPU98_BUILD_STAMP=$(git -C "$root" rev-parse --short=7 HEAD)
if ! git -C "$root" diff --quiet --ignore-submodules HEAD --; then
    VGPU98_BUILD_STAMP="$VGPU98_BUILD_STAMP-dirty"
fi
VGPU98_BUILD_TIME=$(git -C "$root" log -1 --format=%ct)
export VGPU98_BUILD_STAMP VGPU98_BUILD_TIME
echo "build stamp: $VGPU98_BUILD_STAMP"

# The rebuilt exe replaces bin/86Box.exe, which Windows will not do while it runs.
exe="$root/bin/86Box.exe"
if [ -f "$exe" ]; then
    running=$(/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -Command \
        "Get-Process -Name 86Box -ErrorAction SilentlyContinue | ForEach-Object { \$_.Path }" | tr -d '\r')
    if printf '%s\n' "$running" | grep -Fqix "$(cygpath -w "$exe")"; then
        echo "release: bin/86Box.exe is running; close 86Box first" >&2
        exit 1
    fi
fi

step "clean"
rm -rf "$root/build" "$box/build/release"
echo "deleted build/ and 86Box/build/release/"

step "configure and build (tools, generated files, ROM, manifest)"
(cd "$root" && cmake --preset default -DVGPU98_REQUIRE_BIOSTEST=ON -DVGPU98_BUILD_STAMP="$VGPU98_BUILD_STAMP" \
    -DVGPU98_BUILD_TIME="$VGPU98_BUILD_TIME" >/dev/null)
stamp="$root/build/release-start.stamp"
touch "$stamp"
cmake --build "$root/build"

step "tests"
for t in bios_on_host driver_on_host minivdd_on_host; do
    if ! ctest --test-dir "$root/build" -N | grep -q "$t"; then
        echo "release: the host-side test $t is not registered" >&2
        exit 1
    fi
done
ctest --test-dir "$root/build" --output-on-failure
if ! ctest --test-dir "$root/build" -N | grep -q "fonts_independent"; then
    echo "release: note: no reference VGA BIOS image, so the font independence check did not run"
fi

step "86Box header matches the card definition"
cmake --build "$root/build" --target check-86box-header

if [ "$repro" -eq 1 ]; then
    step "reproducibility"
    sh "$root/scripts/repro.sh"
fi

step "stage the ROM"
cmake --build "$root/build" --target stage-rom

step "86Box release build"
if [ ! -f "$box/CMakeUserPresets.json" ]; then
    echo "release: $box has no release preset; run scripts/setup-86box.sh first" >&2
    exit 1
fi
(cd "$box" && cmake --preset release >/dev/null && cmake --build --preset release)

rom="$root/bin/roms/video/vgpu98/vgpu98.bin"
for f in "$rom" "$exe"; do
    if [ ! -f "$f" ]; then
        echo "release: $f is missing" >&2
        exit 1
    fi
done
if [ ! "$exe" -nt "$stamp" ]; then
    echo "release: bin/86Box.exe was not rebuilt" >&2
    exit 1
fi
if ! cmake -E compare_files "$root/build/rom/vgpu98.bin" "$rom"; then
    echo "release: the staged ROM differs from build/rom/vgpu98.bin" >&2
    exit 1
fi
rom_sha=$(sha256sum "$rom" | cut -d' ' -f1)

step "driver package"
# The folder may be mounted in 86Box (as a CD), so it is updated in place
# rather than deleted: the files are overwritten, anything else is removed.
dist="$root/dist"
pkg="$dist/vgpu98-driver"
files="vgpu98.inf vgpu98.drv vgpu98.vxd vgpu98dx.dll glide2x.dll glide3x.dll vgpu98gl.dll vgpu98cp.cpl"
mkdir -p "$pkg"
for f in "$pkg"/* "$pkg"/.[!.]*; do
    [ -e "$f" ] || continue
    case " $files " in
        *" $(basename "$f") "*) ;;
        *) rm -rf "$f" ;;
    esac
done
rm -f "$dist/vgpu98-driver.img"         # an earlier release's floppy image
for f in $files; do
    cp "$root/build/package/vgpu98-driver/$f" "$pkg/$f"
    if ! cmake -E compare_files "$root/build/package/vgpu98-driver/$f" "$pkg/$f"; then
        echo "release: $pkg/$f could not be updated" >&2
        exit 1
    fi
done
# The CD image may be mounted too: copied over, not deleted first.
cp "$root/build/package/vgpu98-driver.iso" "$dist/vgpu98-driver.iso"
if ! cmake -E compare_files "$root/build/package/vgpu98-driver.iso" "$dist/vgpu98-driver.iso"; then
    echo "release: $dist/vgpu98-driver.iso could not be updated (is it mounted in a running 86Box?)" >&2
    exit 1
fi

step "release zip"
# The download for people who do not build. Its text files come from the
# commits, not the working trees, so line ends never differ between checkouts.
version=$(sed -n 's/^project(VGPU98 VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
base=$(cat "$root/scripts/86box/base")
if [ -z "$version" ] || ! git -C "$box" merge-base --is-ancestor "$base" HEAD; then
    echo "release: no version in CMakeLists.txt, or 86Box's base $base (scripts/86box/base) is not in the clone's history" >&2
    exit 1
fi
name="VGPU98-2000-$version"
stage="$root/build/release-zip"
mkdir -p "$stage"
git -C "$root" show HEAD:package/release/README.txt | sed "s/@VERSION@/$version/g" > "$stage/README.txt"
git -C "$root" show HEAD:package/release/THIRD-PARTY-NOTICES.txt > "$stage/THIRD-PARTY-NOTICES.txt"
git -C "$root" show HEAD:LICENSE > "$stage/LICENSE.txt"
git -C "$box" show HEAD:COPYING > "$stage/COPYING.txt"
git -C "$box" diff "$base" HEAD > "$stage/vgpu98.patch"
tag_of() { git -C "$1" describe --tags --exact-match HEAD 2>/dev/null || echo "(none)"; }
{
    echo "VGPU98 2000 $version: the source code"
    echo
    echo "The card's BIOS, Windows 98 drivers, tools and documentation:"
    echo "  https://github.com/streetgurudev/VGPU98-2000"
    echo "  tag $(tag_of "$root"), commit $(git -C "$root" rev-parse HEAD)"
    echo
    echo "86Box with the card (86Box.exe):"
    echo "  https://github.com/streetgurudev/86Box-VGPU98"
    echo "  tag $(tag_of "$box"), commit $(git -C "$box" rev-parse HEAD)"
    echo "  on 86Box's commit $base; vgpu98.patch is the difference"
    echo
    echo "Built with MSYS2's UCRT64 toolchain, at the versions in toolchain.lock."
    echo "The drivers' build stamp: $VGPU98_BUILD_STAMP"
    echo
    echo "SHA-256:"
    (cd "$stage/.." && sha256sum "$exe" "$rom" "$dist/vgpu98-driver.iso" | sed -e 's#\*\?/.*/##' -e 's/^/  /')
} > "$stage/SOURCE.txt"
for f in "$dist"/VGPU98-2000-*-win64.zip; do
    if [ -e "$f" ]; then rm -f "$f"; fi
done
zip="$dist/$name-win64.zip"
"$root/build/tools/zipgen" -o "$zip" -t "$VGPU98_BUILD_TIME" \
    "$name/README.txt=$stage/README.txt" \
    "$name/86Box.exe=$exe" \
    "$name/roms/video/vgpu98/vgpu98.bin=$rom" \
    "$name/vgpu98-driver.iso=$dist/vgpu98-driver.iso" \
    "$name/THIRD-PARTY-NOTICES.txt=$stage/THIRD-PARTY-NOTICES.txt" \
    "$name/LICENSE.txt=$stage/LICENSE.txt" \
    "$name/COPYING.txt=$stage/COPYING.txt" \
    "$name/SOURCE.txt=$stage/SOURCE.txt" \
    "$name/vgpu98.patch=$stage/vgpu98.patch"
zip_sha=$(sha256sum "$zip" | cut -d' ' -f1)
echo "$(basename "$zip"): $(wc -c < "$zip") bytes"

{
    echo "VGPU98 release build (generated by scripts/release.sh)"
    echo "project:     $project_rev"
    echo "86Box:       $box_rev"
    echo "build stamp: $VGPU98_BUILD_STAMP"
    echo "ROM SHA-256: $rom_sha"
    echo "Driver package SHA-256:"
    (cd "$dist" && sha256sum vgpu98-driver/vgpu98.inf vgpu98-driver/vgpu98.drv vgpu98-driver/vgpu98.vxd vgpu98-driver/vgpu98dx.dll \
        vgpu98-driver/glide2x.dll vgpu98-driver/glide3x.dll vgpu98-driver/vgpu98gl.dll vgpu98-driver/vgpu98cp.cpl \
        vgpu98-driver.iso | sed 's/^/  /')
    echo "Release zip SHA-256:"
    echo "  $zip_sha  $(basename "$zip")"
} > "$root/build/release.txt"

# The release's stamp was for this build: later builds in build/ take theirs
# from git again (nothing is rebuilt now).
cmake -S "$root" -B "$root/build" -UVGPU98_BUILD_STAMP -UVGPU98_BUILD_TIME >/dev/null

step "done"
echo "ROM:    bin/roms/video/vgpu98/vgpu98.bin"
echo "        SHA-256 $rom_sha"
echo "86Box:  bin/86Box.exe ($box_rev)"
echo "Driver: dist/vgpu98-driver/ (vgpu98.inf, vgpu98.drv, vgpu98.vxd, vgpu98dx.dll, glide2x.dll, glide3x.dll, vgpu98gl.dll, vgpu98cp.cpl), build $VGPU98_BUILD_STAMP"
echo "        in 86Box: Media > CD-ROM > Folder (mount it again after a rebuild)"
echo "        or the CD image dist/vgpu98-driver.iso (Media > CD-ROM > Image)"
echo "Zip:    dist/$(basename "$zip")"
echo "        SHA-256 $zip_sha"
echo "Record: build/release.txt"
echo "Select \"VGPU98 2000 (PCI)\" under Display in the machine settings."
