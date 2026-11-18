#!/bin/sh
# Local time on the XL vs the PC's time zone database (tools/test_time.c).
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -O2 -Wall -Wextra -DORBS_HOST -fsanitize=address,undefined -o "$T/t" tools/test_time.c src/orbs_time.c
"$T/t"
