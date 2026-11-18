#!/bin/sh
# Builds the orbs' XL firmware into build/orbs_xl.uf2.
#   PICO_SDK_PATH=/path/to/pico-sdk ./build.sh
# Needs: the Raspberry Pi Pico SDK (built and tested with 2.3.1), CMake 3.13+, Ninja, Python 3 and an Arm GNU
# toolchain (arm-none-eabi, tested with 15.2.rel1). If the toolchain isn't on your PATH, give its folder as
# PICO_TOOLCHAIN_PATH. The SDK downloads picotool's source for the build unless PICOTOOL_FETCH_FROM_GIT_PATH or
# FETCHCONTENT_SOURCE_DIR_PICOTOOL says where a copy is.
set -e
cd "$(dirname "$0")"
python3 -I tools/embed.py >/dev/null
: "${PICO_SDK_PATH:?set PICO_SDK_PATH to your pico-sdk folder}"
if [ ! -f build/CMakeCache.txt ]; then
	set -- -DPICO_SDK_PATH="$PICO_SDK_PATH"
	[ -n "$PICO_TOOLCHAIN_PATH" ] && set -- "$@" -DPICO_TOOLCHAIN_PATH="$PICO_TOOLCHAIN_PATH"
	[ -n "$FETCHCONTENT_SOURCE_DIR_PICOTOOL" ] && set -- "$@" -DFETCHCONTENT_SOURCE_DIR_PICOTOOL="$FETCHCONTENT_SOURCE_DIR_PICOTOOL"
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
else
	cmake -S . -B build >/dev/null       # (again each time: the build's name is its date and time)
fi
cmake --build build
ls -l build/orbs_xl.uf2
cat build/orbs_xl.version
