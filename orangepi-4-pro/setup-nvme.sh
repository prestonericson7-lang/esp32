#!/usr/bin/env bash
# =============================================================================
#  setup-nvme.sh  -  Orange Pi 4 Pro: make the M.2 NVMe SSD usable from the desktop
#
#  What it does (safe by default, idempotent, re-runnable):
#    1. Finds the NVMe SSD (/dev/nvme0n1).  If it is not detected it prints
#       diagnostics (PCIe link, kernel log) and tells you what to check.
#    2. If the SSD is BLANK (no partition table, no filesystem) it creates one
#       GPT partition spanning the whole drive and formats it ext4 (label "nvme").
#       If the SSD already has a filesystem it is NOT touched unless you pass --wipe.
#    3. Adds an /etc/fstab line (by UUID, with "nofail" so a missing/removed SSD
#       can never stop the board from booting) and mounts it at /mnt/nvme.
#    4. Gives ownership of the mount to your desktop user, puts a link
#       ~/NVMe -> /mnt/nvme in your home folder and a bookmark in the file
#       manager sidebar so it just shows up like a normal drive.
#    5. Optional (--move-home-folders): moves Downloads/Documents/Pictures/
#       Videos/Music onto the SSD and links them back, so big files land on the
#       128 GB SSD instead of the SD card.
#
#  Usage (on the Orange Pi, in a terminal):
#       sudo bash setup-nvme.sh                 # normal run
#       sudo bash setup-nvme.sh --wipe          # erase whatever is on the SSD first
#       sudo bash setup-nvme.sh --move-home-folders
#       sudo bash setup-nvme.sh --mountpoint /data --label fast
#       sudo bash setup-nvme.sh --yes           # no questions (for scripted use)
#
#  Tested target: Orange Pi 4 Pro (Allwinner A733) official Ubuntu/Debian
#  desktop images.  Should also work on any Debian/Ubuntu based SBC image.
# =============================================================================
set -euo pipefail

# ----------------------------------------------------------------------------- options
MOUNTPOINT="/mnt/nvme"
LABEL="nvme"
WIPE=0
MOVE_HOME=0
ASSUME_YES=0
DEVICE=""

usage() {
  sed -n '2,30p' "$0" | sed 's/^# \{0,2\}//'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --wipe)              WIPE=1 ;;
    --move-home-folders) MOVE_HOME=1 ;;
    --yes|-y)            ASSUME_YES=1 ;;
    --mountpoint)        MOUNTPOINT="${2:?--mountpoint needs a path}"; shift ;;
    --label)             LABEL="${2:?--label needs a name}"; shift ;;
    --device)            DEVICE="${2:?--device needs e.g. /dev/nvme0n1}"; shift ;;
    -h|--help)           usage 0 ;;
    *) echo "Unknown option: $1"; usage 1 ;;
  esac
  shift
done

# ----------------------------------------------------------------------------- helpers
BOLD=$'\e[1m'; GREEN=$'\e[32m'; YELLOW=$'\e[33m'; RED=$'\e[31m'; RESET=$'\e[0m'
info()  { echo "${BOLD}==>${RESET} $*"; }
ok()    { echo "${GREEN}  ✔${RESET} $*"; }
warn()  { echo "${YELLOW}  !${RESET} $*"; }
die()   { echo "${RED}ERROR:${RESET} $*" >&2; exit 1; }

confirm() {
  # confirm "question"  -> returns 0 for yes
  [[ $ASSUME_YES -eq 1 ]] && return 0
  local answer
  read -r -p "$1 [y/N] " answer </dev/tty || answer=""
  [[ "$answer" =~ ^[Yy]([Ee][Ss])?$ ]]
}

need_root() {
  [[ $EUID -eq 0 ]] || die "Run me with sudo:   sudo bash $0 $*"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || return 1
}

