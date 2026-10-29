#!/usr/bin/env bash
# Build and run the host unit tests with the native compiler.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tests/host/build"
mkdir -p "$OUT"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$ROOT/common/cocis_types/include" \
    -I "$ROOT/common/cocis_log_chain/include" \
    -I "$ROOT/tests/host" \
    "$ROOT/tests/host/main.c" \
    "$ROOT/tests/host/test_types.c" \
    "$ROOT/tests/host/test_log_chain.c" \
    "$ROOT/common/cocis_log_chain/chain.c" \
    -o "$OUT/host_tests"

"$OUT/host_tests"
