#!/bin/bash
# FC5MP Game Interaction Tool
# Provides screenshot, click, key, and type commands for automating game testing
set -euo pipefail

MONITOR="${FC5MP_MONITOR:-DP-3}"
# Resolve the monitor's position dynamically (layout changes when HDMI is off)
read -r MONITOR_X MONITOR_Y MONITOR_W MONITOR_H < <(hyprctl monitors -j 2>/dev/null | python3 -c '
import sys,json
for m in json.load(sys.stdin):
    if m["name"]==sys.argv[1]: print(m["x"],m["y"],m["width"],m["height"])' "$MONITOR" 2>/dev/null || echo "5760 0 1920 1080")
MONITOR_X=${MONITOR_X:-5760}; MONITOR_Y=${MONITOR_Y:-0}; MONITOR_W=${MONITOR_W:-1920}; MONITOR_H=${MONITOR_H:-1080}
SCRATCHPAD="/tmp/claude-1000/-home-kalopsia-fc5mp"
SCREENSHOT_DIR="$SCRATCHPAD/screenshots"
mkdir -p "$SCREENSHOT_DIR"

screenshot() {
    local name="${1:-screen}"
    local path="$SCREENSHOT_DIR/${name}-$(date +%s).png"
    grim -o "$MONITOR" "$path"
    echo "$path"
}

click() {
    local x=$1 y=$2
    local abs_x=$((MONITOR_X + x))
    local abs_y=$((MONITOR_Y + y))
    ydotool mousemove --absolute -x "$abs_x" -y "$abs_y"
    sleep 0.05
    ydotool click 0xC0
}

doubleclick() {
    local x=$1 y=$2
    local abs_x=$((MONITOR_X + x))
    local abs_y=$((MONITOR_Y + y))
    ydotool mousemove --absolute -x "$abs_x" -y "$abs_y"
    sleep 0.05
    ydotool click 0xC0
    sleep 0.1
    ydotool click 0xC0
}

moveto() {
    local x=$1 y=$2
    local abs_x=$((MONITOR_X + x))
    local abs_y=$((MONITOR_Y + y))
    ydotool mousemove --absolute -x "$abs_x" -y "$abs_y"
}

keypress() {
    ydotool key "$@"
}

typetext() {
    wtype "$@"
}

wait_stable() {
    local interval="${1:-2}"
    local prev="$SCREENSHOT_DIR/_wait_prev.png"
    local curr="$SCREENSHOT_DIR/_wait_curr.png"
    grim -o "$MONITOR" "$prev"
    while true; do
        sleep "$interval"
        grim -o "$MONITOR" "$curr"
        if cmp -s "$prev" "$curr" 2>/dev/null; then
            echo "stable"
            rm -f "$prev" "$curr"
            return 0
        fi
        mv "$curr" "$prev"
    done
}

case "${1:-help}" in
    screenshot|ss)
        screenshot "${2:-screen}"
        ;;
    click)
        click "$2" "$3"
        echo "clicked ($2, $3)"
        ;;
    doubleclick|dblclick)
        doubleclick "$2" "$3"
        echo "double-clicked ($2, $3)"
        ;;
    move)
        moveto "$2" "$3"
        echo "moved to ($2, $3)"
        ;;
    key)
        shift
        keypress "$@"
        echo "pressed key $*"
        ;;
    type)
        shift
        typetext "$@"
        echo "typed: $*"
        ;;
    wait-stable)
        wait_stable "${2:-2}"
        ;;
    enter)
        keypress 28:1 28:0
        echo "pressed Enter"
        ;;
    escape|esc)
        keypress 1:1 1:0
        echo "pressed Escape"
        ;;
    space)
        keypress 57:1 57:0
        echo "pressed Space"
        ;;
    help|*)
        echo "Usage: $0 <command> [args]"
        echo ""
        echo "Commands:"
        echo "  screenshot [name]     Take screenshot, print path"
        echo "  click X Y             Click at monitor-relative coords"
        echo "  doubleclick X Y       Double-click at coords"
        echo "  move X Y              Move mouse to coords"
        echo "  key KEYCODE           Press key (ydotool keycode)"
        echo "  type TEXT             Type text string"
        echo "  enter                 Press Enter"
        echo "  escape                Press Escape"
        echo "  space                 Press Space"
        echo "  wait-stable [secs]    Wait until screen stops changing"
        echo ""
        echo "Monitor: $MONITOR at ${MONITOR_X},${MONITOR_Y} (${MONITOR_W}x${MONITOR_H})"
        echo "Coords are relative to monitor top-left (0,0 to ${MONITOR_W},${MONITOR_H})"
        ;;
esac
