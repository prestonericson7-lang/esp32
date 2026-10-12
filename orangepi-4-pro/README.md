# Orange Pi 4 Pro: normal desktop + 128 GB NVMe, from a 64 GB SD card

For the **Orange Pi 4 Pro** (Allwinner A733, 4 GB LPDDR5, board rev V1.3.2) with a
**128 GB NVMe SSD** in the M.2 slot and a **64 GB microSD card**.

What you end up with:

* A normal Linux desktop (XFCE: start menu, file manager, browser, terminal) that
  logs in automatically.
* The NVMe SSD showing up in the file manager as **"NVMe SSD"** (mounted at `/mnt/nvme`,
  also `~/NVMe`), permanently, every boot.
* Optional: the whole system moved onto the SSD so the board boots from it without
  the SD card (faster, and all 128 GB becomes the system drive).

Files in this folder:

| File | What it is |
|---|---|
| `setup-nvme.sh` | Run once on the Pi. Formats the blank SSD, mounts it permanently, hooks it into the desktop. Safe to re-run. |
| `move-system-to-nvme.sh` | Optional. Moves the running system onto the SSD and sets the board to boot from it. |

> **Why there is no "image file" in here:** the operating system is a 1.4 to 1.8 GB
> image published by Orange Pi on Google Drive. It has to be *flashed* onto the SD
> card with a flashing tool, not copied as a file. The SD card then contains one
> Linux partition that Windows cannot open, so my script can't be dropped onto the
> card either. Instead you run one command on the Pi after it boots (step 5). That
> is the whole extra work.

---

## What you need

* Orange Pi 4 Pro with the NVMe SSD screwed into the M.2 slot (M-key, 2280, PCIe 3.0 x1).
  A normal NVMe SSD. An M.2 **SATA** SSD will not work.
* 64 GB microSD card (anything 8 GB+, class 10 / A1 works) and a card reader.
* **USB-C power supply: 5 V, at least 3 A (5 V/4 A is better with an SSD).**
  The USB-C port does not do USB-PD. Never use a 9 V/12 V fixed-voltage supply.
  Random reboots or the SSD "disappearing" are almost always the power supply or cable.
* HDMI monitor + cable, USB keyboard and mouse, Ethernet cable (Wi-Fi works too).
* A Windows, Mac or Linux PC to flash the card.

---

## Step 1: download the OS image

