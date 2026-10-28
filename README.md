# PS1WiiU

A native Wii U / Aroma PlayStation 1 emulator frontend using the open-source PCSX-ReARMed core.

**Development status:** the native frontend, PCSX-ReARMed static core, GamePad input adapter, audio/video output, game browser, and persistent storage features now build through GitHub Actions. A successful cross-build confirms the native binaries link; it does not substitute for performance and compatibility testing on a physical Wii U or a particular Cemu version.

## Features

- Native WUT/Aroma app bundle (`.wuhb`) and an `.rpx` binary for direct Cemu testing.
- PCSX-ReARMed's Wii U static libretro core, not a custom-written CPU emulator.
- GamePad game library and settings menu.
- GamePad buttons, D-pad, and both analog sticks mapped to a PlayStation DualShock controller.
- TV and GamePad video output, plus Wii U AX audio output.
- Settings persist in `wiiu/apps/PS1WiiU/settings.cfg`.
- Per-game memory-card files (`.srm`) are loaded on launch and periodically saved when changed.
- Manual per-game save states, loaded and saved using PCSX-ReARMed's serializer.
- Flat game folders and nested per-game folders; CUE-referenced tracks and M3U disc entries are hidden as duplicate library entries.
- GitHub Actions builds on each push and pull request, uploads a build artifact, and updates the **Nightly** release from `main`.

## Supported game/content formats

The browser supports the extensions advertised by the bundled PCSX-ReARMed core:

| Extension | What it is / notes |
|---|---|
| `.cue` | Disc cue sheet. **Preferred for a disc dumped as `.cue + .bin` tracks.** |
| `.bin` | Raw disc track. Displayed only when it is not referenced by a sibling/nearby CUE sheet, to avoid duplicate entries. |
| `.chd` | Compressed disc image; convenient for a single-file disc dump. |
| `.pbp` | PlayStation EBOOT/PBP image. |
| `.iso` | Disc image. |
| `.img` | Disc image or track file. |
| `.mdf`, `.toc`, `.cbn` | Other image/cue-table formats accepted by PCSX-ReARMed. |
| `.m3u` | Playlist for multi-disc games. Each line should point to a relative `.cue` or image path. |
| `.exe` | PlayStation executable content supported by the core; this is **not** a Windows executable. |

The core reports these content extensions: `bin|cue|img|mdf|pbp|toc|cbn|m3u|chd|iso|exe`. A particular file can still fail to boot if its dump is incomplete, encrypted, corrupt, or uses unsupported subfeatures.

## How to organize BIN/CUE files

You can drop a CUE/BIN pair directly into `games/`, or put the pair in a folder of its own. **You do not need a separate folder**, but using one keeps each game tidy. Keep the cue sheet and every track it references together in the same relative layout.

Flat layout:

```
sd:/wiiu/apps/PS1WiiU/games/
├── Example Game.cue
└── Example Game.bin
```

Organized layout (also supported; the browser searches subfolders):

```
sd:/wiiu/apps/PS1WiiU/games/
└── Example Game/
    ├── Example Game.cue
    └── Example Game.bin
```

A typical CUE includes a line like `FILE "Example Game.bin" BINARY`. The name in that line must match the actual track filename, including spaces and punctuation. Keep the `.cue` and `.bin` together; do not rename one without updating the CUE. In the browser, launch the **.cue** file, not the `.bin` track. The app suppresses tracks referenced by CUE files so they do not appear as duplicate games. A `.bin` without a referencing CUE remains selectable.

For multi-disc games, create a `.m3u` playlist with one relative disc filename per line, for example:

```
Disc 1/Game Disc 1.cue
Disc 2/Game Disc 2.cue
```

Keep the playlist entries relative to the playlist itself. The browser then presents the playlist as one game entry rather than listing every disc separately.

## Install on a real Wii U

