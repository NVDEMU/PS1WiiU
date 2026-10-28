# PS1WiiU

A native Wii U / Aroma PlayStation 1 emulator frontend using the open-source PCSX-ReARMed core.

**Development status:** the native frontend, core integration, GamePad input adapter, basic audio/video output, game-image browser, and core-option settings are in development. GitHub Actions verifies the cross-build, but successful compilation does not replace testing on a physical Wii U; game compatibility and performance are not yet guaranteed.

## Features

- Native WUT/Aroma application packaged as a `.wuhb`.
- Uses PCSX-ReARMed's Wii U static libretro-core target instead of a hand-written PS1 CPU emulator.
- GamePad-driven game library and settings menu.
- GamePad buttons, D-pad, and both analog sticks mapped to a PlayStation controller.
- TV and GamePad video output; audio uses the Wii U audio system when voices are available.
- Configurable region, BIOS mode, dynamic recompiler, CPU clock, dithering, frameskip, RGB32 output, boot logo, memory cards, GPU list accuracy, and fractional frame rate.
- Settings stored in `wiiu/apps/PS1WiiU/settings.cfg`.
- GitHub Actions runs on every push and pull request. It uploads a complete SD-card layout as a workflow artifact and updates the **Nightly** GitHub Release from `main`.

## Install

1. Download `PS1WiiU-SD.zip` from the latest successful [GitHub Actions run](https://github.com/NVDEMU/PS1WiiU/actions) or the [Nightly Release](https://github.com/NVDEMU/PS1WiiU/releases/tag/Nightly).
2. Extract the ZIP to the root of your Wii U SD card.
3. Launch `PS1WiiU.wuhb` from the Wii U Menu with Aroma.
4. Copy your own supported PlayStation image files into `sd:/wiiu/apps/PS1WiiU/games/`.
5. Optional: place a PlayStation BIOS file you legally dumped into `sd:/wiiu/apps/PS1WiiU/bios/`. BIOS selection can be set in Settings; automatic/HLE fallback is intended to help when a dumped BIOS is not available.

The installer ZIP creates the app's data directories so the first launch has a predictable layout. **No BIOS files or commercial games are included.**

## SD-card layout

```
sd:/
└── wiiu/
    └── apps/
        ├── PS1WiiU.wuhb
        └── PS1WiiU/
            ├── games/        # Your own .cue, .chd, .pbp, .iso, or .img files
            ├── bios/         # Optional BIOS files dumped by you
            ├── saves/        # PCSX-ReARMed save data / memory-card files
            ├── memcards/     # Reserved for future dedicated card management
            ├── screenshots/  # Reserved for future screenshot support
            └── settings.cfg  # Created by the app
```

For multi-track disc dumps, launch the `.cue` file and keep its referenced tracks in the same relative locations. The app's browser intentionally does not list standalone `.bin` tracks, to avoid confusing a track file with a complete disc image.

## GamePad controls

| Wii U GamePad | PlayStation input |
|---|---|
| A | Cross |
| B | Circle |
| Y | Square |
| X | Triangle |
| D-pad | D-pad |
| Left stick | Left analog stick |
| Right stick | Right analog stick |
| L / R | L1 / R1 |
| ZL / ZR | L2 / R2 |
| Minus / Plus | Select / Start |
| Left-stick / right-stick click | L3 / R3 |

In the library, use Up/Down to select a game, A to launch, and Y to rescan. In Settings, use Up/Down to select an option and Left/Right or A to cycle values. Press X to switch pages. While playing, hold **L + R + Minus** for about three-quarters of a second to return to the library.

## Emulation options

Settings are saved to `settings.cfg` and supplied to PCSX-ReARMed through its libretro environment:
- **Region:** Auto, NTSC, PAL.
- **BIOS:** Auto or HLE.
- **Dynamic recompiler:** enabled or disabled.
- **PS1 CPU clock:** Auto, 57%, 66%, 75%, 90%, 100%.
- **Dithering:** enabled, disabled, force.
- **Frameskip:** disabled, auto, auto-threshold, fixed interval.
- **RGB32 output:** enabled or disabled.
- **BIOS boot logo:** enabled or disabled.
- **Memory Card 1 / 2**, accurate GPU-list processing, and fractional frame rate.

Some changes only take effect on the next game load. The full set of PCSX-ReARMed options is broader than the first native menu exposes; unexposed options retain core defaults.

## Build locally

Install devkitPro's Wii U toolchain and WUT, then clone recursively so the core and its dependencies are present:

```sh
git clone --recurse-submodules https://github.com/NVDEMU/PS1WiiU.git
cd PS1WiiU
./tools/build_core.sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
python3 tools/package_release.py
```

The build produces `PS1WiiU.rpx`, an Aroma `PS1WiiU.wuhb`, and `dist/PS1WiiU-SD.zip` with the app, folder layout, README, and licensing notices. Pushing a tag such as `v0.1.0` publishes the same package to a versioned GitHub Release.

## Third-party code and legal notes

PCSX-ReARMed is GPL-2.0-or-later. See [COPYING](COPYING), [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt), and the pinned `vendor/pcsx_rearmed` submodule. Its upstream source and dependencies have their own license notices.

Only use game images and BIOS files that you are legally entitled to use. PS1WiiU does not provide BIOS files, games, or copyrighted game assets.
