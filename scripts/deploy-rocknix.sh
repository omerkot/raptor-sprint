#!/bin/sh
# Installs dist/rocknix onto a ROCKNIX handheld over SSH, replacing roms/ports/raptorsprint's program
# and assets but keeping saves, best times, settings and log.txt. Usage: scripts/deploy-rocknix.sh <host>
# Logs in with an SSH key: $ROCKNIX_KEY if set, else ssh's defaults. One SSH session.
set -eu
HOST=${1:?usage: deploy-rocknix.sh <host>}
PORTS=/storage/roms/ports
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PKG="$ROOT/dist/rocknix"
[ -d "$PKG" ] || { echo "run scripts/package-rocknix.sh first" >&2; exit 1; }
SSH="ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new"
if [ -n "${ROCKNIX_KEY:-}" ]; then SSH="$SSH -i $ROCKNIX_KEY -o IdentitiesOnly=yes"; fi
(cd "$PKG/ports" && COPYFILE_DISABLE=1 tar -cf - raptorsprint "Raptor Sprint.sh") |
    $SSH "root@$HOST" "mkdir -p '$PORTS/raptorsprint' && rm -rf '$PORTS/raptorsprint/assets' '$PORTS/raptorsprint/raptorsprint.aarch64' && tar -xf - -C '$PORTS' && chown -R root:root '$PORTS/raptorsprint' '$PORTS/Raptor Sprint.sh' && sync"
echo "installed on $HOST:$PORTS"
