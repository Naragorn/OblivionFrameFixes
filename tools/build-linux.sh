#!/usr/bin/env bash
# Builds OblivionFrameFixes.dll on Linux with clang-cl and lld-link against
# xwin's winsysroot (cmake/toolchain-linux-clang-cl.cmake), after running the
# unit tests natively (clang++ -m32). Releases are built with MSVC on Windows;
# this is the build for working on Linux.
#
#   tools/build-linux.sh              tests, then the DLL into build-linux/
#   tools/build-linux.sh --deploy DIR also copy the DLL and the INI into DIR
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deploy=""
if [[ "${1:-}" == "--deploy" ]]; then
	deploy="${2:?--deploy needs a folder}"
fi

cmake -S "$root/tests" -B "$root/build-linux-tests" -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 >/dev/null
cmake --build "$root/build-linux-tests"
ctest --test-dir "$root/build-linux-tests" --output-on-failure

cmake -S "$root" -B "$root/build-linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	--toolchain "$root/cmake/toolchain-linux-clang-cl.cmake" >/dev/null
cmake --build "$root/build-linux"
ls -la "$root/build-linux/OblivionFrameFixes.dll"

if [[ -n "$deploy" ]]; then
	cp "$root/build-linux/OblivionFrameFixes.dll" "$deploy/"
	[[ -f "$deploy/OblivionFrameFixes.ini" ]] || cp "$root/OblivionFrameFixes.ini" "$deploy/"
	echo "Deployed to $deploy"
fi
