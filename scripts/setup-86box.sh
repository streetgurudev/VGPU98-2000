#!/bin/sh
# Prepares the 86Box clone in <project>/86Box for VGPU98 work. Safe to run more than once.
#   - remote "upstream" is the official 86Box repository, fetch only (push URL disabled)
#   - a pre-push hook refuses any push to the official repository
#   - branch vgpu98 is checked out
#   - the local "release" build preset (86Box.exe -> <project>/bin) is installed
# 86Box/ is normally the fork as this repository's submodule (git clone --recursive), whose
# .git is a file naming its git directory; a plain clone of 86Box there works too.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
box="$root/86Box"
official=https://github.com/86Box/86Box.git

if [ ! -e "$box/.git" ]; then
    echo "setup-86box: no 86Box clone at $box (after a clone without --recursive: git submodule update --init)" >&2
    exit 1
fi

cd "$box"

# A plain clone of the official repository names it "origin". Rename it, so "origin" stays free
# for the fork; a clone of the fork keeps it as "origin" and gets "upstream" beside it.
if ! git remote get-url upstream >/dev/null 2>&1; then
    if [ "$(git remote get-url origin 2>/dev/null)" = "$official" ]; then
        git remote rename origin upstream
    else
        git remote add upstream "$official"
    fi
fi
git remote set-url --push upstream DISABLED

hooks=$(git rev-parse --git-path hooks)
mkdir -p "$hooks"
cp "$root/scripts/86box/pre-push" "$hooks/pre-push"
chmod +x "$hooks/pre-push"

if git show-ref --verify --quiet refs/heads/vgpu98; then
    git switch vgpu98
elif git show-ref --verify --quiet refs/remotes/origin/vgpu98; then
    if git symbolic-ref -q HEAD >/dev/null; then
        # A clone of the fork on another branch: the fork's own vgpu98.
        git switch -c vgpu98 --track origin/vgpu98
    else
        # A submodule, at the commit this repository records: the branch starts there.
        git switch -c vgpu98
        git branch --set-upstream-to=origin/vgpu98 vgpu98 >/dev/null
    fi
else
    git switch -c vgpu98 --no-track
fi

if [ ! -f CMakeUserPresets.json ]; then
    cp "$root/scripts/86box/CMakeUserPresets.json" .
fi

git remote -v
git branch --show-current
