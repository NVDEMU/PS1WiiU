# PS1WiiU

A native Wii U / Aroma PlayStation 1 emulator project.

> **Early development:** the current build is an application-shell milestone, not a playable emulator yet. The PS1 emulation core and game loading are not integrated. The first goal is to keep the native Wii U build reproducible, then port/adapt a compatible open-source PS1 core.

## Builds

GitHub Actions builds the project on every push and pull request. Each successful build uploads a ZIP containing the current app binary and the SD-card folder layout. Pushing a version tag such as `v0.1.0` creates a GitHub Release with the same ZIP attached.

## SD-card layout

Extract the release ZIP to the root of your SD card. The package is arranged like this:

```
sd:/
└── wiiu/
    └── apps/
        ├── PS1WiiU.wuhb
        └── PS1WiiU/
            ├── games/       # Your own PS1 disc images (.cue/.bin, .chd, .iso)
            ├── bios/        # Optional BIOS files you legally dump yourself
            ├── saves/       # Emulator save data (reserved)
            ├── memcards/    # Virtual memory cards (reserved)
            └── screenshots/ # Screenshots (reserved)
```

The current app does not read or run games yet; these directories are included now so release packages have a stable layout as game loading is implemented.

## BIOS and games

No commercial games or PlayStation BIOS files are distributed with this project. Use only game images and BIOS files that you are legally entitled to use. BIOS support and exact filenames will be documented once the emulator core is selected.

## Build locally

Use the devkitPro Wii U toolchain with WUT installed:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
``

The build should produce `PS1WiiU.rpx` and, when WUT's WUHB helper is available, `PS1WiiU.wuhb`.