ensure_tools() {
  # Everything below is in the official images already, but a server/minimal
  # image may lack parted/e2fsprogs.  Install quietly if apt is available.
  local missing=()
  need_cmd lsblk    || missing+=(util-linux)
  need_cmd blkid    || missing+=(util-linux)
  need_cmd sfdisk   || missing+=(fdisk)
  need_cmd wipefs   || missing+=(util-linux)
  need_cmd mkfs.ext4|| missing+=(e2fsprogs)
  if [[ $MOVE_HOME -eq 1 ]] && ! need_cmd rsync; then missing+=(rsync); fi
  if [[ ${#missing[@]} -gt 0 ]]; then
    info "Installing missing tools: ${missing[*]}"
    if need_cmd apt-get; then
      DEBIAN_FRONTEND=noninteractive apt-get install -y -q "${missing[@]}" \
        || { DEBIAN_FRONTEND=noninteractive apt-get update -q && \
             DEBIAN_FRONTEND=noninteractive apt-get install -y -q "${missing[@]}"; } \
        || die "Could not install ${missing[*]}. Connect to the internet and re-run."
    else
      die "Missing tools: ${missing[*]} (and no apt-get to install them)."
    fi
  fi
}

# The user that owns the desktop session: whoever ran sudo, else uid 1000.
desktop_user() {
  local u="${SUDO_USER:-}"
  if [[ -z "$u" || "$u" == "root" ]]; then
    u="$(getent passwd 1000 | cut -d: -f1 || true)"
  fi
  [[ -n "$u" ]] || u="root"
  echo "$u"
}

# Root filesystem's underlying disk, e.g. mmcblk0 or nvme0n1 (never format this!)
root_disk() {
  local src
  src="$(findmnt -n -o SOURCE / 2>/dev/null || true)"
  [[ -n "$src" ]] || return 0
  lsblk -n -o PKNAME "$src" 2>/dev/null | head -n1 || true
}

# ----------------------------------------------------------------------------- 1. find the SSD
find_nvme() {
  if [[ -n "$DEVICE" ]]; then
    [[ -b "$DEVICE" ]] || die "$DEVICE is not a block device."
    echo "$DEVICE"; return
  fi
  # Prefer real NVMe disks (transport "nvme"); fall back to name match.
  local d
  d="$(lsblk -d -n -o NAME,TRAN 2>/dev/null | awk '$2=="nvme"{print "/dev/"$1; exit}')"
  if [[ -z "$d" ]]; then
    local cand
    for cand in /dev/nvme[0-9]n[0-9]; do
      [[ -b "$cand" ]] && { d="$cand"; break; }
    done
  fi
  echo "$d"
}

nvme_diagnostics() {
  echo
  echo "${RED}No NVMe SSD was detected.${RESET}  Things to check, in order:"
  echo "  1. Power the board OFF (unplug), re-seat the SSD in the M.2 slot and screw it down."
  echo "     The Orange Pi 4 Pro M.2 slot is M-key, 2280 length, PCIe 3.0 x1 - a normal"
  echo "     NVMe SSD.  SATA M.2 drives (B+M key) will NOT work."
  echo "  2. Use a good 5V/4A or higher USB-C power supply.  An SSD adds ~3-5 W and a weak"
  echo "     phone charger makes drives randomly vanish."
  echo "  3. Some SSDs need a reboot after the first cold boot to be enumerated - try once more."
  echo
  echo "Diagnostics (copy/paste these if you ask for help):"
  echo "--- lsblk ---";        lsblk -d -o NAME,SIZE,TYPE,TRAN,MODEL 2>/dev/null || true
  echo "--- lspci ---";        (need_cmd lspci && lspci 2>/dev/null) || echo "(lspci not installed: sudo apt install pciutils)"
  echo "--- kernel log (pci/nvme) ---"
  dmesg 2>/dev/null | grep -iE 'pci|nvme' | tail -n 25 || true
  echo "--- /boot/orangepiEnv.txt ---"
  [[ -f /boot/orangepiEnv.txt ]] && cat /boot/orangepiEnv.txt || echo "(not an Orange Pi image?)"
  exit 1
}

# ----------------------------------------------------------------------------- main
need_root "$@"
ensure_tools

DESK_USER="$(desktop_user)"
DESK_HOME="$(getent passwd "$DESK_USER" | cut -d: -f6)"
ROOT_DISK="$(root_disk)"

info "Looking for the NVMe SSD..."
NVME="$(find_nvme)"
[[ -n "$NVME" && -b "$NVME" ]] || nvme_diagnostics
NVME_NAME="$(basename "$NVME")"
NVME_SIZE="$(lsblk -d -n -o SIZE "$NVME")"
NVME_MODEL="$(lsblk -d -n -o MODEL "$NVME" | sed 's/[[:space:]]*$//')"
ok "Found $NVME  ($NVME_SIZE, ${NVME_MODEL:-unknown model})"

if [[ -n "$ROOT_DISK" && "$ROOT_DISK" == "$NVME_NAME" ]]; then
  die "$NVME is the disk you are currently booted from. Refusing to touch it."
fi

# ----------------------------------------------------------------------------- 2. partition / format
# Decide what is on the drive right now.
PART=""
EXISTING_FS=""
# first partition on the drive (if any)
PART="$(lsblk -n -o PATH,TYPE "$NVME" | awk '$2=="part"{print $1; exit}' || true)"
if [[ -n "$PART" ]]; then
  EXISTING_FS="$(blkid -o value -s TYPE "$PART" 2>/dev/null || true)"
else
  # maybe a filesystem written directly on the whole disk (rare)
  EXISTING_FS="$(blkid -o value -s TYPE "$NVME" 2>/dev/null || true)"
fi

format_drive() {
  info "Creating a GPT partition table and one ext4 partition on $NVME"
  # make sure nothing on it is mounted
  while read -r mp; do
    [[ -n "$mp" ]] && umount -l "$mp" || true
  done < <(lsblk -n -o MOUNTPOINT "$NVME" | sed '/^$/d')
  wipefs -a "$NVME" >/dev/null
  # sfdisk is part of util-linux, so it is on every Debian/Ubuntu image.
  # "label: gpt" + ",,L" = one Linux-filesystem partition using the whole disk.
  printf 'label: gpt\n,,L\n' | sfdisk --quiet --wipe always --wipe-partitions always "$NVME"
  partx -u "$NVME" 2>/dev/null || blockdev --rereadpt "$NVME" 2>/dev/null || true
  if need_cmd udevadm; then udevadm settle 2>/dev/null || true; fi
  sleep 1
  PART="$(lsblk -n -o PATH,TYPE "$NVME" | awk '$2=="part"{print $1; exit}')"
  [[ -n "$PART" && -b "$PART" ]] || die "Partition did not appear after partitioning $NVME"
  mkfs.ext4 -q -F -L "$LABEL" "$PART"
  ok "Formatted $PART as ext4 (label: $LABEL)"
}

HOME_FOLDERS=(Downloads Documents Pictures Videos Music)

if [[ $WIPE -eq 1 ]]; then
  echo
  warn "--wipe given: EVERYTHING on $NVME ($NVME_SIZE) will be erased."
  for d in "${HOME_FOLDERS[@]}"; do
    if [[ -L "$DESK_HOME/$d" && "$(readlink "$DESK_HOME/$d")" == "$MOUNTPOINT"/* ]]; then
      warn "Your ~/$d folder currently lives on this SSD and will be erased too!"
    fi
  done
  confirm "Really erase $NVME?" || die "Aborted."
  format_drive
elif [[ -z "$EXISTING_FS" ]]; then
  ok "The SSD is blank - it will be set up fresh."
  format_drive
else
  ok "The SSD already has a ${EXISTING_FS} filesystem on ${PART:-$NVME} - keeping it."
  echo "    (run with --wipe if you want it erased and set up fresh)"
  [[ -n "$PART" ]] || PART="$NVME"
  case "$EXISTING_FS" in
    ext4|ext3|ext2|xfs|btrfs|f2fs) ;;
    ntfs|vfat|exfat)
      warn "$EXISTING_FS is a Windows-style filesystem; it will work, but Linux permissions"
      warn "won't apply. For a Linux-only data drive, --wipe gives you ext4." ;;
    *) warn "Unusual filesystem '$EXISTING_FS'; mounting it anyway." ;;
  esac
fi

FSTYPE="$(blkid -o value -s TYPE "$PART")"
UUID="$(blkid -o value -s UUID "$PART")"
[[ -n "$UUID" ]] || die "Could not read the UUID of $PART"

# ----------------------------------------------------------------------------- 3. fstab + mount
info "Mounting $PART at $MOUNTPOINT (and making it permanent)"
mkdir -p "$MOUNTPOINT"

# If something else is mounted at MOUNTPOINT, or the partition is mounted
# elsewhere (e.g. auto-mounted by the desktop under /media), unmount it first.
if mountpoint -q "$MOUNTPOINT"; then
  umount "$MOUNTPOINT" || umount -l "$MOUNTPOINT" || true
fi
while read -r mp; do
  [[ -n "$mp" && "$mp" != "$MOUNTPOINT" ]] && umount "$mp" 2>/dev/null || true
done < <(lsblk -n -o MOUNTPOINT "$PART" | sed '/^$/d')

case "$FSTYPE" in
  ext4|ext3|ext2|xfs|btrfs|f2fs) OPTS="defaults,noatime,nofail,x-systemd.device-timeout=10" ;;
  ntfs)   FSTYPE="ntfs3"; OPTS="defaults,nofail,uid=$(id -u "$DESK_USER"),gid=$(id -g "$DESK_USER"),x-systemd.device-timeout=10" ;;
  vfat|exfat) OPTS="defaults,nofail,uid=$(id -u "$DESK_USER"),gid=$(id -g "$DESK_USER"),x-systemd.device-timeout=10" ;;
  *)      OPTS="defaults,nofail,x-systemd.device-timeout=10" ;;
esac

FSTAB_LINE="UUID=$UUID  $MOUNTPOINT  $FSTYPE  $OPTS  0  2"
cp -a /etc/fstab "/etc/fstab.bak.$(date +%Y%m%d%H%M%S)"
# remove any older line for this mountpoint or this UUID, then append ours
awk -v mp="$MOUNTPOINT" -v uuid="UUID=$UUID" '
  /added by setup-nvme.sh/ { next }
  $0 ~ /^[[:space:]]*#/ { print; next }
  $2 == mp || $1 == uuid { next }
  { print }' /etc/fstab > /etc/fstab.new
{
  cat /etc/fstab.new
  echo "# NVMe SSD (added by setup-nvme.sh)"
  echo "$FSTAB_LINE"
} > /etc/fstab
rm -f /etc/fstab.new
systemctl daemon-reload 2>/dev/null || true
mount "$MOUNTPOINT" || mount -a || die "mount failed - check 'sudo journalctl -xe'"
mountpoint -q "$MOUNTPOINT" || die "$MOUNTPOINT is still not mounted."
ok "Mounted. fstab line:  $FSTAB_LINE"

# Stop the desktop from ALSO auto-mounting it under /media/... as a second copy.
if need_cmd udevadm; then
  cat > /etc/udev/rules.d/99-nvme-nomedia.rules <<EOF
# added by setup-nvme.sh: the NVMe is mounted via fstab, keep udisks from auto-mounting it
ENV{ID_FS_UUID}=="$UUID", ENV{UDISKS_IGNORE}="1"
EOF
  udevadm control --reload 2>/dev/null || true
fi

# ----------------------------------------------------------------------------- 4. ownership + shortcuts
if [[ "$FSTYPE" =~ ^(ext4|ext3|ext2|xfs|btrfs|f2fs)$ ]]; then
  chown "$DESK_USER":"$(id -gn "$DESK_USER")" "$MOUNTPOINT"
  chmod 775 "$MOUNTPOINT"
  # Leave lost+found alone but make it unobtrusive.
  [[ -d "$MOUNTPOINT/lost+found" ]] && chmod 700 "$MOUNTPOINT/lost+found" || true
fi
ok "$MOUNTPOINT is owned by $DESK_USER"

if [[ -n "$DESK_HOME" && -d "$DESK_HOME" && "$DESK_USER" != "root" ]]; then
  # ~/NVMe link
  if [[ ! -e "$DESK_HOME/NVMe" ]]; then
    ln -s "$MOUNTPOINT" "$DESK_HOME/NVMe"
    chown -h "$DESK_USER":"$(id -gn "$DESK_USER")" "$DESK_HOME/NVMe"
  fi
  # file-manager sidebar bookmark (GNOME Files / Nautilus, Thunar, Nemo, Caja all read this)
  DESK_GROUP="$(id -gn "$DESK_USER")"
  BM_DIR="$DESK_HOME/.config/gtk-3.0"
  install -d -o "$DESK_USER" -g "$DESK_GROUP" "$DESK_HOME/.config" "$BM_DIR"
  [[ -f "$BM_DIR/bookmarks" ]] || install -o "$DESK_USER" -g "$DESK_GROUP" -m 644 /dev/null "$BM_DIR/bookmarks"
  grep -qF "file://$MOUNTPOINT " "$BM_DIR/bookmarks" 2>/dev/null \
    || echo "file://$MOUNTPOINT NVMe SSD" >> "$BM_DIR/bookmarks"
  chown "$DESK_USER":"$DESK_GROUP" "$BM_DIR/bookmarks"
  ok "Added ~/NVMe link and a file-manager bookmark"

  # If an earlier --move-home-folders run put ~/Downloads etc. on the SSD and the
  # SSD was since wiped/replaced, the links would dangle: recreate their targets.
  for d in "${HOME_FOLDERS[@]}"; do
    lnk="$DESK_HOME/$d"
    if [[ -L "$lnk" ]]; then
      tgt="$(readlink "$lnk")"
      if [[ "$tgt" == "$MOUNTPOINT"/* && ! -d "$tgt" ]]; then
        # create every missing path component below the mountpoint, owned by the user
        rel="${tgt#"$MOUNTPOINT"/}"
        p="$MOUNTPOINT"
        IFS='/' read -r -a parts <<< "$rel"
        for part in "${parts[@]}"; do
          p="$p/$part"
          [[ -d "$p" ]] || install -d -o "$DESK_USER" -g "$DESK_GROUP" "$p"
        done
        # shellcheck disable=SC2088
        warn "~/$d pointed to a folder that no longer existed on the SSD; recreated it (empty)."
      fi
    fi
  done
fi

# ----------------------------------------------------------------------------- 5. optional: move home folders
if [[ $MOVE_HOME -eq 1 ]]; then
  if [[ "$DESK_USER" == "root" || -z "$DESK_HOME" ]]; then
    warn "--move-home-folders skipped: no normal desktop user found."
  elif ! [[ "$FSTYPE" =~ ^(ext4|ext3|ext2|xfs|btrfs|f2fs)$ ]]; then
    warn "--move-home-folders skipped: needs a Linux filesystem (ext4). Use --wipe."
  else
    info "Moving big home folders onto the SSD"
    TARGET="$MOUNTPOINT/$DESK_USER"
    mkdir -p "$TARGET"
    chown "$DESK_USER":"$(id -gn "$DESK_USER")" "$TARGET"
    for d in "${HOME_FOLDERS[@]}"; do
      src="$DESK_HOME/$d"
      dst="$TARGET/$d"
      if [[ -L "$src" ]]; then
        ok "$d already a link -> $(readlink "$src")"; continue
      fi
      mkdir -p "$dst"
      if [[ -d "$src" ]]; then
        rsync -a "$src/" "$dst/"
        rm -rf "$src"
      fi
      ln -s "$dst" "$src"
      chown -R "$DESK_USER":"$(id -gn "$DESK_USER")" "$dst"
      chown -h "$DESK_USER":"$(id -gn "$DESK_USER")" "$src"
      ok "$d -> $dst"
    done
  fi
fi

# ----------------------------------------------------------------------------- done
echo
echo "${GREEN}${BOLD}All done.${RESET}"
df -h "$MOUNTPOINT" | sed 's/^/    /'
echo
echo "  The SSD is mounted at $MOUNTPOINT every boot (also reachable as ~/NVMe)."
echo "  It will show in the file manager sidebar as \"NVMe SSD\"."
[[ $MOVE_HOME -eq 0 ]] && echo "  Tip: re-run with --move-home-folders to keep Downloads/Documents/etc on the SSD."
echo
