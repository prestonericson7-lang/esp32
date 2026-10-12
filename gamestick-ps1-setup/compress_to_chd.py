#!/usr/bin/env python3
"""
PS1 Disc Image Compressor — bin/cue → CHD

Converts PS1 disc images from bin/cue (or iso) to CHD format.
CHD is lossless and typically saves 40-50% disk space, which
is critical when fitting games on a 64GB SD card.

Requires: chdman (part of MAME tools)
  Install: apt install mame-tools  OR  brew install rom-tools

Usage:
    python3 compress_to_chd.py /path/to/ps1/games/
    python3 compress_to_chd.py /path/to/ps1/games/ --delete-originals
    python3 compress_to_chd.py /path/to/game.cue
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def find_chdman():
    """Locate the chdman binary."""
    path = shutil.which("chdman")
    if path:
        return path

    common_paths = [
        "/usr/bin/chdman",
        "/usr/local/bin/chdman",
        "/opt/homebrew/bin/chdman",
    ]
    for p in common_paths:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p

    return None


def get_disc_images(search_path):
    """Find all convertible disc images in a directory."""
    path = Path(search_path)
    images = []

    if path.is_file():
        if path.suffix.lower() in {".cue", ".iso", ".gdi"}:
            images.append(path)
        return images

    for ext in ("*.cue", "*.iso", "*.gdi"):
        images.extend(path.rglob(ext))

    return sorted(images)


def get_bin_files_for_cue(cue_path):
    """Parse a .cue file to find all referenced .bin files."""
    bins = []
    try:
        with open(cue_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.upper().startswith("FILE"):
                    parts = line.split('"')
                    if len(parts) >= 2:
                        bin_name = parts[1]
                        bin_path = cue_path.parent / bin_name
                        bins.append(bin_path)
    except Exception:
        pass
    return bins


def convert_to_chd(source_path, output_dir=None, delete_original=False):
    """Convert a single disc image to CHD format."""
    source = Path(source_path)
    if output_dir:
        chd_path = Path(output_dir) / (source.stem + ".chd")
    else:
        chd_path = source.with_suffix(".chd")

    if chd_path.exists():
        return "skipped", f"Already exists: {chd_path.name}"

    original_size = source.stat().st_size
    if source.suffix.lower() == ".cue":
        for bin_file in get_bin_files_for_cue(source):
            if bin_file.exists():
                original_size += bin_file.stat().st_size

    cmd = ["chdman", "createcd", "-i", str(source), "-o", str(chd_path)]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=600,
        )
        if result.returncode != 0:
            chd_path.unlink(missing_ok=True)
            return "failed", result.stderr.strip()[:200]

        chd_size = chd_path.stat().st_size
        savings = (1 - chd_size / original_size) * 100 if original_size > 0 else 0

        if delete_original:
            source.unlink()
            if source.suffix.lower() == ".cue":
                for bin_file in get_bin_files_for_cue(source):
                    bin_file.unlink(missing_ok=True)

        return "ok", (
            f"{source.name} → {chd_path.name}  "
            f"({original_size/1024/1024:.1f} MB → {chd_size/1024/1024:.1f} MB, "
            f"{savings:.0f}% smaller)"
        )

    except subprocess.TimeoutExpired:
        chd_path.unlink(missing_ok=True)
        return "failed", "Conversion timed out (>10 min)"
    except FileNotFoundError:
        return "failed", "chdman not found — install mame-tools"


def main():
    parser = argparse.ArgumentParser(
        description="Convert PS1 disc images to CHD (lossless compression)"
    )
    parser.add_argument(
        "path",
        help="Directory of disc images, or a single .cue/.iso file",
    )
    parser.add_argument(
        "--output-dir",
        help="Output directory for .chd files (default: same as source)",
    )
    parser.add_argument(
        "--delete-originals",
        action="store_true",
        help="Delete original bin/cue files after successful conversion",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="List what would be converted without doing it",
    )
    args = parser.parse_args()

    chdman = find_chdman()
    if not chdman and not args.dry_run:
        print("ERROR: chdman not found.")
        print("Install it:")
        print("  Ubuntu/Debian: sudo apt install mame-tools")
        print("  macOS:         brew install rom-tools")
        print("  Windows:       Download MAME and add chdman.exe to PATH")
        sys.exit(1)

    if chdman:
        print(f"Using chdman: {chdman}")

    images = get_disc_images(args.path)
    if not images:
        print(f"No disc images found in: {args.path}")
        sys.exit(1)

    print(f"\nFound {len(images)} disc image(s) to convert:\n")

    total_before = 0
    total_after = 0
    results = {"ok": 0, "skipped": 0, "failed": 0}

    for i, img in enumerate(images, 1):
        size = img.stat().st_size
        if img.suffix.lower() == ".cue":
            for b in get_bin_files_for_cue(img):
                if b.exists():
                    size += b.stat().st_size

        if args.dry_run:
            print(f"  [{i}/{len(images)}] Would convert: {img.name} ({size/1024/1024:.1f} MB)")
            total_before += size
            total_after += int(size * 0.55)
            continue

        print(f"  [{i}/{len(images)}] Converting: {img.name}...", end=" ", flush=True)
        status, message = convert_to_chd(img, args.output_dir, args.delete_originals)
        print(f"[{status.upper()}] {message}")
        results[status] += 1

        if status == "ok":
            total_before += size

    print(f"\n{'=' * 60}")
    print(f"  Results: {results['ok']} converted, {results['skipped']} skipped, {results['failed']} failed")
    if args.dry_run and total_before > 0:
        print(f"  Estimated savings: {total_before/1024/1024:.0f} MB → ~{total_after/1024/1024:.0f} MB")
        print(f"  That frees ~{(total_before - total_after)/1024/1024:.0f} MB on your 64GB card")
    print(f"{'=' * 60}\n")


if __name__ == "__main__":
    main()
