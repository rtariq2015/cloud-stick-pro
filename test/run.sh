#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 test/critical_sections.test.py
mkdir -p artifacts
g++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude -Ilib/tinyxml2/src \
  lib/tinyxml2/src/tinyxml2.cpp src/pro_core.cpp src/pro_journal.cpp src/pro_http.cpp test/native.cpp -lcrypto -o artifacts/native-tests
ASAN_OPTIONS=detect_leaks=1 python3 test/mock_webdav.py
