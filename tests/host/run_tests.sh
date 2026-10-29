#!/usr/bin/env bash
# Build and run the host unit tests with the native compiler.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tests/host/build"
mkdir -p "$OUT"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$ROOT/common/cocis_types/include" \
    -I "$ROOT/common/cocis_log_chain/include" \
    -I "$ROOT/common/cocis_time/include" \
    -I "$ROOT/common/cocis_proto/include" \
    -I "$ROOT/entrance_node/components/user_registry/include" \
    -I "$ROOT/entrance_node/components/occupancy/include" \
    -I "$ROOT/entrance_node/components/access_ctrl/include" \
    -I "$ROOT/entrance_node/components/door_fsm/include" \
    -I "$ROOT/room_node/components/light_fsm/include" \
    -I "$ROOT/room_node/components/energy/include" \
    -I "$ROOT/tests/host" \
    "$ROOT/tests/host/main.c" \
    "$ROOT/tests/host/test_types.c" \
    "$ROOT/tests/host/test_log_chain.c" \
    "$ROOT/tests/host/test_schedule.c" \
    "$ROOT/tests/host/test_proto.c" \
    "$ROOT/tests/host/test_registry.c" \
    "$ROOT/tests/host/test_access.c" \
    "$ROOT/tests/host/test_occupancy.c" \
    "$ROOT/tests/host/test_door.c" \
    "$ROOT/tests/host/test_light.c" \
    "$ROOT/common/cocis_log_chain/chain.c" \
    "$ROOT/common/cocis_time/schedule.c" \
    "$ROOT/common/cocis_proto/proto.c" \
    "$ROOT/entrance_node/components/user_registry/user_registry.c" \
    "$ROOT/entrance_node/components/occupancy/occupancy.c" \
    "$ROOT/entrance_node/components/access_ctrl/access_ctrl.c" \
    "$ROOT/entrance_node/components/door_fsm/door_fsm.c" \
    "$ROOT/room_node/components/light_fsm/light_fsm.c" \
    "$ROOT/room_node/components/light_fsm/daylight.c" \
    "$ROOT/room_node/components/energy/energy.c" \
    -o "$OUT/host_tests"

"$OUT/host_tests"
