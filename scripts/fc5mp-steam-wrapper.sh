#!/usr/bin/env bash
# fc5mp-steam-wrapper.sh — Steam launch wrapper for Far Cry 5.
#
# Steam launch option (set once):
#   /home/kalopsia/fc5mp/scripts/fc5mp-steam-wrapper.sh %command%
#
# Everything else is controlled from ~/.fc5mp/launch.env, which can be edited
# between launches without touching Steam. Lines there are `KEY=value` and are
# exported into the game's environment (WINEDLLOVERRIDES, PROTON_LOG, LD_PRELOAD,
# FC5MP_CAPTURE, ...). FC5MP_DISABLED=1 launches the vanilla game untouched.
set -u
ENV_FILE="$HOME/.fc5mp/launch.env"
LOG="$HOME/.fc5mp/wrapper.log"
mkdir -p "$HOME/.fc5mp"

{
  echo "=== $(date '+%F %T') wrapper invoked"
  echo "argv: $*"
} >> "$LOG"

if [ -f "$ENV_FILE" ]; then
  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in ''|'#'*) continue ;; esac
    key="${line%%=*}"; val="${line#*=}"
    # strip one layer of surrounding quotes
    case "$val" in \"*\") val="${val#\"}"; val="${val%\"}" ;; \'*\') val="${val#\'}"; val="${val%\'}" ;; esac
    export "$key=$val"
    echo "env: $key=$val" >> "$LOG"
  done < "$ENV_FILE"
fi

if [ "${FC5MP_DISABLED:-0}" = "1" ]; then
  unset WINEDLLOVERRIDES LD_PRELOAD
  echo "FC5MP_DISABLED=1 -> vanilla launch" >> "$LOG"
fi

exec "$@"
