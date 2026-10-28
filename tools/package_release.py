#!/usr/bin/env python3
"""Assemble a distributable ZIP with the app and its SD-card data folders."""
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
STAGE = DIST / "PS1WiiU-SD"
ARCHIVE = DIST / "PS1WiiU-SD.zip"

if DIST.exists():
    shutil.rmtree(DIST)
DIST.mkdir(parents=True)
(STAGE / "wiiu" / "apps" / "PS1WiiU").mkdir(parents=True)

app_dir = STAGE / "wiiu" / "apps"
for folder in ("games", "bios", "saves", "memcards", "states", "screenshots"):
    target = app_dir / "PS1WiiU" / folder
    target.mkdir(parents=True, exist_ok=True)
    (target / ".gitkeep").write_text("Folder reserved for PS1WiiU user data.\n", encoding="utf-8")

readme = ROOT / "README.md"
shutil.copy2(readme, STAGE / "README.txt")

for source_name, target_name in (
    ("COPYING", "COPYING.txt"),
    ("THIRD_PARTY_NOTICES.txt", "THIRD_PARTY_NOTICES.txt"),
):
    source = ROOT / source_name
    if source.is_file():
        shutil.copy2(source, STAGE / target_name)

built_files = {}
for name in ("PS1WiiU.wuhb", "PS1WiiU.rpx"):
    built = BUILD / name
    if not built.is_file():
        raise SystemExit(f"Missing required build output: {built}")
    data = built.read_bytes()
    if len(data) < 512 * 1024:
        raise SystemExit(f"Suspiciously small {name}: {len(data)} bytes (expected at least 512 KiB)")
    if name.endswith(".rpx") and not data.startswith(b"\\x7fELF"):
        raise SystemExit(f"Invalid RPX header in {built}")
    if name.endswith(".wuhb"):
        if not data.startswith(b"WUHB"):
            raise SystemExit(f"Invalid WUHB header in {built}")
        if b"\\x7fELF" not in data[:4096]:
            raise SystemExit(f"WUHB does not contain an embedded RPX ELF near its header: {built}")
    built_files[name] = len(data)
    shutil.copy2(built, app_dir / name)

with zipfile.ZipFile(ARCHIVE, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(STAGE.rglob("*")):
        if path.is_file():
            archive.write(path, path.relative_to(STAGE))

with zipfile.ZipFile(ARCHIVE, "r") as archive:
    names = set(archive.namelist())
    required = {
        "wiiu/apps/PS1WiiU.wuhb",
        "wiiu/apps/PS1WiiU.rpx",
        "wiiu/apps/PS1WiiU/games/.gitkeep",
        "wiiu/apps/PS1WiiU/bios/.gitkeep",
        "wiiu/apps/PS1WiiU/memcards/.gitkeep",
        "wiiu/apps/PS1WiiU/states/.gitkeep",
    }
    missing = required - names
    if missing:
        raise SystemExit(f"SD-card ZIP is incomplete; missing: {sorted(missing)}")
    if any(name.startswith("PS1WiiU-SD/") for name in names):
        raise SystemExit("ZIP has an extra PS1WiiU-SD/ wrapper; extract it to the SD root instead.")

for name, size in sorted(built_files.items()):
    print(f"Validated {name}: {size:,} bytes ({size / (1024 * 1024):.2f} MiB)")

print(f"Packaged and verified {ARCHIVE} (ZIP root contains wiiu/ for direct SD-card extraction)")
