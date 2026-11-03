#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
scratch=$(mktemp -d /tmp/pico-current-path.XXXXXX)
trap 'rm -rf "$scratch"' EXIT
g++ -std=c++20 -D_GLIBCXX_DEBUG -fsanitize=address,undefined -g \
    -I"$root/tests/search/path-stubs" -I"$root/arm9/source" \
    "$root/tests/search/current_directory_path.cpp" \
    "$root/arm9/source/fat/CurrentDirectoryPath.cpp" -o "$scratch/current-path"
"$scratch/current-path"
