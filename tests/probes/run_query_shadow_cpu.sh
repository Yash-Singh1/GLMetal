#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
probe_work=$(mktemp -d /tmp/glmetal-query-shadow.XXXXXX)
trap 'rm -rf "$probe_work"' EXIT
xcrun clang -arch arm64 -arch x86_64 -fsanitize=address -g -O1 -ffunction-sections \
    -Isrc -Ibuild/gen tests/probes/query_shadow_cpu.c -Wl,-dead_strip -o "$probe_work/query_shadow_cpu"
arch -arm64 "$probe_work/query_shadow_cpu"
arch -x86_64 "$probe_work/query_shadow_cpu"
