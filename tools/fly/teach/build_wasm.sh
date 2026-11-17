#!/usr/bin/env bash
# Builds the teach rig (dendrite's echo, the itch off, the fly's body played by hand) to
# WebAssembly, then the teach page around it.
#   needs emsdk (https://emscripten.org): source emsdk_env.sh first
#   tools/fly/teach/build_wasm.sh            -> build/teach/rig.wasm and build/teach/teach.html
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p build/teach
# FlyData is the fly's brain; the page never runs it, but the engine links it. A stub keeps the
# module small (the fly stays asleep: useFly(false), the itch off).
em++ -O3 -std=c++20 -I Source -I tools/fly/teach \
    -fno-exceptions -fno-rtti -msimd128 \
    tools/fly/teach/teach_wasm.cpp $(ls Source/dsp/*.cpp | grep -v FlyData.cpp) tools/fly/teach/flydata_stub.cpp \
    -sSTANDALONE_WASM=1 --no-entry -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 \
    -sERROR_ON_UNDEFINED_SYMBOLS=1 \
    -o build/teach/rig.wasm
ls -l build/teach/rig.wasm
[ -f tools/fly/teach/make_page.py ] && python3 tools/fly/teach/make_page.py build/teach/rig.wasm build/teach/teach.html
