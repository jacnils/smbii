#!/usr/bin/env bash

set -Eeuo pipefail

if [[ ! -d "src" || ! -f "CMakeLists.txt" ]]; then
    echo "Please run this script from the project root." >&2
    exit 1
fi

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLCHAIN="${DEVKITPRO}/cmake/Wii.cmake"

if [[ ! -f "$TOOLCHAIN" ]]; then
    echo "Wii toolchain not found: $TOOLCHAIN" >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is required." >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is required." >&2
    exit 1
fi

python3 ./tools/wad2hpp.py forwarder.wad > src/wad_data.hpp

rm -rf build
mkdir -p build

cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel

mkdir -p build/apps/smbii/themes

cp -r theme build/apps/smbii/themes/default_theme
cp build/smbii.dol build/apps/smbii/boot.dol
cp meta.xml build/apps/smbii/
cp icon.png build/apps/smbii/

tar -czf build/smbii.tar.gz -C build apps

echo "Build completed successfully."
echo "Release archive: build/smbii.tar.gz"