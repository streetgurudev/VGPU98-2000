#!/bin/sh
# Writes the release's third-party notices for 86Box.exe: the libraries its
# link line takes from MSYS2's packages (found with pacman), the components
# 86Box's own source tree bundles, and every licence text once, from the
# licence files the packages and the source tree hold. Nothing is downloaded.
#
#   scripts/third-party-notices.sh [86Box build dir] [output]
#
# Run in an MSYS2 UCRT64 shell after an 86Box release build. The output,
# package/release/THIRD-PARTY-NOTICES.txt by default, is committed: run this
# again when toolchain.lock or 86Box's libraries change, and commit the result.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
box="$root/86Box"
build=${1:-$box/build/release}
out=${2:-$root/package/release/THIRD-PARTY-NOTICES.txt}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
prefix=mingw-w64-ucrt-x86_64-

# The libraries 86Box.exe links: the link line's MSYS2 archives and -l names,
# and the runtime GCC links in itself (libgcc, libstdc++, winpthreads, the CRT).
awk '/^build .*86Box\.exe: /{p=1} p && /LINK_LIBRARIES/{print; exit}' "$build/build.ninja" | tr ' ' '\n' > "$work/line"
{
    grep -i -E '^[A-Za-z]:/msys64/.*\.a$' "$work/line" | sed -E 's#^[A-Za-z]:/msys64##'
    sed -n 's/^-l\(.*\)/\/ucrt64\/lib\/lib\1.a/p' "$work/line"
    echo /ucrt64/lib/libstdc++.a
    echo /ucrt64/lib/libwinpthread.a
    echo /ucrt64/lib/libmingw32.a
} | sort -u | while read -r a; do
    if [ -f "$a" ]; then pacman -Qoq "$a" 2>/dev/null || true; fi
done | sort -u > "$work/packages"

declared() { pacman -Qi "$1" | sed -n 's/^Licenses *: //p' | sed 's/spdx://g; s/  */ /g'; }
homepage() { pacman -Qi "$1" | sed -n 's/^URL *: //p'; }
version() { pacman -Q "$1" | cut -d' ' -f2; }

# Licence files: a package's own, or a note when it installs none. Qt's
# commercial agreement and documentation licence do not apply to 86Box.exe,
# and READMEs are not licences.
license_dir() {
    case "${1#"$prefix"}" in
        qt5-static) echo /ucrt64/qt5-static/share/licenses/qt5 ;;
        portaudio) echo /ucrt64/share/doc/portaudio ;;
        *) echo "/ucrt64/share/licenses/${1#"$prefix"}" ;;
    esac
}
license_files() {
    d=$(license_dir "$1")
    [ -d "$d" ] || return 0
    (cd "$d" && find . -type f \( -iname 'licen*' -o -iname 'copying*' -o -iname 'copyright*' -o -iname '*.txt' \) \
        ! -name 'LICENSE.FDL' ! -name 'LICENSE.QT-LICENSE-AGREEMENT' ! -iname 'readme*' | sed 's#^\./##' | sort)
}

: > "$work/seen"
text() { # text LABEL FILE: a licence text, or a pointer to the same text above
    sum=$(sha256sum < "$2" | cut -c1-64)
    first=$(awk -v s="$sum" '$1 == s { $1 = ""; sub(/^ /, ""); print; exit }' "$work/seen")
    echo "---- $1 ----"
    echo
    if [ -n "$first" ]; then
        echo "The same text as $first, above."
    else
        echo "$sum $1" >> "$work/seen"
        tr -d '\r' < "$2"
    fi
    echo
}

