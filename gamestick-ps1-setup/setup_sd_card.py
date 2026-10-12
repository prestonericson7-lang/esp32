#!/usr/bin/env python3
"""
M8 Game Stick Lite 4K (MG99/RK3228) — SD Card PS1 Setup Tool

Prepares a 64GB SD card for PS1 emulation on the M8 Game Stick.
Creates folder structure, validates game files, generates the
games database entries, and compresses disc images to save space.

Usage:
    python3 setup_sd_card.py /path/to/sdcard [--games-dir /path/to/ps1/isos]
    python3 setup_sd_card.py /path/to/sdcard --scan
    python3 setup_sd_card.py /path/to/sdcard --compress
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sqlite3
import struct
import subprocess
import sys
from pathlib import Path


SYSTEM_FOLDERS = {
    "ps1":   {"name": "PlayStation",  "extensions": {".bin", ".cue", ".pbp", ".chd", ".iso", ".img", ".mdf", ".ecm"}},
    "gba":   {"name": "Game Boy Advance", "extensions": {".gba", ".zip"}},
    "gbc":   {"name": "Game Boy Color",   "extensions": {".gbc", ".zip"}},
    "gb":    {"name": "Game Boy",          "extensions": {".gb", ".zip"}},
    "nes":   {"name": "NES",               "extensions": {".nes", ".zip"}},
    "snes":  {"name": "SNES",              "extensions": {".sfc", ".smc", ".zip"}},
    "sega":  {"name": "Sega Genesis",      "extensions": {".md", ".smd", ".gen", ".bin", ".zip"}},
    "mame":  {"name": "Arcade",            "extensions": {".zip"}},
    "n64":   {"name": "Nintendo 64",       "extensions": {".n64", ".z64", ".v64", ".zip"}},
    "psp":   {"name": "PSP",               "extensions": {".iso", ".cso", ".pbp"}},
    "pce":   {"name": "PC Engine",         "extensions": {".pce", ".zip"}},
    "fba":   {"name": "Final Burn Alpha",  "extensions": {".zip"}},
}

PS1_BIOS_HASHES = {
    "scph5500.bin": "8dd7d5296a650fac7319bce665a6a53c",
    "scph5501.bin": "490f666e1afb15b7c0571ce25db9a141",
    "scph5502.bin": "32736f17079d0b2b7024407c39bd3050",
    "scph1001.bin": "924e392ed05558ffdb115408c263dccf",
}


def create_folder_structure(sd_root):
    """Create the standard M8 game stick folder layout."""
    created = []
    for folder in SYSTEM_FOLDERS:
        rom_path = sd_root / "roms" / folder
        rom_path.mkdir(parents=True, exist_ok=True)
        created.append(str(rom_path))

    download_path = sd_root / "download"
    download_path.mkdir(exist_ok=True)

    bios_path = sd_root / "bios"
    bios_path.mkdir(exist_ok=True)

    res_path = sd_root / "res"
    res_path.mkdir(exist_ok=True)

    return created


def validate_bios(sd_root):
    """Check for PS1 BIOS files and validate their hashes."""
    bios_dir = sd_root / "bios"
    results = {}
    found_any = False

    for bios_name, expected_md5 in PS1_BIOS_HASHES.items():
        bios_path = bios_dir / bios_name
        if bios_path.exists():
            with open(bios_path, "rb") as f:
                actual_md5 = hashlib.md5(f.read()).hexdigest()
            if actual_md5 == expected_md5:
                results[bios_name] = "OK"
                found_any = True
            else:
                results[bios_name] = f"BAD HASH (got {actual_md5})"
        else:
            results[bios_name] = "MISSING"

    return results, found_any


def scan_ps1_games(games_dir):
    """Scan a directory for PS1 game files and return metadata."""
    games = []
    games_path = Path(games_dir)

    if not games_path.exists():
        return games

    cue_files = {}
    bin_files = {}
    standalone = []

    for f in sorted(games_path.rglob("*")):
        if not f.is_file():
            continue
        ext = f.suffix.lower()
        if ext == ".cue":
            cue_files[f.stem.lower()] = f
        elif ext == ".bin" and f.stem.lower() in cue_files:
            bin_files[f.stem.lower()] = f
        elif ext in {".pbp", ".chd", ".iso", ".img"}:
            standalone.append(f)
        elif ext == ".bin":
            bin_files[f.stem.lower()] = f

    for stem, cue_path in cue_files.items():
        size = cue_path.stat().st_size
        bin_path = bin_files.get(stem)
        if bin_path:
            size += bin_path.stat().st_size
        clean_name = clean_game_name(cue_path.stem)
        games.append({
            "name": clean_name,
            "file": str(cue_path),
            "launch": cue_path.name,
            "size_mb": round(size / 1024 / 1024, 1),
            "format": "bin/cue",
            "compressible": True,
        })

    for f in standalone:
        clean_name = clean_game_name(f.stem)
        games.append({
            "name": clean_name,
            "file": str(f),
            "launch": f.name,
            "size_mb": round(f.stat().st_size / 1024 / 1024, 1),
            "format": f.suffix.lower().lstrip("."),
            "compressible": f.suffix.lower() not in {".chd", ".pbp"},
        })

    for stem, bin_path in bin_files.items():
        if stem not in cue_files:
            clean_name = clean_game_name(bin_path.stem)
            games.append({
                "name": clean_name,
                "file": str(bin_path),
                "launch": bin_path.name,
                "size_mb": round(bin_path.stat().st_size / 1024 / 1024, 1),
                "format": "bin (no cue)",
                "compressible": True,
            })

    return games


def clean_game_name(filename):
    """Turn a ROM filename into a display name."""
    name = filename
    name = re.sub(r"\(.*?\)", "", name)
    name = re.sub(r"\[.*?\]", "", name)
    name = re.sub(r"_", " ", name)
    name = re.sub(r"\s+", " ", name).strip()
    name = re.sub(r"\s*-\s*Disc\s*\d+", "", name, flags=re.IGNORECASE)
    return name


def copy_games_to_sd(games, sd_root):
    """Copy PS1 game files to the SD card's roms/ps1 folder."""
    dest = sd_root / "roms" / "ps1"
    dest.mkdir(parents=True, exist_ok=True)

    copied = []
    skipped = []

    for game in games:
        src = Path(game["file"])
        dst = dest / src.name

        if dst.exists():
            skipped.append(game["name"])
            continue

        if game["format"] == "bin/cue":
            shutil.copy2(src, dst)
            bin_src = src.with_suffix(".bin")
            if bin_src.exists():
                shutil.copy2(bin_src, dest / bin_src.name)
            multi_bins = list(src.parent.glob(f"{src.stem}*.bin"))
            for mb in multi_bins:
                mb_dst = dest / mb.name
                if not mb_dst.exists():
                    shutil.copy2(mb, mb_dst)
        else:
            shutil.copy2(src, dst)

        copied.append(game["name"])

    return copied, skipped