Official Orange Pi downloads (Google Drive, from the
[Orange Pi 4 Pro support page](http://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/service-and-support/Orange-Pi-4-Pro.html)):

* **Ubuntu images folder:** <https://drive.google.com/drive/folders/1TcFAGTblG_gbDhwFotEWS0y78LwHA-VG>
* **Debian images folder:** <https://drive.google.com/drive/folders/1AzF-uTwA328qDFPaVBaKpiP4VjZjkmbS>
* User manual (PDF): <https://orangepi.net/wp-content/uploads/2026/01/OrangePi_4_Pro_A733_User-Manual_v1.4.pdf>

Pick **one** desktop image (all are XFCE desktops, all auto-login):

| File | What it is | Size | Notes |
|---|---|---|---|
| `Orangepi4pro_1.1.0_debian_trixie_desktop_xfce_linux6.6.98.7z` | **Recommended.** Debian 13, kernel 6.6 | 1.4 GB | Newest (Aug 2026). Firefox/Chromium install cleanly with apt. |
| `Orangepi4pro_1.1.0_ubuntu_resolute_desktop_xfce_linux6.6.98.7z` | Ubuntu 26.04, kernel 6.6 | 1.8 GB | Newest Ubuntu (Aug 2026). Same steps. |
| `Orangepi4pro_1.0.6_ubuntu_jammy_desktop_xfce_linux5.15.147.7z` | Ubuntu 22.04, kernel 5.15 | 1.1 GB | The one the manual was written against. Fallback if a 1.1.0 image gives you trouble. |
| `Orangepi4pro_1.0.6_debian_bullseye_desktop_xfce_linux5.15.147.7z` | Debian 11, kernel 5.15 | 1.0 GB | Only image with GPU/video acceleration per the manual, but Debian 11 is end-of-life. |

Do **not** download the `_server_` files (no desktop) or the Android ones.

## Step 2: unpack the `.7z`

The download is a `.7z` archive. balenaEtcher and Raspberry Pi Imager can't open `.7z`,
so unpack it first:

* **Windows:** install [7-Zip](https://www.7-zip.org/), right-click the file, 7-Zip, "Extract Here".
* **Mac:** `brew install p7zip` then `7z x <file>.7z`, or use Keka.
* **Linux:** `sudo apt install p7zip-full && 7z x <file>.7z`

You get a `.img` file (around 4 GB) and a `.sha` checksum file. That `.img` is what you flash.

## Step 3: flash the SD card

1. Install [balenaEtcher](https://www.balena.io/etcher/) (Windows/Mac/Linux). If it refuses to
   start on Windows, right-click it and "Run as administrator".
2. Put the SD card in the reader. **Everything on the card will be erased.**
3. Etcher: **Flash from file** -> pick the `.img` -> **Select target** -> your SD card
   (check the size, 64 GB, so you don't wipe the wrong drive) -> **Flash!**
4. Wait for "Flash Complete!" (it verifies afterwards, let it finish), then remove the card.

Windows may say the card "needs to be formatted" afterwards. **Click Cancel.** That is
just Windows not understanding the Linux partition.

Alternatives that also work: Raspberry Pi Imager ("Use custom"), Win32DiskImager, Rufus
("DD image" mode). One forum user whose board would not boot from an Etcher-written card
had it work with Rufus.

## Step 4: first boot

1. SSD in the M.2 slot, SD card in the slot, HDMI, keyboard, mouse, Ethernet.
2. Plug in power **last**. Red LED = power, green LED blinking = Linux is running.
3. First boot takes 2 to 3 minutes and may reboot once: it grows the system partition to
   fill the 64 GB card by itself. Don't pull the power during this.
4. You land on the XFCE desktop, logged in as **`orangepi`** automatically.

Logins, if ever asked:

| User | Password |
|---|---|
| `orangepi` | `orangepi` |
| `root` | `orangepi` |

Wi-Fi: click the network icon top-right, pick your network. Change the password
later with `passwd` in a terminal if you care.

## Step 5: make the NVMe SSD work (one command)

Open a terminal on the Pi (menu -> Terminal, or `Ctrl+Alt+T`) and paste:

```bash
curl -fsSL https://raw.githubusercontent.com/prestonericson7-lang/esp32/claude/orange-pi-desktop-nvme-boot-z1h4n4/orangepi-4-pro/setup-nvme.sh -o setup-nvme.sh && sudo bash setup-nvme.sh
```

(Once this branch is merged into `main`, replace `claude/orange-pi-desktop-nvme-boot-z1h4n4`
in the URL with `main`. No internet on the Pi? Copy `setup-nvme.sh` onto a USB stick and
run `sudo bash /media/orangepi/<stick>/setup-nvme.sh`.)

It will:

1. Find the SSD (`/dev/nvme0n1`). If it is blank, make one big ext4 partition. If it already
   has data, keep it (use `--wipe` to erase).
2. Mount it at `/mnt/nvme` now and on every boot (`/etc/fstab`, by UUID, `nofail`, so an
   unplugged SSD can never stop the board from booting).
3. Give the drive to your user, add `~/NVMe` and an **"NVMe SSD"** bookmark in the file manager.

Useful options:

```bash
sudo bash setup-nvme.sh --move-home-folders   # Downloads/Documents/Pictures/Videos/Music live on the SSD
sudo bash setup-nvme.sh --wipe                # erase the SSD first (asks before doing it)
sudo bash setup-nvme.sh --mountpoint /data    # mount somewhere else
```

Check it worked: `df -h /mnt/nvme` should show a drive of about 115 to 120G.

**Done.** That is the "normal desktop with the NVMe mounted and working" setup.

---

## Step 6 (optional): boot the whole system from the SSD

The SD card is slow-ish. The board has a 16 MB SPI flash chip (the small `XMC 25QU128C`
next to the HDMI port) that can hold the bootloader, so the complete system can run from
the NVMe with **no SD card**. This is section 3.36 of the manual, automated:

```bash
curl -fsSL https://raw.githubusercontent.com/prestonericson7-lang/esp32/claude/orange-pi-desktop-nvme-boot-z1h4n4/orangepi-4-pro/move-system-to-nvme.sh -o move-system-to-nvme.sh && sudo bash move-system-to-nvme.sh
```

* **It erases the SSD** (including anything you put there in step 5; do this step first
  if you want the system on the SSD, or copy your files off first).
* Type `ERASE` to confirm. The script partitions the SSD the way the manual does and opens
  Orange Pi's installer (`nand-sata-install`). In it choose:
  **"Boot from SPI - system on SATA, USB or NVMe"** -> **Yes** -> filesystem **ext4** ->
  wait 5 to 15 min -> "write bootloader to SPI flash?" **Yes** -> **Power off**.
* Unplug power, **take the SD card out**, plug power back in. It now boots from the SSD and
  the full 128 GB is your system drive. Keep the SD card as a rescue: the SD slot always has
  boot priority, so if the SSD ever fails to boot, put the card back in.
* Orange Pi says they only tested this with Fanxiang and Guangcun SSDs and that some
  Kingston drives had boot trouble (partly fixed in image 1.0.6). If your SSD won't boot
  this way, stay with step 5 (SD boots, SSD is the data drive). That setup is just as usable.
* On the Debian 11 (Bullseye) image the GPU driver has to be unloaded first; the script
  handles it but has to be run from a text console (`Ctrl+Alt+F3`, log in, run it) instead
  of a desktop terminal.

Run it with `--dry-run` first if you want to see what it would do without changing anything.

---

## If something goes wrong

**SSD not found** (`setup-nvme.sh` prints diagnostics): power off, re-seat the SSD, make sure
it is screwed down, use a 5 V/4 A supply, try one more reboot. Check with `lsblk` and `lspci`.
It must be an NVMe (PCIe) drive, not M.2 SATA.

**Board keeps rebooting / screen goes black under load:** power supply or cable. Use a
proper 5 V/3 A+ supply and a short, thick USB-C cable.

**Does not boot at all (no green LED blinking):** re-flash with Rufus or Etcher as
administrator, verify the download (`sha256sum -c *.sha` on Linux/Mac, or `certutil
-hashfile <file>.img SHA256` on Windows and compare with the `.sha` file), try another
SD card.

**No browser installed:** Debian image: `sudo apt update && sudo apt install -y firefox-esr`.
Ubuntu image: `sudo snap install firefox` or `sudo apt install -y chromium`.

**Everything feels slow / videos stutter:** on these images only the Debian 11 build has
the GPU driver; the others draw the desktop with the CPU. XFCE is fine, 1080p YouTube is not
great. That is the board's software state right now, not something the SD card or SSD fixes.

**Want it back to a plain data drive after step 6:** boot from the SD card again and run
`setup-nvme.sh --wipe`.

## Sources

* Orange Pi 4 Pro user manual v1.4 (sections 2.3, 2.9, 3.1, 3.2, 3.4, 3.6, 3.36, 7.2):
  <https://orangepi.net/wp-content/uploads/2026/01/OrangePi_4_Pro_A733_User-Manual_v1.4.pdf>
* Official download page and Google Drive folders linked above (image list read on 2026-10-11).
* Armbian community support PR for the board (NVMe boot via SPI confirmed):
  <https://github.com/armbian/build/pull/9967>
