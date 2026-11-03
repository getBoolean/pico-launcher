#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
scratch=$(mktemp -d /tmp/pico-search.XXXXXX)
trap 'rm -rf "$scratch"' EXIT
# Keep the production folder/filter/FileInfo code together; replace hardware-only
# file types and FAT handles at their boundary so this runs on a host compiler.
cp "$root"/arm9/source/romBrowser/{SdFolder.cpp,SdFolder.h,FileInfo.cpp,FileInfo.h} "$scratch/"
cp -R "$root/tests/search/stubs/FileType" "$scratch/"
g++ -std=c++20 -D_GLIBCXX_DEBUG -fsanitize=address,undefined -g \
    -I"$root/tests/search/stubs" -I"$scratch" -I"$root/arm9/source/romBrowser" -I"$root/arm9/source" \
    "$root/tests/search/search.cpp" "$scratch/SdFolder.cpp" "$scratch/FileInfo.cpp" \
    "$root/arm9/source/core/StringUtil.cpp" -o "$scratch/search"
"$scratch/search"
g++ -std=c++20 -fsanitize=address,undefined -g \
    -I"$root/tests/search/stubs" -I"$root/arm7/source" \
    "$root/tests/search/touch_filter.cpp" "$root/arm7/source/touchScreen.cpp" -o "$scratch/touch-filter"
"$scratch/touch-filter"
g++ -std=c++20 -O2 -fsanitize=address,undefined -g \
    -I"$root/tests/search/stubs" -I"$root/tests/taskQueue/stubs" -I"$root/arm9/source" \
    "$root/tests/search/weak_ptr.cpp" "$root/arm9/source/core/WeakPtr.cpp" \
    "$root/arm9/source/core/SharedPtr.cpp" -o "$scratch/weak-ptr"
timeout 5 "$scratch/weak-ptr"