{
cat <<'EOF'
THIRD-PARTY NOTICES

86Box.exe in this package is 86Box (GNU GPL version 2 or later: COPYING.txt)
with the VGPU98 2000 card added (GNU GPL version 2 or later: LICENSE.txt),
built with MSYS2's UCRT64 toolchain. It contains third-party code: libraries
from MSYS2's packages, linked into it, and components that 86Box's own source
tree bundles. Each keeps its own licence, listed below with its text.

Some of these libraries are licensed under the GNU GPL version 3, the GNU LGPL
version 3 or the Apache License 2.0, so 86Box.exe as a whole is distributed
under the terms of the GNU GPL version 3, which the "version 2 or later"
licences of 86Box and the VGPU98 2000 allow.

Source code: 86Box with the card, the bundled components included, is at
https://github.com/streetgurudev/86Box-VGPU98; the card's BIOS and drivers at
https://github.com/streetgurudev/VGPU98-2000. The libraries are MSYS2's
packages at the versions listed (https://packages.msys2.org), as MSYS2 builds
them from their projects' sources; its build recipes and patches are at
https://github.com/msys2/MINGW-packages.


PART 1: LIBRARIES FROM MSYS2 PACKAGES

EOF
while read -r p; do
    printf '%s %s\n    Licence: %s\n    Home: %s\n' "${p#"$prefix"}" "$(version "$p")" "$(declared "$p")" "$(homepage "$p")"
done < "$work/packages"
cat <<'EOF'


PART 2: COMPONENTS BUNDLED IN 86BOX'S SOURCE

ayumi (AY-3-8910/YM2149 emulation): MIT. src/sound/ayumi
Dear ImGui: MIT. src/osd/imgui
ESFMu, with code from Nuked OPL3: GNU LGPL 2.1 or later. src/sound/esfmu
libchdr: BSD 3-Clause. src/cdrom/libchdr
LZMA SDK (in libchdr): see its notice below. src/cdrom/libchdr/deps
LZW library: GNU LGPL 3 or later. src/floppy/lzw
MiniVHD: see its notice below. src/disk/minivhd
Munt (mt32emu): GNU LGPL 2.1 or later. src/sound/munt
reSIDfp: GNU GPL 2 or later. src/sound/resid-fp
Berkeley SoftFloat 3e: BSD 3-Clause. src/cpu/softfloat3e
ymfm: BSD 3-Clause. src/sound/ymfm

The LGPL texts for ESFMu, the LZW library and Munt are in Part 3, under the
packages that install them.


PART 3: LICENCE TEXTS

EOF
while read -r p; do
    files=$(license_files "$p")
    if [ -z "$files" ]; then
        echo "---- ${p#"$prefix"} ----"
        echo
        case "${p#"$prefix"}" in
            libvorbis) echo "Its package installs no licence file. libvorbis is under the Xiph.Org BSD-style licence, the same terms as libogg's text above, copyright the Xiph.Org Foundation." ;;
            *) echo "Its package installs no licence file. Its package declares its licence as: $(declared "$p"). Those texts are given under other packages here." ;;
        esac
        echo
        continue
    fi
    for f in $files; do
        text "${p#"$prefix"}: $f" "$(license_dir "$p")/$f"
    done
done < "$work/packages"
for c in "ayumi: LICENSE|src/sound/ayumi/LICENSE" "Dear ImGui: LICENSE.txt|src/osd/imgui/LICENSE.txt" \
         "libchdr: LICENSE.txt|src/cdrom/libchdr/LICENSE.txt" \
         "LZMA SDK: LICENSE|$(cd "$box" && ls -d src/cdrom/libchdr/deps/lzma-*/LICENSE | head -1)" \
         "MiniVHD: LICENSE|src/disk/minivhd/LICENSE" "reSIDfp: COPYING|src/sound/resid-fp/COPYING" \
         "Berkeley SoftFloat 3e: COPYING.txt|src/cpu/softfloat3e/COPYING.txt"; do
    text "${c%%|*}" "$box/${c#*|}"
done
sed -n '1,/^$/p' "$box/src/sound/ymfm/ymfm.h" | sed 's#^// \{0,1\}##' > "$work/ymfm"
text "ymfm: the licence in ymfm.h" "$work/ymfm"
} > "$out"
echo "third-party-notices: $(wc -l < "$work/packages") packages, written to $out"
