# M8 Game Stick Lite 4K (MG99) — PS1 Setup Guide

## Your Hardware

| Component | Detail |
|-----------|--------|
| CPU | MG99 (Rockchip RK3228), Quad-core ARM Cortex-A7 @ 1.3GHz |
| RAM | 256MB DDR3 (2x Hynix H5TQ1G63BFR) |
| Output | HDMI (up to 4K upscaled) |
| Storage | Micro SD card (your 64GB card) |
| Controllers | USB 2.4GHz wireless |

## What Can Run

| System | Works? | Notes |
|--------|--------|-------|
| PS1 | Yes | Most 2D games run well, 3D games vary |
| NES / SNES / GBA / Genesis | Yes | Full speed |
| N64 | Partial | Simple games only |
| Arcade (MAME/FBA) | Yes | CPS1/CPS2/Neo Geo run great |
| PS2 | No | Hardware is too weak, not possible |
| PSP | Barely | A few 2D games might work |

## SD Card Folder Structure

Your 64GB SD card should look like this:

```
SD_CARD_ROOT/
├── bootlogo.bmp              ← Boot splash (270x45 BMP)
├── magic.bin                  ← Device auth (DO NOT DELETE)
├── bios/
│   ├── scph5501.bin           ← PS1 BIOS (US) — YOU supply this
│   ├── scph5502.bin           ← PS1 BIOS (EU) — optional
│   └── scph5500.bin           ← PS1 BIOS (JP) — optional
├── roms/
│   ├── ps1/                   ← Your PS1 games go here
│   │   ├── Spyro the Dragon.chd
│   │   ├── Crash Bandicoot.chd
│   │   └── ...
│   ├── nes/
│   ├── snes/
│   ├── gba/
│   ├── sega/
│   ├── mame/
│   └── n64/
├── download/                  ← Alternate game folder (some firmware versions)
└── res/
    └── games.db               ← Game menu database
```

## Step-by-Step Setup

### 1. Back Up Your SD Card First

Before changing anything, image your existing SD card:

**Windows:** Use Win32DiskImager → Read → save as `original_backup.img`
**Mac/Linux:**
```bash
sudo dd if=/dev/sdX of=original_backup.img bs=4M status=progress
```

### 2. Create the Folder Structure

Plug the SD card into your PC and run:
```bash
python3 setup_sd_card.py /path/to/sdcard
```

Or manually create these folders:
- `bios/`
- `roms/ps1/`

### 3. Add PS1 BIOS

You need at minimum ONE of these BIOS files (rip from your own PS1):
- `scph5501.bin` — US region (most common)
- `scph5502.bin` — European region
- `scph5500.bin` — Japanese region

Place it in the `bios/` folder on the SD card.

### 4. Add Your PS1 Games

Copy your ripped PS1 disc images to `roms/ps1/`.

**Supported formats:**
| Format | Extension | Size per game | Notes |
|--------|-----------|---------------|-------|
| BIN/CUE | .bin + .cue | 500-700 MB | Raw disc image, launch the .cue file |
| CHD | .chd | 250-400 MB | Compressed, recommended for 64GB |
| PBP | .pbp | 300-500 MB | PSP eboot format, compressed |
| ISO | .iso | 500-700 MB | Single track only |

**Recommended format: CHD** — lossless compression saves ~45% space.

To convert your bin/cue files to CHD:
```bash
# Install chdman first
sudo apt install mame-tools    # Linux
brew install rom-tools          # macOS

# Convert all games in a folder
python3 compress_to_chd.py /path/to/ps1/games/

# Convert a single game
python3 compress_to_chd.py /path/to/game.cue
```

### 5. How Many Games Fit on 64GB?

| Format | Avg size | Games on 64GB |
|--------|----------|---------------|
| BIN/CUE (raw) | ~600 MB | ~100 games |
| CHD (compressed) | ~330 MB | ~180 games |
| PBP (compressed) | ~400 MB | ~150 games |

With CHD compression, you can fit roughly 150-180 PS1 games on your 64GB card alongside other system ROMs.

### 6. Eject and Test

1. Safely eject the SD card
2. Insert into the M8 game stick
3. Power on — bootlogo.bmp displays
4. PS1 games should appear in the PlayStation section of the menu
5. If games don't appear, check that the folder name matches what your firmware expects (`ps1`, `psx`, or `PS`)

## PS1 Performance Tips

The RK3228 handles PS1 reasonably but not perfectly. Some tips:

1. **2D games run great** — RPGs, fighters, puzzle games are all smooth
2. **3D games vary** — simple 3D (Spyro, Crash) usually works, heavy 3D (Gran Turismo 2) may lag
3. **Use CHD format** — faster loading from SD compared to raw bin/cue
4. **Multi-disc games** — keep all discs in the same folder, the emulator handles disc swapping

## Games Known to Work Well on RK3228

These titles are confirmed to run at playable speeds on similar hardware:

- Spyro the Dragon / Ripto's Rage / Year of the Dragon
- Crash Bandicoot 1 / 2 / 3 / CTR
- Tekken 3
- Final Fantasy VII / VIII / IX
- Metal Gear Solid
- Castlevania: Symphony of the Night
- Resident Evil 1 / 2 / 3
- Silent Hill
- Tony Hawk's Pro Skater 1 / 2
- MediEvil
- Oddworld: Abe's Oddysee / Exoddus
- Mega Man X4 / X5 / X6
- Rayman
- PaRappa the Rapper
- Twisted Metal 2
- Wipeout / Wipeout XL
- Ridge Racer Type 4
- Tomb Raider 1 / 2 / 3
- Legend of Dragoon
- Vagrant Story
- Chrono Cross
- Xenogears
- Suikoden 1 / 2
- Breath of Fire III / IV
- Wild Arms
- Arc the Lad Collection
- Parasite Eve 1 / 2
- Dino Crisis 1 / 2
- Street Fighter Alpha 3
- Marvel vs Capcom
- Armored Core
- Front Mission 3
- Valkyrie Profile
- Star Ocean: The Second Story
- Lunar: Silver Star Story
- Grandia

## Troubleshooting

**Games don't appear in menu:**
- Check folder name — try `ps1`, `psx`, `PS`, and `PS1`
- Some firmware requires games in `download/` instead of `roms/`
- The games.db database may need updating (run `setup_sd_card.py --update-db`)

**Games won't launch:**
- Missing BIOS — check `bios/` folder
- Wrong format — ensure .cue files reference the correct .bin filename
- Corrupted dump — re-rip the disc

**Games run slow:**
- Normal for heavy 3D titles on this hardware
- Try the PBP format (sometimes loads faster)
- Some games have frameskip settings accessible through the emulator menu

**No audio:**
- Check HDMI connection
- Some emulator settings need audio enabled manually
- Try a different HDMI cable or TV input

## About magic.bin

The `magic.bin` file is your device's authentication token. The firmware
checks it on boot. **Never delete or modify it** — without it, the device
won't boot. If you're making a fresh SD card from a firmware image, this
file comes with the image.

## Alternative: GStickOS (Custom Firmware)

If the stock firmware is too limiting, there's a community firmware called
GStickOS based on RetroArch:
- ~50 Libretro emulator cores included
- Full RetroArch UI with settings access
- SSH access to the device
- USB and SD card ROM loading

Check the project on GitHub (lucamot/GStickOS) for compatibility with
your specific hardware revision.
