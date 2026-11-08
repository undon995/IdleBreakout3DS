# Idle Breakout 3DS — Beta 0.9

An **unofficial Nintendo 3DS homebrew adaptation** of Kodiqi's *Idle Breakout*.

This beta features touchscreen upgrades, multiple ball types, prestige progression, boss battles, offline earnings, and SD-card saving. The CIA build includes a custom HOME Menu icon and banner with a short Basic Ball sound.

## Release files

- **IdleBreakout3DS_Beta_0.9.cia** — HOME Menu installation for a CFW 3DS (use FBI).
- **IdleBreakout3DS.3dsx** — Homebrew Launcher build.
- **IdleBreakout3DS_GitHub_Source_Beta_0.9.zip** — full native C source and build scripts.

## Installation

Copy the CIA into `sd:/cias/` and install it from FBI. Alternatively, copy the 3DSX to `sd:/3ds/IdleBreakout3DS/` and launch from the Homebrew Launcher.

Saves are separate in `sd:/3ds/IdleBreakout3DS/`. **Back up your saves first.**

## Beta status

Some boss mechanics are approximations; performance and hardware compatibility require testing. If you see an ARM11 error, provide the 3DS model and a photo of the exception screen.

## Credits

Original *Idle Breakout* by Kodiqi. Unofficial fan-made adaptation; not affiliated with Nintendo or the original developer. **Check asset redistribution permission before publishing binaries/source containing the original artwork and sound.**

> **Maintainer checklist before publishing:** Rebuild the binaries from the cleaned source and test them on actual hardware. The two initially supplied compiled files predate the final source cleanup and may still show the older website text in-game.
