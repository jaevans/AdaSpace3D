#!/usr/bin/env bash
# Builds the config-mode drive image on the host, mounts it with hdiutil and
# checks CONFIG.HTM and the volume name. macOS only.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# realpath: mount reports /private/var/..., so detach must use the same path.
work="$(realpath "$(mktemp -d)")"
mnt="$work/mnt"
bad_mnt="$work/bad_mnt"

cleanup() {
  for m in "$mnt" "$bad_mnt"; do
    if mount | grep -qF " on $m "; then hdiutil detach -quiet "$m" || hdiutil detach -force -quiet "$m" || true; fi
  done
  rm -rf "$work"
}
trap cleanup EXIT

attach() {
  hdiutil attach -readonly -nobrowse -mountpoint "$2" -imagekey diskimage-class=CRawDiskImage "$1"
}

c++ -std=c++17 -Wall -Wextra -Werror -o "$work/vd" "$here/virtual_drive_test.cpp"
"$work/vd" "$work/drive.img" "$work/page.html"

mkdir "$mnt"
attach "$work/drive.img" "$mnt" >/dev/null
cmp "$mnt/CONFIG.HTM" "$work/page.html"
volume="$(diskutil info "$mnt" | awk -F': *' '/Volume Name/ {print $2}')"
if [ "$volume" != "ADASPACE" ]; then
  echo "FAIL: volume name is '$volume', expected ADASPACE" >&2
  exit 1
fi
hdiutil detach -quiet "$mnt"

# Negative control: a zero bytes-per-sector field must not mount, or the
# checks above would pass for any image.
cp "$work/drive.img" "$work/bad.img"
printf '\x00\x00' | dd of="$work/bad.img" bs=1 seek=11 conv=notrunc 2>/dev/null
mkdir "$bad_mnt"
if attach "$work/bad.img" "$bad_mnt" >/dev/null 2>&1; then
  echo "FAIL: corrupted image mounted; the mount check proves nothing" >&2
  exit 1
fi

echo "PASS: CONFIG.HTM ($(wc -c < "$work/page.html" | tr -d ' ') bytes) and volume ADASPACE verified"
