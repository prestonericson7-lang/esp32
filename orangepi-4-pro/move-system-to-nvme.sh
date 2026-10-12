#!/usr/bin/env bash
# =============================================================================
#  move-system-to-nvme.sh  -  Orange Pi 4 Pro: run the WHOLE system from the NVMe
#
#  After this, the board boots without the SD card: the bootloader lives in the
#  16 MB SPI flash chip on the board and the Linux system lives on the NVMe SSD
#  (all 128 GB become the system drive).  This is the procedure from section
#  3.36 of the official Orange Pi 4 Pro user manual, with the prep automated.
#
#  !!! EVERYTHING ON THE NVMe SSD IS ERASED !!!
#
#  Usage (on the Orange Pi, booted from the SD card):
#       sudo bash move-system-to-nvme.sh            # interactive
#       sudo bash move-system-to-nvme.sh --dry-run  # only show what would happen
#
#  When the Orange Pi tool (nand-sata-install) opens, pick:
#       "Boot from SPI - system on SATA, USB or NVMe"  ->  OK  ->  Yes
#       filesystem: ext4  ->  wait  ->  "write bootloader to SPI flash?": Yes
#       -> Power off.  Then unplug power, REMOVE THE SD CARD, plug power back in.
#
#  If it ever fails to boot from the SSD, just put the SD card back in: the SD
#  card always has priority, so you can never lock yourself out.
# =============================================================================
set -euo pipefail

DRY=0
DEVICE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY=1 ;;
    --device)  DEVICE="${2:?--device needs e.g. /dev/nvme0n1}"; shift ;;
    -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,2\}//'; exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
  shift
done

BOLD=$'\e[1m'; GREEN=$'\e[32m'; YELLOW=$'\e[33m'; RED=$'\e[31m'; RESET=$'\e[0m'
info() { echo "${BOLD}==>${RESET} $*"; }
ok()   { echo "${GREEN}  ✔${RESET} $*"; }
warn() { echo "${YELLOW}  !${RESET} $*"; }
die()  { echo "${RED}ERROR:${RESET} $*" >&2; exit 1; }
run()  { if [[ $DRY -eq 1 ]]; then echo "    (dry-run) $*"; else "$@"; fi; }

[[ $EUID -eq 0 ]] || die "Run me with sudo:   sudo bash $0"

# ---------------------------------------------------------------- sanity checks
ROOT_SRC="$(findmnt -n -o SOURCE / || true)"
ROOT_DISK="$(lsblk -n -o PKNAME "$ROOT_SRC" 2>/dev/null | head -n1 || true)"
case "$ROOT_DISK" in
  nvme*) die "You are already running from the NVMe ($ROOT_SRC). Nothing to do." ;;
  mmcblk*) ok "Running from the SD card ($ROOT_SRC) - good." ;;
  *) warn "Root is on $ROOT_SRC (not an SD card). Continuing anyway." ;;
esac

if [[ -z "$DEVICE" ]]; then
  DEVICE="$(lsblk -d -n -o NAME,TRAN 2>/dev/null | awk '$2=="nvme"{print "/dev/"$1; exit}')"
  [[ -n "$DEVICE" ]] || for c in /dev/nvme[0-9]n[0-9]; do [[ -b "$c" ]] && { DEVICE="$c"; break; }; done
fi
[[ -n "$DEVICE" && -b "$DEVICE" ]] || die "No NVMe SSD found. Power off, re-seat the SSD, try again (see setup-nvme.sh for diagnostics)."
ok "NVMe SSD: $DEVICE  ($(lsblk -d -n -o SIZE "$DEVICE"), $(lsblk -d -n -o MODEL "$DEVICE" | sed 's/ *$//'))"

if ! command -v nand-sata-install >/dev/null 2>&1; then
  die "nand-sata-install is not on this image. Use:  sudo orangepi-config  ->  System  ->  Install  (same menu), after partitioning: see the manual section 3.36."
fi

# SPI flash shows up as an MTD device when the kernel sees it
if ls /dev/mtd0 /sys/class/mtd/mtd0 >/dev/null 2>&1; then
  ok "SPI flash detected ($(cat /sys/class/mtd/mtd0/name 2>/dev/null || echo mtd0))."
