#!/bin/bash
# ROCKNIX launcher (goes in /storage/roms/ports next to the raptorsprint folder).
# The game runs as a Wayland client of ROCKNIX's sway. On the RG DS it has a fullscreen window on
# each panel: the view on DSI-2 (upper), the map on DSI-1 (lower).
# Other handhelds have one screen, and the game plays there in its single-screen layout. While it
# runs, the frontend is paused (SIGSTOP, so the pad doesn't drive the hidden menu too) and any
# lower panel is powered on; on exit both go back as they were, and so do the CPU governors.
. /etc/profile
PORTDIR="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR="$PORTDIR/raptorsprint"
[ -x "$GAMEDIR/raptorsprint.aarch64" ] || exit 1

export HOME="${HOME:-/storage}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
export SWAYSOCK="${SWAYSOCK:-$XDG_RUNTIME_DIR/sway-ipc.0.sock}"
export SDL_VIDEODRIVER=wayland
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-pulseaudio}"
export SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0
# The pad works whichever of the game's windows has the focus (the menu is paused meanwhile).
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
export MALI_DEFAULT_DISPLAY=wayland
# ROCKNIX's pad mapping puts the A button (east) on SDL's "a"; with --pad-position the game reads
# A as the east button, so swap a/b only. X and Y stay as they are.
DB=/storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt
if [ -f "$DB" ]; then
    SDL_GAMECONTROLLERCONFIG="$(grep "retrogame_joypad" "$DB" | sed -e "s/a:b0/a:b1/g" -e "s/b:b1/b:b0/g")"
    # A pad with an entry of its own (the XU Mini M's) gets the same swap.
    pad=$(sed -n 's/^N: Name="\(.*[Gg]amepad\)"$/\1/p' /proc/bus/input/devices | head -n 1)
    own=""
    [ -n "$pad" ] && own="$(grep -F ",$pad," "$DB" | sed -e 's/,a:/,@A:/' -e 's/,b:/,a:/' -e 's/,@A:/,b:/')"
    [ -n "$own" ] && SDL_GAMECONTROLLERCONFIG="$SDL_GAMECONTROLLERCONFIG
$own"
    export SDL_GAMECONTROLLERCONFIG
fi

# Two panels, or one? (If sway doesn't say, assume the RG DS.)
outputs=$(swaymsg -t get_outputs -r 2>/dev/null | jq -r '.[].name' 2>/dev/null || true)
UPPER=DSI-2
LOWER=DSI-1
layout=(--layout dual)
if [ -n "$outputs" ] && ! echo "$outputs" | grep -qx DSI-2; then
    UPPER=$(echo "$outputs" | head -n 1)
    LOWER=""
    size=$(swaymsg -t get_outputs -r 2>/dev/null | jq -r '.[0] | "\(.rect.width)x\(.rect.height)"' 2>/dev/null || true)
    layout=()
    case "$size" in [0-9]*x[0-9]*) layout+=(--res "$size") ;; esac
else
    export SDL_VIDEO_DISPLAY_PRIORITY=$UPPER,$LOWER
fi

lower_power=""
[ -n "$LOWER" ] && lower_power=$(swaymsg -t get_outputs -r 2>/dev/null | jq -r --arg o "$LOWER" '.[] | select(.name==$o) | .power' 2>/dev/null || true)
frontend_pids=$(pgrep -x emulationstation 2>/dev/null || true)
governors=""
focus_pid=""
restored=false
restore() {
    $restored && return
    restored=true
    [ -n "$focus_pid" ] && kill "$focus_pid" 2>/dev/null
    for item in $governors; do echo "${item#*:}" > "${item%%:*}/scaling_governor" 2>/dev/null || true; done
    for pid in $frontend_pids; do kill -CONT "$pid" 2>/dev/null || true; done
    # When the lower window closes, sway can leave focus (and the frontend itself) on the
    # lower panel, which is about to be switched off: the upper screen would stay black. Put
    # the frontend back on the upper panel (or the only one), full screen, and focus it there.
    sleep 0.3
    for es in '[app_id="emulationstation"]' '[title="EmulationStation"]'; do
        swaymsg "$es move container to output $UPPER" >/dev/null 2>&1 || continue
        swaymsg "focus output $UPPER" >/dev/null 2>&1
        swaymsg "$es focus" >/dev/null 2>&1
        swaymsg "$es fullscreen enable" >/dev/null 2>&1
        break
    done
    [ "$lower_power" = false ] && swaymsg output "$LOWER" power off >/dev/null 2>&1
    true
}
trap restore EXIT INT TERM HUP

for p in /sys/devices/system/cpu/cpufreq/policy*; do
    [ -w "$p/scaling_governor" ] || continue
    grep -qw performance "$p/scaling_available_governors" 2>/dev/null || continue
    governors="$governors $p:$(cat "$p/scaling_governor")"
    echo performance > "$p/scaling_governor"
done
for pid in $frontend_pids; do
    kill -STOP "$pid" 2>/dev/null || { echo "could not pause the frontend" >&2; exit 3; }
done
[ -n "$LOWER" ] && { swaymsg output "$LOWER" power on >/dev/null 2>&1 || true; }
swaymsg -q "for_window [title=\"^Raptor Sprint\$\"] border none, move window to output $UPPER, fullscreen enable" 2>/dev/null || true
[ -n "$LOWER" ] && { swaymsg -q "for_window [title=\"^Raptor Sprint lower\$\"] border none, move window to output $LOWER, fullscreen enable" 2>/dev/null || true; }
# Give the upper window the pad: keep focusing it for a moment once it appears.
(
    for _ in $(seq 1 100); do
        if swaymsg -t get_tree -r 2>/dev/null | jq -e '.. | objects | select(.name? == "Raptor Sprint")' >/dev/null 2>&1; then
            for _ in $(seq 1 25); do swaymsg '[title="^Raptor Sprint$"] focus' >/dev/null 2>&1; sleep 0.1; done
            exit 0
        fi
        sleep 0.1
    done
) &
focus_pid=$!

# Debugging: an empty file named diag.on next to the game logs frame times to log.txt.
args=("${layout[@]}" --pad-position "$@")
[ -f "$GAMEDIR/diag.on" ] && args+=(--diag)
cd "$GAMEDIR" || exit 1
./raptorsprint.aarch64 "${args[@]}" > log.txt 2>&1
