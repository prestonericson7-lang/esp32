# Orange Pi 4 Pro: one file -> desktop with the 128 GB NVMe working

For the **Orange Pi 4 Pro** (Allwinner A733, 4 GB LPDDR5, board V1.3.2) with a
**128 GB NVMe SSD** in the M.2 slot and a **64 GB microSD card**.

## The one file

**`image/OrangePi4Pro-DietPi-Xfce-desktop-NVMe-autosetup.img.xz`** (142 MB)

Download it from this folder on GitHub (click the file, then the **Download** button;
it is stored with Git LFS so the "Raw" view shows a tiny pointer, use Download).
Direct link:

<https://github.com/prestonericson7-lang/esp32/raw/claude/orange-pi-desktop-nvme-boot-z1h4n4/orangepi-4-pro/image/OrangePi4Pro-DietPi-Xfce-desktop-NVMe-autosetup.img.xz>

Then:

1. **Flash it** to the SD card with [balenaEtcher](https://www.balena.io/etcher/)
   (Flash from file -> pick the `.img.xz`, no unpacking needed -> Select target -> your
   64 GB card -> Flash). Raspberry Pi Imager ("Use custom") or Rufus work too.
   Windows will say the card "needs to be formatted" afterwards: click **Cancel**.
2. SSD in the M.2 slot, SD card in, **Ethernet cable plugged in**, HDMI, keyboard, mouse,
   then power (5 V / 3 A or better USB-C, no 9 V/12 V bricks).
3. **Walk away for 20 to 40 minutes.** The first boot is fully automatic: it grows the
   system onto the card, downloads and installs the Xfce desktop and Firefox, formats the
   blank SSD, mounts it, and reboots into the desktop. You will see text scrolling on the
   HDMI screen the whole time. Don't pull the power.
4. It boots straight to the desktop, logged in. The SSD is at `/mnt/nvme`, also `~/NVMe`,
   and shows as **"NVMe SSD"** in the file manager sidebar. Every boot, no clicks.

Passwords, if anything ever asks: user **`dietpi`** / **`orangepi`**, root **`orangepi`**.

Needs the Ethernet cable for that first boot only (it has to download the desktop).
After that Wi-Fi can be set up from the desktop network icon.

**What it is:** the official DietPi image for the Orange Pi 4 Pro (Debian 13, the vendor
6.6.98 kernel with the Orange Pi 4 Pro device tree), with DietPi's first-boot automation
file filled in and my NVMe setup script hooked into it. The exact changes are in
`image/bake-dietpi-image.sh`, nothing else was touched.

> **An even better file is coming:** the same thing built on Orange Pi's own Debian 13
> Xfce desktop image (desktop already inside, no internet needed on first boot, Wi-Fi and
> audio drivers vendor-tested). Google Drive is throttling downloads of that 1.4 GB image
> from my side right now; as soon as it comes through, `image/` gets a second file,
> `Orangepi4pro_1.1.0_debian_trixie_desktop_xfce_linux6.6.98-nvme-autosetup.img.xz`, and
> this README will point at it. Flashing is identical.

---

## If something goes wrong

**Nothing on HDMI for the first minute:** normal, U-Boot and the kernel take ~30 s. If it
stays black for 5 minutes, re-flash the card and check the power supply.

**It sits at a text login instead of the desktop:** the first-boot install didn't finish,
usually because there was no Ethernet/internet. Plug Ethernet in and reboot: DietPi
resumes the automation. Or log in (`dietpi` / `orangepi`) and run `dietpi-software` ->
Install -> Xfce, then `dietpi-autostart` -> Desktop autologin.

**SSD not showing:** open a terminal and run `sudo setup-nvme.sh`. It prints why (not
detected, not seated, power). The M.2 slot is M-key, 2280, PCIe 3.0 x1; a normal NVMe
drive. M.2 **SATA** drives will not work. A weak power supply makes the SSD vanish.

**Board reboots on its own / screen goes black:** power supply or cable. 5 V / 3 A
minimum, short thick USB-C cable.

**No GPU acceleration / video is choppy:** the vendor kernel has no desktop GPU driver
in this build, so the desktop is drawn by the CPU. Xfce is fine, 1080p YouTube is not.

**Want the whole system on the SSD (boot without the SD card)?** Only the official Orange
Pi images ship the tool for that (`nand-sata-install`, manual section 3.36). That is
what the upcoming official-based image is for; `move-system-to-nvme.sh` automates the
prep and walks you through the menu.

---

## What else is in this folder

| File | What it is |
|---|---|
| `image/` | The flashable image (Git LFS) and the two scripts that build it. |
| `setup-nvme.sh` | The NVMe setup script baked into the image. Can also be run by hand on any Orange Pi / Debian / Ubuntu board: formats a blank SSD, mounts it permanently by UUID with `nofail`, bookmarks it. Re-runnable. `--wipe`, `--move-home-folders`, `--mountpoint`. |
| `move-system-to-nvme.sh` | For the official Orange Pi images: moves the running system onto the SSD and sets the board to boot from it via the on-board SPI flash. |
| `image/nvme-firstboot.service` | The systemd unit that runs `setup-nvme.sh` on first boot. |
| `image/bake-image.sh` | Builds the official-Orange-Pi-based image (official `.7z` in, `.img.xz` out). |
| `image/bake-dietpi-image.sh` | Builds the DietPi-based image above. |

## Doing it by hand with an official Orange Pi image instead

Official images (Google Drive, from the
[Orange Pi 4 Pro support page](http://www.orangepi.org/html/hardWare/computerAndMicrocontrollers/service-and-support/Orange-Pi-4-Pro.html)):

* Ubuntu: <https://drive.google.com/drive/folders/1TcFAGTblG_gbDhwFotEWS0y78LwHA-VG>
* Debian: <https://drive.google.com/drive/folders/1AzF-uTwA328qDFPaVBaKpiP4VjZjkmbS>
* Manual: <https://orangepi.net/wp-content/uploads/2026/01/OrangePi_4_Pro_A733_User-Manual_v1.4.pdf>

| File | What | Size |
|---|---|---|
| `Orangepi4pro_1.1.0_debian_trixie_desktop_xfce_linux6.6.98.7z` | Debian 13 Xfce, kernel 6.6 (Aug 2026) | 1.4 GB |
| `Orangepi4pro_1.1.0_ubuntu_resolute_desktop_xfce_linux6.6.98.7z` | Ubuntu 26.04 Xfce, kernel 6.6 (Aug 2026) | 1.8 GB |
| `Orangepi4pro_1.0.6_ubuntu_jammy_desktop_xfce_linux5.15.147.7z` | Ubuntu 22.04 Xfce, kernel 5.15 (manual's version) | 1.1 GB |
| `Orangepi4pro_1.0.6_debian_bullseye_desktop_xfce_linux5.15.147.7z` | Debian 11 Xfce, only one with GPU accel, but EOL | 1.0 GB |

Unpack the `.7z` with 7-Zip, flash the `.img` with Etcher, boot (auto-login as
`orangepi` / `orangepi`, root `orangepi`), then in a terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/prestonericson7-lang/esp32/claude/orange-pi-desktop-nvme-boot-z1h4n4/orangepi-4-pro/setup-nvme.sh -o setup-nvme.sh && sudo bash setup-nvme.sh
```

Optional, boot the whole system from the SSD (manual section 3.36, automated):

```bash
curl -fsSL https://raw.githubusercontent.com/prestonericson7-lang/esp32/claude/orange-pi-desktop-nvme-boot-z1h4n4/orangepi-4-pro/move-system-to-nvme.sh -o move-system-to-nvme.sh && sudo bash move-system-to-nvme.sh
```

It erases the SSD, partitions it like the manual, opens Orange Pi's installer: choose
**"Boot from SPI - system on SATA, USB or NVMe"** -> Yes -> ext4 -> wait -> write
bootloader to SPI: Yes -> Power off. Unplug, **take the SD card out**, power on. The SD
slot keeps boot priority, so the card always gets you back in. Orange Pi only tested this
with Fanxiang and Guangcun SSDs; some Kingston drives had trouble (partly fixed in 1.0.6).

## Sources

* Orange Pi 4 Pro user manual v1.4, sections 2.3, 2.9, 3.1, 3.2, 3.4, 3.6, 3.36, 7.2.
* Orange Pi 4 Pro download page and Google Drive folders (read 2026-10-11).
* DietPi Orange Pi 4 Pro images: <https://dietpi.com/downloads/images/> and
  DietPi automation reference `dietpi.txt`.
* Armbian community support PR (vendor 6.6.98 kernel, NVMe, SPI boot): <https://github.com/armbian/build/pull/9967>
