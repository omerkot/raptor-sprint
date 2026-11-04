#!/bin/sh
# muOS launcher for single-screen handhelds such as the RG34XXSP (goes in ROMS/Ports next to the
# raptorsprint folder). Runs the CPU in "performance" while the game plays, then puts back the
# governor that was set. Touches no volume or firmware settings.
PORTDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GAMEDIR="$PORTDIR/raptorsprint"
[ -x "$GAMEDIR/raptorsprint.aarch64" ] || exit 1

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run}"
export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0
# muOS maps the pad by printed label (a: is the A button, on the east side); the game reads
# positions like SDL's own layout (a = south), so swap a/b and x/y to
# make each button do what its label says.
export SDL_GAMECONTROLLERCONFIG_FILE=/usr/lib/gamecontrollerdb.txt
if [ -r /opt/muos/device/config/sdl/name ]; then
    controller_name=$(cat /opt/muos/device/config/sdl/name)
    SDL_GAMECONTROLLERCONFIG=$(grep -F ",$controller_name," "$SDL_GAMECONTROLLERCONFIG_FILE" |
        sed -e 's/,a:/,@A:/; s/,b:/,a:/; s/,@A:/,b:/; s/,x:/,@X:/; s/,y:/,x:/; s/,@X:/,y:/')
    export SDL_GAMECONTROLLERCONFIG
fi
# Lets muOS's menu-button handling know which process is in front.
if [ -e /run/muos/system/foreground_process ]; then
    printf '%s' raptorsprint.aarch64 > /run/muos/system/foreground_process
fi
# The panel's size (720x480 on the RG34XXSP).
W=$(cat /opt/muos/device/config/screen/width 2>/dev/null)
H=$(cat /opt/muos/device/config/screen/height 2>/dev/null)

GOV_FILE=$(cat /opt/muos/device/config/cpu/governor 2>/dev/null)
[ -w "$GOV_FILE" ] || GOV_FILE=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
OLD_GOV=""
if [ -w "$GOV_FILE" ] && grep -qw performance "${GOV_FILE%/*}/scaling_available_governors" 2>/dev/null; then
    OLD_GOV=$(cat "$GOV_FILE")
    echo performance > "$GOV_FILE"
fi

# Debugging: an empty file named diag.on next to the game logs frame times to log.txt.
DIAG=""
[ -f "$GAMEDIR/diag.on" ] && DIAG=--diag
cd "$GAMEDIR" || exit 1
./raptorsprint.aarch64 --res "${W:-720}x${H:-480}" $DIAG "$@" > log.txt 2>&1
[ -n "$OLD_GOV" ] && echo "$OLD_GOV" > "$GOV_FILE"
exit 0
