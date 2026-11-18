#!/bin/sh
# The settings upgrades (versions 1, 2 and 3 -> 4) and reload, on the PC with made-up values (tools/test_settings.c).
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -O1 -Wall -Wextra -fsanitize=address,undefined -Itools/hoststub -o "$T/t" tools/test_settings.c src/settings.c
"$T/t"
