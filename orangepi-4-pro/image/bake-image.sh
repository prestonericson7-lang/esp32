#!/usr/bin/env bash
# =============================================================================
#  bake-image.sh  -  turn the official Orange Pi 4 Pro desktop image into a
#  "flash it and forget it" image that sets up the NVMe SSD by itself.
#
#  What gets added to the official image (nothing else is changed):
#    /usr/local/sbin/setup-nvme.sh            the same script as ../setup-nvme.sh
#    /etc/systemd/system/nvme-firstboot.service   runs it once, on the first boot
#    (enabled for multi-user.target)
#
#  Usage (on any Linux PC with root, loop devices, 7z and xz):
#       sudo bash bake-image.sh Orangepi4pro_1.1.0_debian_trixie_desktop_xfce_linux6.6.98.7z
#       sudo bash bake-image.sh some.img            # already extracted
#  Output: <name>-nvme-autosetup.img.xz next to the input, plus a .sha256 file.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IN="${1:?give me the official .7z or .img}"
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
for t in 7z xz losetup mount sfdisk; do command -v "$t" >/dev/null || { echo "missing tool: $t"; exit 1; }; done

WORK="$(dirname "$(readlink -f "$IN")")"
cd "$WORK"

# 1. get a raw .img
if [[ "$IN" == *.7z ]]; then
  echo "==> extracting $IN"
  7z x -y "$IN" >/dev/null
  IMG="$(find . -maxdepth 1 -name '*.img' -printf '%T@ %p\n' | sort -rn | head -n1 | cut -d' ' -f2-)"
  if ls ./*.sha >/dev/null 2>&1; then
    echo "==> verifying checksum of the official image"
    sha256sum -c ./*.sha
  fi
else
  IMG="$IN"
fi
echo "==> image: $IMG ($(du -h "$IMG" | cut -f1))"

# 2. loop-mount the root partition (the Orange Pi images have exactly one partition)
MNT=""
LOOP="$(losetup -f --show -P "$IMG")"
trap '[[ -n "$MNT" ]] && umount -q "$MNT" 2>/dev/null; losetup -d "$LOOP" 2>/dev/null || true' EXIT
sleep 1
PART="$(lsblk -n -o PATH,TYPE "$LOOP" | awk '$2=="part"{print $1}' | tail -n1)"
[[ -b "$PART" ]] || { echo "no partition found in $IMG"; exit 1; }
MNT="$(mktemp -d)"
mount "$PART" "$MNT"
echo "==> mounted $PART at $MNT ($(cat "$MNT/etc/os-release" | grep PRETTY_NAME))"
[[ -d "$MNT/etc/systemd/system" && -d "$MNT/usr/local" ]] || { echo "this does not look like the rootfs"; exit 1; }

# 3. inject the script + service
install -D -m 755 "$HERE/../setup-nvme.sh"        "$MNT/usr/local/sbin/setup-nvme.sh"
install -D -m 644 "$HERE/nvme-firstboot.service"  "$MNT/etc/systemd/system/nvme-firstboot.service"
mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"
ln -sf /etc/systemd/system/nvme-firstboot.service "$MNT/etc/systemd/system/multi-user.target.wants/nvme-firstboot.service"
rm -f "$MNT/var/lib/nvme-firstboot.done"
# a note for whoever looks at the image later
cat > "$MNT/etc/nvme-autosetup-release" <<EOF
base_image=$(basename "$IMG")
added=/usr/local/sbin/setup-nvme.sh, /etc/systemd/system/nvme-firstboot.service
source=https://github.com/prestonericson7-lang/esp32/tree/main/orangepi-4-pro
baked=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF
sync
echo "==> injected:"
ls -la "$MNT/usr/local/sbin/setup-nvme.sh" "$MNT/etc/systemd/system/nvme-firstboot.service" "$MNT/etc/systemd/system/multi-user.target.wants/nvme-firstboot.service"
umount "$MNT"
losetup -d "$LOOP"
trap - EXIT

# 4. compress
OUT="${IMG%.img}-nvme-autosetup.img.xz"
echo "==> compressing to $OUT (this takes a while)"
xz -T0 -4 -k -f -c "$IMG" > "$OUT"
sha256sum "$(basename "$OUT")" > "$OUT.sha256"
echo "==> done:"
ls -la "$OUT" "$OUT.sha256"
