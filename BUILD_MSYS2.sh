#!/usr/bin/env bash
set -e
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
cd "$(dirname "$0")"
if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "ERROR: devkitARM compiler not found. Check your devkitPro install.";exit 1
fi
if [ ! -f "$DEVKITPRO/libctru/include/3ds.h" ];then
  echo "ERROR: libctru not found at $DEVKITPRO/libctru"; exit 1
fi
make clean
make -j1
if [ -s IdleBreakout3DS.3dsx ]; then
  echo "SUCCESS: $(pwd)/IdleBreakout3DS.3dsx"
  echo "Copy to SD:/3ds/IdleBreakout3DS/IdleBreakout3DS.3dsx"
else
  echo "ERROR: build finished without the expected 3dsx";exit 1
fi