else
  warn "No SPI flash (/dev/mtd0) visible. Booting without the SD card needs it."
  warn "You can still continue; if the board won't boot from the SSD afterwards, keep using the SD card to boot (choose 'Boot from SD - system on NVMe' in the tool instead)."
fi

# Debian Bullseye images: the manual says unload the GPU driver first.
if lsmod 2>/dev/null | grep -q '^pvrsrvkm'; then
  if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    die "The GPU driver (pvrsrvkm) is loaded and you are inside the desktop. The manual says to stop the desktop first:
    press Ctrl+Alt+F3, log in (orangepi / orangepi), then run:  sudo bash $0"
  fi
  info "Stopping the desktop and unloading the GPU driver (manual section 3.36)"
  run systemctl stop lightdm || true
  run rmmod pvrsrvkm || true
fi

# ---------------------------------------------------------------- the warning
echo
echo "${RED}${BOLD}THIS WILL ERASE EVERYTHING ON $DEVICE${RESET} and copy the running system onto it."
echo "Afterwards the board boots from the SSD with the SD card removed."
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DEVICE" | sed 's/^/    /'
echo
if [[ $DRY -eq 0 ]]; then
  read -r -p "Type ERASE to continue: " ans </dev/tty || ans=""
  [[ "$ans" == "ERASE" ]] || die "Aborted - nothing was changed."
fi

# ---------------------------------------------------------------- prep
info "Unmounting anything on $DEVICE and removing setup-nvme.sh's fstab entry"
while read -r mp; do
  [[ -n "$mp" ]] && run umount -l "$mp"
done < <(lsblk -n -o MOUNTPOINT "$DEVICE" | sed '/^$/d')
if grep -q 'added by setup-nvme.sh' /etc/fstab 2>/dev/null; then
  run cp -a /etc/fstab "/etc/fstab.bak.$(date +%Y%m%d%H%M%S)"
  if [[ $DRY -eq 0 ]]; then
    # drop our comment line and the fstab line that follows it
    sed -i '/added by setup-nvme.sh/{N;d;}' /etc/fstab
  else
    echo "    (dry-run) remove the 'added by setup-nvme.sh' lines from /etc/fstab"
  fi
fi
run rm -f /etc/udev/rules.d/99-nvme-nomedia.rules
for lnk in /home/*/NVMe; do
  [[ -L "$lnk" ]] && run rm -f "$lnk"
done

info "Partitioning $DEVICE exactly as the manual does (GPT, one ext4 partition from sector 65536)"
run wipefs -a "$DEVICE"
if command -v parted >/dev/null 2>&1; then
  run parted --script "$DEVICE" mklabel gpt mkpart primary ext4 65536s 100%
else
  if [[ $DRY -eq 1 ]]; then
    printf '    (dry-run) printf "label: gpt\\nstart=65536, type=L\\n" | sfdisk %s\n' "$DEVICE"
  else
    printf 'label: gpt\nstart=65536, type=L\n' | sfdisk --quiet --wipe always "$DEVICE"
  fi
fi
run partx -u "$DEVICE" || true
sleep 1
ok "Partition table written."

# ---------------------------------------------------------------- hand over to Orange Pi's installer
cat <<EOF

${BOLD}The Orange Pi installer will open now.  Do exactly this:${RESET}

   1. Select  ${BOLD}"Boot from SPI - system on SATA, USB or NVMe"${RESET}   and press Enter
      (if the SSD is not the first choice, make sure $DEVICE / $(basename "$DEVICE")p1 is selected)
   2. Confirm with  ${BOLD}<Yes>${RESET}
   3. Filesystem type:  ${BOLD}ext4${RESET}
   4. Wait - formatting and copying takes 5-15 minutes.  Do not touch anything.
   5. "Write the bootloader to SPI flash?"  ->  ${BOLD}<Yes>${RESET}
   6. Choose  ${BOLD}<Power off>${RESET}

   Then: unplug the power, ${BOLD}TAKE THE SD CARD OUT${RESET}, plug the power back in.
   The board now boots from the SSD.  Keep the SD card as a rescue stick.

EOF
if [[ $DRY -eq 1 ]]; then
  echo "(dry-run) would now run:  nand-sata-install"
  exit 0
fi
read -r -p "Press Enter to open the installer..." _ </dev/tty || true
exec nand-sata-install
