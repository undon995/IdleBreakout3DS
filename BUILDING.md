# Build instructions

Build from **devkitPro MSYS2** on Windows with a working 3DS development environment (devkitARM and libctru). `make` is used for the native executable; `makerom` and `bannertool` are required only for the CIA package.

## 3DSX (Homebrew Launcher)

```bash
cd /c/Users/YOUR_USERNAME/Downloads/IdleBreakout3DS_GitHub_Source_Beta_0.9
bash BUILD_MSYS2.sh
```

Expected output: `IdleBreakout3DS.3dsx`.

## CIA (HOME Menu)

Put the `makerom` and `bannertool` executables in your PATH, in the project root, or in a local `tools/` directory. They are **not** included in the repository.

```bash
bash BUILD_CIA_MSYS2.sh
```

Expected output: `IdleBreakout3DS_Beta_0.9.cia`.

`bannertool` converts `cia_assets/icon.png`, `cia_assets/banner.png`, and `cia_assets/banner_once.wav` into the HOME Menu icon and banner. The banner WAV contains one short Basic Ball sound effect; the 3DS HOME Menu may replay the banner when the title is selected again.

## Build contents

- `source/main.c` — game rules, rendering, menus, input and saving.
- `source/assets.c` and `source/assets.h` — preconverted sprite pixels and font.
- `source/audio_data.c` and `source/audio_data.h` — preconverted PCM sound data.
- `cia_assets/` — icon, banner, WAV, and makerom RSF configuration.
- `Makefile`, `BUILD_MSYS2.sh`, `BUILD_CIA_MSYS2.sh` — native build files.

The code currently uses large embedded data arrays to avoid runtime asset loading. Raw source-game archives are not part of this repo and aren't needed for ordinary builds.

## Troubleshooting

- **`arm-none-eabi-gcc: command not found`** — build from devkitPro MSYS2 and check devkitARM.
- **`bannertool` or `makerom` missing** — provide the executable in PATH or `tools/` and retry.
- **ARM11 exception** — a successful compilation does not guarantee runtime compatibility. Record the exception type, current process, and PC address and file a bug report.
- **Missing saves** — check `sd:/3ds/IdleBreakout3DS/` and keep a backup before changing or reinstalling builds.

This code has not been recompiled in the environment used to prepare this repository archive. Please test the build on your own devkitPro installation before publishing new binaries.
