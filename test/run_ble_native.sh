#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p artifacts/milestone1
g++ -std=c++17 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer \
  -fsanitize=address,undefined -Iinclude test/ble_lifecycle.test.cpp \
  -o artifacts/milestone1/ble-policy-tests
ASAN_OPTIONS=detect_leaks=1 artifacts/milestone1/ble-policy-tests
