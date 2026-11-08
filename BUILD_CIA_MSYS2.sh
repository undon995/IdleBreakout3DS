#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"

find_tool() {
  local name="$1"
  local candidate
  if command -v "$name" >/dev/null 2>&1; then
    command -v "$name"
    return 0
  fi
  if command -v "$name.exe" >/dev/null 2>&1; then
    command -v "$name.exe"
    return 0
  fi
  for candidate in "./tools/$name" "./tools/$name.exe" "./$name" "./$name.exe"; do
    if [[ -f "$candidate" ]]; then
      printf '%s\n' "$PWD/${candidate#./}"
      return 0
    fi
  done
  return 1
}

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo 'Missing devkitARM: open devkitPro MSYS2 and install the 3DS toolchain.' >&2
  exit 1
fi

BANNERTOOL=$(find_tool bannertool) || {
  echo 'Missing bannertool. Add it to PATH or place bannertool.exe in tools/.' >&2
  exit 1
}
MAKEROM=$(find_tool makerom) || {
  echo 'Missing makerom. Add it to PATH or place makerom.exe in tools/.' >&2
  exit 1
}

bash BUILD_MSYS2.sh
[[ -s IdleBreakout3DS.elf ]] || {
  echo 'Build did not produce an ELF.' >&2
  exit 1
}

mkdir -p cia_assets
"$BANNERTOOL" makesmdh \
  -s 'Idle Breakout 3DS' \
  -l 'Unofficial Beta 0.9' \
  -p 'Fan-made / Kodiqi original' \
  -i cia_assets/icon.png -o cia_assets/icon.icn
"$BANNERTOOL" makebanner \
  -i cia_assets/banner.png \
  -a cia_assets/banner_once.wav \
  -o cia_assets/banner.bnr

"$MAKEROM" -f cia \
  -o IdleBreakout3DS_Beta_0.9.cia \
  -DAPP_ENCRYPTED=false -target t -exefslogo \
  -elf IdleBreakout3DS.elf \
  -rsf cia_assets/idlebreakout.rsf \
  -icon cia_assets/icon.icn \
  -banner cia_assets/banner.bnr

[[ -s IdleBreakout3DS_Beta_0.9.cia ]] || {
  echo 'CIA file missing after build.' >&2
  exit 1
}
ls -lh IdleBreakout3DS_Beta_0.9.cia IdleBreakout3DS.3dsx
printf 'Build completed. Back up your SD saves before installing.\n'
