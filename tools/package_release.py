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
for folder in ("games", "bios", "saves", "memcards", "screenshots"):
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

for name in ("PS1WiiU.wuhb", "PS1WiiU.rpx"):
    built = BUILD / name
    if built.is_file():
        shutil.copy2(built, app_dir / name)

if not (app_dir / "PS1WiiU.wuhb").exists() and not (app_dir / "PS1WiiU.rpx").exists():
    raise SystemExit("No PS1WiiU.wuhb or PS1WiiU.rpx found in build/")

with zipfile.ZipFile(ARCHIVE, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(STAGE.rglob("*")):
        if path.is_file():
            archive.write(path, path.relative_to(DIST))

print(f"Packaged {ARCHIVE}")
