#!/bin/sh
# Installs dist/muos onto a muOS handheld over SSH (enable SSH in muOS's web services), into
# ROMS/Ports on the union mount. Replaces the program and assets; keeps saves, best times and
# settings. Only copies files: start the game from Ports on the device.
# Usage: scripts/deploy-muos.sh <host>      Logs in with $MUOS_KEY if set, else ssh's defaults.
set -eu
HOST=${1:?usage: deploy-muos.sh <host>}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PKG="$ROOT/dist/muos"
[ -d "$PKG" ] || { echo "run scripts/package-muos.sh first" >&2; exit 1; }
SSH="ssh -T -o ConnectTimeout=5 -o BatchMode=yes -o StrictHostKeyChecking=accept-new"
if [ -n "${MUOS_KEY:-}" ]; then SSH="$SSH -i $MUOS_KEY -o IdentitiesOnly=yes"; fi
# A tar stream over ssh -T (scp hangs on dropbear; a pty corrupts binary data).
(cd "$PKG" && COPYFILE_DISABLE=1 tar -cf - raptorsprint "Raptor Sprint.sh") |
    $SSH "root@$HOST" 'sh -c "
        dest=/mnt/union/ROMS/Ports
        [ -d \$dest ] || dest=/mnt/sdcard/ROMS/Ports
        mkdir -p \$dest/raptorsprint && rm -rf \$dest/raptorsprint/assets \$dest/raptorsprint/raptorsprint.aarch64
        tar -C \$dest -xf -
        find \$dest/raptorsprint -name \"._*\" -delete
        chmod +x \"\$dest/Raptor Sprint.sh\" \$dest/raptorsprint/raptorsprint.aarch64
        sync
        echo installed in \$dest
    "'