1. Download `PS1WiiU-SD.zip` from the latest successful [GitHub Actions run](https://github.com/NVDEMU/PS1WiiU/actions) or the [Nightly Release](https://github.com/NVDEMU/PS1WiiU/releases/tag/Nightly).
2. Extract the ZIP directly to the root of the Wii U SD card (the archive contains `wiiu/` at its root).
3. Launch `wiiu/apps/PS1WiiU.wuhb` from the Wii U Menu with Aroma.
4. Copy your own game images into `sd:/wiiu/apps/PS1WiiU/games/`.
5. Optionally add a PlayStation BIOS file you legally dumped into `sd:/wiiu/apps/PS1WiiU/bios/`.

No BIOS files or commercial games are included.

## Test with Cemu

Cemu emulates an SD card using the `sdcard` directory in the Cemu root folder. PS1WiiU uses the Wii U's standard `fs:/vol/external01` SD-card path, which maps to that virtual SD card in Cemu, so **the same build and the same folder layout work for both**.

1. Close Cemu if it is running.
2. Download `PS1WiiU-SD.zip` and extract it **directly into Cemu's `sdcard/` directory**, not into a new enclosing `PS1WiiU-SD/` folder. After extraction, confirm that `sdcard/wiiu/apps/PS1WiiU.rpx` and `sdcard/wiiu/apps/PS1WiiU/games/` exist.
3. Open Cemu and use **File → Load** to select `sdcard/wiiu/apps/PS1WiiU.rpx`. The `.rpx` is for direct testing; the `.wuhb` is the Aroma bundle for a real Wii U. Cemu versions or configurations may differ in how they run homebrew, so direct RPX loading is the preferred test path.
4. Put test images in `sdcard/wiiu/apps/PS1WiiU/games/` and BIOS files in `sdcard/wiiu/apps/PS1WiiU/bios/`, then launch the RPX again.

The Cemu filesystem path is the host-side `Cemu/sdcard/` folder (or the equivalent `sdcard/` under your portable Cemu root); the app itself continues to see `fs:/vol/external01/`. The current project is cross-build-tested, but it has **not** been claimed as hardware-tested unless someone runs it on console/Cemu and reports the results.

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

In the library, use Up/Down to select a game, A to launch, Y to rescan, and X to open Settings. In Settings, use Up/Down to select an option and Left/Right or A to cycle its value.

While playing:
- **Save state:** hold `ZL + ZR + Plus` together.
- **Load state:** hold `ZL + ZR + Minus` together.
- **Return to library:** hold `L + R + Minus` for about three-quarters of a second.

Save states are stored in `sd:/wiiu/apps/PS1WiiU/states/` and are associated with the selected content filename. Use save states as a convenience, not as a replacement for in-game saves; states may not be compatible between different core versions.

## BIOS, saves, and settings

- BIOS files are searched in `sd:/wiiu/apps/PS1WiiU/bios/`; BIOS Auto/HLE can be configured in Settings.
- The first memory card is configured to use PCSX-ReARMed's libretro save RAM by default. Its data is stored as `sd:/wiiu/apps/PS1WiiU/memcards/<game>.srm` and periodically written when the card changes. Always exit to the library normally when possible to flush the latest card data.
- Save states are stored in `sd:/wiiu/apps/PS1WiiU/states/<game>.state`.
- Settings are saved to `sd:/wiiu/apps/PS1WiiU/settings.cfg`; most core-option changes apply on the next game load.

Options currently exposed include region, BIOS mode, dynamic recompiler, PS1 CPU clock, dithering, frameskip, RGB32 output, BIOS boot logo, memory-card modes, accurate GPU-list processing, and fractional frame rate. Other PCSX-ReARMed options retain their upstream core defaults.

## Build locally

Install devkitPro's Wii U toolchain and WUT, then clone recursively so the core and its dependencies are present:

```sh
git clone --recurse-submodules https://github.com/NVDEMU/PS1WiiU.git
cd PS1WiiU
sh ./tools/build_core.sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
python3 tools/package_release.py
```

The build produces `PS1WiiU.rpx`, `PS1WiiU.wuhb`, and `dist/PS1WiiU-SD.zip`. Pushing a tag such as `v0.1.0` publishes the package to a versioned GitHub Release.

## Third-party code and licensing

PCSX-ReARMed is GPL-2.0-or-later. See [COPYING](COPYING), [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt), and the pinned `vendor/pcsx_rearmed` submodule. Its dependencies retain their own license notices.

Only use game images and BIOS files that you are legally entitled to use. PS1WiiU does not provide BIOS files, games, or copyrighted game assets.
