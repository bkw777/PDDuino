#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/fakes tests/firmware_test.cpp -o "$build_dir/firmware_test"
"$build_dir/firmware_test"
