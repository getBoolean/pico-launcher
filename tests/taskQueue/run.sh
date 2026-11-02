#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
binary=$(mktemp /tmp/pico-task-queue.XXXXXX)
trap 'rm -f "$binary"' EXIT
# Production pool offsets use 32-bit pointer casts. Allow those on a 64-bit host;
# subtraction still gives the same offset within this small pool.
g++ -std=c++20 -fpermissive -fsanitize=address -g \
    -I"$root/tests/taskQueue/stubs" -I"$root/arm9/source" \
    "$root/tests/taskQueue/taskQueue.cpp" \
    "$root/arm9/source/core/task/Task.cpp" \
    "$root/arm9/source/core/task/TaskQueue.cpp" -o "$binary"
"$binary"
