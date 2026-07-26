#!/usr/bin/env sh
set -eu
build=${1:-build}
config=${2:-Release}
prefix="$build/install"
cmake --install "$build" --config "$config" --prefix "$prefix"
cmake -S tests/consumer -B "$build/consumer" -DCMAKE_BUILD_TYPE="$config" -DCMAKE_PREFIX_PATH="$prefix"
cmake --build "$build/consumer" --config "$config"
ctest --test-dir "$build/consumer" -C "$config" --output-on-failure
