#!/usr/bin/env bash
# Packs the Nexus download into dist/:
#
#   OblivionFrameFixes-<version>.zip   OBSE/Plugins/OblivionFrameFixes.dll, .ini,
#                                      the license and the README as text -
#                                      relative to the game's Data folder, so a
#                                      mod manager places it (FoliageAA's layout)
#
#   tools/package-release.sh <version>
#
# The DLL is the MSVC build in build-win/ when there is one (the release
# build; README "Building"), else the clang-cl one in build-linux/. It says
# which. Runs in Git Bash on Windows (zips with Windows' own tar) and on Linux
# (zip).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:?usage: tools/package-release.sh <version>}"

dll=""
for candidate in "$root/build-win/OblivionFrameFixes.dll" "$root/build-linux/OblivionFrameFixes.dll"; do
	if [[ -f "$candidate" ]]; then
		dll="$candidate"
		break
	fi
done
[[ -n "$dll" ]] || { echo "no DLL in build-win/ or build-linux/ - build first (README, Building)" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
plugins="$work/OBSE/Plugins"
mkdir -p "$plugins"
cp "$dll" "$plugins/"
cp "$root/OblivionFrameFixes.ini" "$plugins/"
cp "$root/LICENSE" "$plugins/OblivionFrameFixes-LICENSE.txt"
cp "$root/README.md" "$plugins/OblivionFrameFixes-README.txt"

mkdir -p "$root/dist"
zip_path="$root/dist/OblivionFrameFixes-$version.zip"
rm -f "$zip_path"
if command -v zip >/dev/null; then
	(cd "$work" && zip -qr "$zip_path" OBSE)
else
	# Windows' bsdtar writes a zip with forward slashes (-a picks the format
	# from the name); Git Bash's own tar cannot.
	(cd "$work" && "$SYSTEMROOT/System32/tar.exe" -a -cf "$(cygpath -w "$zip_path")" OBSE)
fi
unzip -l "$zip_path"
echo "DLL: $dll"