def generate_cue_for_bin(bin_path):
    """Generate a .cue file for a standalone .bin that lacks one."""
    bin_file = Path(bin_path)
    cue_file = bin_file.with_suffix(".cue")

    if cue_file.exists():
        return None

    cue_content = (
        f'FILE "{bin_file.name}" BINARY\n'
        f"  TRACK 01 MODE2/2352\n"
        f"    INDEX 01 00:00:00\n"
    )
    cue_file.write_text(cue_content)
    return str(cue_file)


def estimate_chd_savings(games):
    """Estimate how much space CHD compression would save."""
    compressible_size = sum(g["size_mb"] for g in games if g["compressible"])
    estimated_chd = compressible_size * 0.55
    return compressible_size, estimated_chd


def update_games_db(sd_root, games):
    """Update/create the games.db SQLite database for the menu system."""
    db_path = sd_root / "res" / "games.db"

    if db_path.exists():
        conn = sqlite3.connect(str(db_path))
        cursor = conn.cursor()
        try:
            cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
            tables = [row[0] for row in cursor.fetchall()]
            print(f"  Existing database tables: {', '.join(tables)}")
        except sqlite3.Error as e:
            print(f"  Warning: could not read existing database: {e}")
            conn.close()
            return
    else:
        conn = sqlite3.connect(str(db_path))
        cursor = conn.cursor()
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS games (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                path TEXT NOT NULL,
                system TEXT DEFAULT 'ps1',
                image TEXT DEFAULT '',
                pinyin TEXT DEFAULT '',
                visible INTEGER DEFAULT 1
            )
        """)

    for game in games:
        launch_file = game["launch"]
        game_path = f"roms/ps1/{launch_file}"
        cursor.execute("SELECT id FROM games WHERE path = ?", (game_path,))
        if not cursor.fetchone():
            pinyin = game["name"].lower().replace(" ", "")
            cursor.execute(
                "INSERT INTO games (name, path, system, pinyin, visible) VALUES (?, ?, 'ps1', ?, 1)",
                (game["name"], game_path, pinyin),
            )

    conn.commit()
    conn.close()


def print_report(games, bios_results, sd_root):
    """Print a summary report of the setup."""
    total_size = sum(g["size_mb"] for g in games)
    comp_size, est_chd = estimate_chd_savings(games)

    print("\n" + "=" * 60)
    print("  M8 GAME STICK — PS1 SETUP REPORT")
    print("=" * 60)

    print(f"\n  SD Card Root:    {sd_root}")
    print(f"  PS1 Games Found: {len(games)}")
    print(f"  Total Size:      {total_size:.1f} MB ({total_size/1024:.1f} GB)")

    if comp_size > 0:
        print(f"\n  Compression Potential (bin/cue → CHD):")
        print(f"    Before: {comp_size:.1f} MB")
        print(f"    After:  ~{est_chd:.1f} MB (estimated)")
        print(f"    Saved:  ~{comp_size - est_chd:.1f} MB")

    print(f"\n  BIOS Status:")
    for name, status in bios_results.items():
        indicator = "[OK]" if status == "OK" else "[!!]"
        print(f"    {indicator} {name}: {status}")

    need_bios = not any(s == "OK" for s in bios_results.values())
    if need_bios:
        print("\n  WARNING: No valid PS1 BIOS found!")
        print("  You MUST place one of these in the bios/ folder:")
        print("    scph5501.bin (US)  scph5502.bin (EU)  scph5500.bin (JP)")
        print("  The BIOS is required — PS1 games will NOT run without it.")

    print(f"\n  Game List:")
    for i, game in enumerate(sorted(games, key=lambda g: g["name"]), 1):
        fmt = game["format"].upper()
        print(f"    {i:3d}. {game['name'][:45]:<45s} {game['size_mb']:>7.1f} MB  [{fmt}]")

    print("\n" + "=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description="M8 Game Stick (MG99) PS1 SD Card Setup Tool"
    )
    parser.add_argument(
        "sd_root",
        help="Path to the SD card mount point (the root of the SD card)",
    )
    parser.add_argument(
        "--games-dir",
        help="Directory containing your PS1 game files to copy",
    )
    parser.add_argument(
        "--scan",
        action="store_true",
        help="Scan existing roms/ps1 folder and report what's there",
    )
    parser.add_argument(
        "--fix-cue",
        action="store_true",
        help="Generate missing .cue files for standalone .bin files",
    )
    parser.add_argument(
        "--update-db",
        action="store_true",
        help="Update the games.db database with found games",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be done without making changes",
    )
    args = parser.parse_args()

    sd_root = Path(args.sd_root).resolve()
    if not sd_root.exists():
        print(f"Error: SD card path does not exist: {sd_root}")
        sys.exit(1)

    print(f"\nM8 Game Stick PS1 Setup — Target: {sd_root}\n")

    print("[1/5] Creating folder structure...")
    if not args.dry_run:
        folders = create_folder_structure(sd_root)
        print(f"  Created {len(folders)} system folders")
    else:
        print("  (dry run — skipped)")

    print("\n[2/5] Checking PS1 BIOS files...")
    bios_results, has_bios = validate_bios(sd_root)
    for name, status in bios_results.items():
        print(f"  {name}: {status}")

    games = []
    if args.games_dir:
        print(f"\n[3/5] Scanning source games directory: {args.games_dir}")
        games = scan_ps1_games(args.games_dir)
        print(f"  Found {len(games)} PS1 games")

        if games and not args.dry_run:
            print("\n[4/5] Copying games to SD card...")
            copied, skipped = copy_games_to_sd(games, sd_root)
            print(f"  Copied: {len(copied)}")
            print(f"  Skipped (already exist): {len(skipped)}")
        else:
            print("\n[4/5] Copying games... (skipped — dry run or no games)")
    elif args.scan:
        print(f"\n[3/5] Scanning existing PS1 games on SD card...")
        games = scan_ps1_games(sd_root / "roms" / "ps1")
        print(f"  Found {len(games)} PS1 games")
        print("\n[4/5] Copy step skipped (scan mode)")
    else:
        print("\n[3/5] No --games-dir or --scan specified, skipping game scan")
        print("[4/5] Skipping copy")

    if args.fix_cue:
        ps1_dir = sd_root / "roms" / "ps1"
        if ps1_dir.exists():
            for f in ps1_dir.glob("*.bin"):
                cue = generate_cue_for_bin(f)
                if cue:
                    print(f"  Generated: {cue}")

    if args.update_db and games:
        print("\n[5/5] Updating games database...")
        if not args.dry_run:
            update_games_db(sd_root, games)
            print("  Database updated")
        else:
            print("  (dry run — skipped)")
    else:
        print("\n[5/5] Database update skipped")

    print_report(games, bios_results, sd_root)

    print("\nNEXT STEPS:")
    print("  1. Place your PS1 BIOS file (scph5501.bin) in the bios/ folder")
    print("  2. Place your PS1 game files (.bin/.cue or .chd) in roms/ps1/")
    print("  3. Eject the SD card safely and insert it into the game stick")
    print("  4. PS1 games should appear in the PlayStation menu")
    print()


if __name__ == "__main__":
    main()
