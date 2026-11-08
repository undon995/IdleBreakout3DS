# Idle Breakout 3DS

**Beta 0.9 · Nintendo 3DS homebrew · Unofficial fan adaptation**

A native C/libctru adaptation of *Idle Breakout* for the Nintendo 3DS. Gameplay runs on the top screen, with menus and upgrades on the touchscreen. The project targets original and New 3DS hardware.

![Top-screen gameplay preview](docs/screenshots/top_preview.png)

## Downloads

The GitHub Release provides two formats:

| File | Use |
| --- | --- |
| `IdleBreakout3DS_Beta_0.9.cia` | Install from FBI on a custom-firmware 3DS to launch from HOME Menu |
| `IdleBreakout3DS.3dsx` | Run from the Homebrew Launcher |

### Installing the CIA

1. Copy the `.cia` file to a folder on the SD card, such as `sd:/cias/`.
2. Open FBI and browse to that file.
3. Select **Install CIA**.
4. Return to the HOME Menu and launch the game.

### Using the Homebrew Launcher

Copy `IdleBreakout3DS.3dsx` to `sd:/3ds/IdleBreakout3DS/IdleBreakout3DS.3dsx`, then launch it through the Homebrew Launcher.

**Back up your progress before switching builds.** Game saves are kept separately from the installed title in `sd:/3ds/IdleBreakout3DS/`. The active save is `save_v5.bin`; exported saves are stored under `saves/`.

## Gameplay and features

- Brick-breaker progression with multiple purchasable ball types and upgrades.
- Prestige upgrades, power-ups, special bricks, and laser effects.
- Boss battles and a 50-entry boss roster. Some boss-specific attacks are still approximated.
- Offline earnings, save export/import, settings, and a test/cheat menu.
- Custom HOME Menu icon and banner with a single Basic Ball sound effect.

This is a **beta**. Balancing, boss behavior, performance, and compatibility still need more hardware testing. See [Known issues](#known-issues).

## Building from source

The repository includes the C game code, preconverted embedded graphics/audio sources, `Makefile`, and shell build scripts. Raw web game files and website files are **not needed** to compile this native build.

See [BUILDING.md](BUILDING.md) for devkitPro MSYS2 instructions. The complete source ZIP is generated from this same repository layout.

## Known issues

- This release is not yet confirmed fully working on every 3DS model or firmware setup.
- Some unique boss attacks and boss-rush behavior are incomplete or approximated.
- Visual effects with many balls may affect frame rate on original 3DS hardware.
- The cheat/test menu is included in this beta.

Bug reports should include the model (Old/New 3DS), which build was used (`.cia` or `.3dsx`), and a photo of any exception screen.

## Credits and rights

*Idle Breakout* was created by **Kodiqi**. Original game concepts, art, and sound belong to their respective rights holders. This is an **unofficial fan-made adaptation**, not a Nintendo or Kodiqi release.

The source package contains derived game assets embedded in C files for the native renderer. Public redistribution of original game assets or binaries that include them may require permission from the rights holder. See [RIGHTS.md](RIGHTS.md).

Built using [devkitPro](https://devkitpro.org/) and libctru.
