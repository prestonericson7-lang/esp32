#!/usr/bin/env bash
# =============================================================================
#  bake-dietpi-image.sh  -  fallback "one file" image for the Orange Pi 4 Pro,
#  built from the official DietPi image (dietpi.com/downloads/images/
#  DietPi_OrangePi4Pro-ARMv8-Trixie.img.xz, vendor 6.6.98 kernel).
#
#  DietPi has a first-boot automation file (/boot/dietpi.txt).  This script
#  fills it in so the FIRST BOOT, with an Ethernet cable plugged in, does all
#  of this by itself with no questions asked:
#     - installs the Xfce desktop + Firefox
#     - sets the desktop to auto-login as user "dietpi"
#     - runs setup-nvme.sh (formats the blank SSD, mounts it at /mnt/nvme,
#       bookmark in the file manager) via /boot/Automation_Custom_Script.sh
#       AND via a systemd service (belt and braces)
#     - password for "dietpi" and "root": orangepi
#
#  Usage:  sudo bash bake-dietpi-image.sh DietPi_OrangePi4Pro-ARMv8-Trixie.img.xz
#  Output: OrangePi4Pro-DietPi-Xfce-desktop-NVMe-autosetup.img.xz (+ .sha256)
# =============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IN="${1:?give me DietPi_OrangePi4Pro-ARMv8-Trixie.img.xz (or the .img)}"
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
for t in xz losetup partx mount; do command -v "$t" >/dev/null || { echo "missing tool: $t"; exit 1; }; done

WORK="$(dirname "$(readlink -f "$IN")")"; cd "$WORK"
if [[ "$IN" == *.xz ]]; then
  IMG="$WORK/OrangePi4Pro-DietPi-Xfce-desktop-NVMe-autosetup.img"
  echo "==> decompressing $IN -> $IMG"
  xz -dkc "$IN" > "$IMG"
else
  IMG="$WORK/OrangePi4Pro-DietPi-Xfce-desktop-NVMe-autosetup.img"
  cp -f "$IN" "$IMG"
fi

MNT=""
LOOP="$(losetup -f --show "$IMG")"
trap '[[ -n "$MNT" ]] && umount -q "$MNT" 2>/dev/null; losetup -d "$LOOP" 2>/dev/null || true' EXIT
partx -a "$LOOP" 2>/dev/null || true
sleep 1
PART="${LOOP}p1"
[[ -b "$PART" ]] || { echo "no partition 1 in $IMG"; exit 1; }
MNT="$(mktemp -d)"
mount "$PART" "$MNT"
echo "==> mounted $PART ($(grep PRETTY_NAME "$MNT/etc/os-release"))"
[[ -f "$MNT/boot/dietpi.txt" ]] || { echo "not a DietPi image"; exit 1; }

# ---------------------------------------------------------------- dietpi.txt
set_kv() {  # set_kv KEY VALUE  (replace the existing line, or append)
  local f="$MNT/boot/dietpi.txt"
  if grep -qE "^#?$1=" "$f"; then
    sed -i -E "s|^#?$1=.*|$1=$2|" "$f"
  else
    echo "$1=$2" >> "$f"
  fi
}
set_kv AUTO_SETUP_AUTOMATED 1
set_kv AUTO_SETUP_GLOBAL_PASSWORD orangepi
set_kv AUTO_SETUP_LOCALE en_US.UTF-8
set_kv AUTO_SETUP_KEYBOARD_LAYOUT us
set_kv AUTO_SETUP_NET_ETHERNET_ENABLED 1
set_kv AUTO_SETUP_NET_WIFI_ENABLED 0
set_kv AUTO_SETUP_INSTALL_SOFTWARE_ID "25 67"          # 25 = Xfce desktop, 67 = Firefox
set_kv AUTO_SETUP_BROWSER_INDEX -1                      # Firefox
set_kv AUTO_SETUP_AUTOSTART_TARGET_INDEX 2              # 2 = desktop autologin
set_kv AUTO_SETUP_AUTOSTART_LOGIN_USER dietpi
set_kv AUTO_SETUP_CUSTOM_SCRIPT_EXEC 1
set_kv SURVEY_OPTED_IN 0
set_kv CONFIG_CHECK_DIETPI_UPDATES 1
set_kv AUTO_SETUP_DHCP_TO_STATIC 0
echo "==> dietpi.txt automation values:"
grep -E "^(AUTO_SETUP_(AUTOMATED|GLOBAL_PASSWORD|LOCALE|KEYBOARD_LAYOUT|NET_ETHERNET_ENABLED|INSTALL_SOFTWARE_ID|BROWSER_INDEX|AUTOSTART_TARGET_INDEX|AUTOSTART_LOGIN_USER|CUSTOM_SCRIPT_EXEC)|SURVEY_OPTED_IN)=" "$MNT/boot/dietpi.txt"

# ---------------------------------------------------------------- NVMe setup hooks
install -D -m 755 "$HERE/../setup-nvme.sh" "$MNT/usr/local/sbin/setup-nvme.sh"
cat > "$MNT/boot/Automation_Custom_Script.sh" <<'EOF'
#!/bin/bash
# DietPi runs this once, as root, after the first-boot software installs.
/usr/local/sbin/setup-nvme.sh --yes && touch /var/lib/nvme-firstboot.done
exit 0
EOF
chmod 755 "$MNT/boot/Automation_Custom_Script.sh"
install -D -m 644 "$HERE/nvme-firstboot.service" "$MNT/etc/systemd/system/nvme-firstboot.service"
mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"
ln -sf /etc/systemd/system/nvme-firstboot.service "$MNT/etc/systemd/system/multi-user.target.wants/nvme-firstboot.service"
rm -f "$MNT/var/lib/nvme-firstboot.done"
cat > "$MNT/etc/nvme-autosetup-release" <<EOF
base_image=$(basename "$IN")
added=/boot/dietpi.txt automation, /boot/Automation_Custom_Script.sh, /usr/local/sbin/setup-nvme.sh, nvme-firstboot.service
source=https://github.com/prestonericson7-lang/esp32/tree/main/orangepi-4-pro
baked=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF
sync
umount "$MNT"; MNT=""
losetup -d "$LOOP"; trap - EXIT

OUT="$IMG.xz"
echo "==> compressing to $OUT"
xz -T0 -6 -f -c "$IMG" > "$OUT"
rm -f "$IMG"
( cd "$WORK" && sha256sum "$(basename "$OUT")" > "$OUT.sha256" )
ls -la "$OUT" "$OUT.sha256"
