#!/usr/bin/env python3
"""Download Melody Matrix third-party dependencies.

Usage:
    python scripts/download_deps.py [--all] [--sdl2] [--glm] [--imgui] [--miniaudio] [--glad] [--catch2]

Downloads to: third_party/<lib>/
"""

import argparse
import os
import subprocess
import sys
import urllib.request
import zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
THIRD_PARTY = os.path.join(PROJECT_DIR, "third_party")

# ── Dependency definitions ──
DEPS = {
    "sdl2": {
        "url": "https://www.libsdl.org/release/SDL2-devel-2.30.8-VC.zip",
        "type": "zip",
        "extract_dir": "SDL2-2.30.8",
        "target_dir": "SDL2",
    },
    "glm": {
        "url": "https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip",
        "type": "zip",
        "extract_dir": "glm-1.0.1",
        "target_dir": "glm",
    },
    "imgui": {
        "url": "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5-docking.zip",
        "type": "zip",
        "extract_dir": "imgui-1.91.5-docking",
        "target_dir": "imgui",
    },
    "miniaudio": {
        "url": "https://github.com/mackron/miniaudio/archive/refs/tags/0.11.21.zip",
        "type": "zip",
        "extract_dir": "miniaudio-0.11.21",
        "target_dir": "miniaudio",
    },
    "glad": {
        "url": "https://github.com/Dav1dde/glad/archive/refs/tags/v2.0.6.zip",
        "type": "zip",
        "extract_dir": "glad-2.0.6",
        "target_dir": "glad",
    },
    "catch2": {
        "url": "https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.zip",
        "type": "zip",
        "extract_dir": "Catch2-3.7.1",
        "target_dir": "Catch2",
    },
}


def download_file(url: str, dest: str) -> bool:
    print(f"  Downloading: {url}")
    try:
        urllib.request.urlretrieve(url, dest)
        return True
    except Exception as e:
        print(f"  FAILED: {e}")
        return False


def extract_zip(archive: str, extract_to: str, strip_dir: str = None, rename_to: str = None):
    print(f"  Extracting: {archive}")
    with zipfile.ZipFile(archive, "r") as z:
        z.extractall(extract_to)
    if strip_dir and rename_to:
        src = os.path.join(extract_to, strip_dir)
        dst = os.path.join(extract_to, rename_to)
        if os.path.exists(dst):
            import shutil
            shutil.rmtree(dst)
        os.rename(src, dst)


def install_dep(name: str):
    dep = DEPS[name]
    target_dir = os.path.join(THIRD_PARTY, dep["target_dir"])

    if os.path.exists(target_dir):
        print(f"[{name}] Already exists at {target_dir}, skipping.")
        return True

    print(f"[{name}] Installing to {target_dir}...")
    archive = os.path.join(THIRD_PARTY, f"{name}.zip")

    if not download_file(dep["url"], archive):
        return False

    extract_zip(archive, THIRD_PARTY, dep["extract_dir"], dep["target_dir"])

    # Clean up archive
    if os.path.exists(archive):
        os.remove(archive)

    print(f"[{name}] Done!")
    return True


def main():
    os.makedirs(THIRD_PARTY, exist_ok=True)

    parser = argparse.ArgumentParser(description="Download Melody Matrix dependencies")
    parser.add_argument("--all", action="store_true", help="Download all dependencies")
    for name in DEPS:
        parser.add_argument(f"--{name}", action="store_true", help=f"Download {name}")
    args = parser.parse_args()

    if args.all:
        targets = list(DEPS.keys())
    else:
        targets = [name for name in DEPS if getattr(args, name, False)]

    if not targets:
        parser.print_help()
        print("\nNo targets specified. Use --all to download everything.")
        return 1

    success = True
    for name in targets:
        if not install_dep(name):
            success = False

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
