#!/usr/bin/env bash
# launch-capture.sh [host|guest] — stage the fc5mp proxy beside Far Cry 5 and
# print the Steam launch option. Creates NEW files only; never overwrites game
# files. The role (default: host) only names the capture file so the two halves
# of the protocol stay separate and labeled:
#   host  — UbiServices login, config fetch, RdV CreateSession, host P2P side
#   guest — login, the in-game JOIN flow, guest P2P side
set -euo pipefail

ROLE="${1:-host}"
case "$ROLE" in
  host|guest) ;;
  *) echo "usage: $0 [host|guest]  (default: host)"; exit 2 ;;
esac

BIN="/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin"
BUILD="$(cd "$(dirname "$0")/.." && pwd)/client/build-win"
REAL_VERSION="$(find /home/kalopsia/.local/share/Steam/steamapps/common/Proton* \
  -name version.dll -path '*x86_64-windows*' 2>/dev/null | head -1)"
CAP="$HOME/.fc5mp/captures/session-$ROLE.bin"

for f in version.dll fc5mp-client.dll; do
  [ -f "$BUILD/$f" ] || { echo "missing build artifact: $BUILD/$f (build client first)"; exit 1; }
done
[ -n "$REAL_VERSION" ] || { echo "could not find a Proton version.dll to forward to"; exit 1; }

echo "Staging into: $BIN   (role: $ROLE)"
echo "  (these are new files; the game's own files are untouched)"
cp -n "$BUILD/version.dll"       "$BIN/version.dll"
cp -n "$BUILD/fc5mp-client.dll"  "$BIN/fc5mp-client.dll"
cp -n "$REAL_VERSION"            "$BIN/version_real.dll"
mkdir -p "$HOME/.fc5mp/captures"

cat <<MSG

Staged. Now, in Steam:
  Far Cry 5 → Properties → Launch Options, paste:

    WINEDLLOVERRIDES="version=n,b" FC5MP_CAPTURE="$CAP" %command%

Then launch and, as the $ROLE:
MSG
if [ "$ROLE" = host ]; then
  cat <<'MSG'
  - create the co-op session and wait for your guest to join,
  - play a few minutes together, then quit.
MSG
else
  cat <<'MSG'
  - accept the host's invite / join their session (this captures the JOIN flow),
  - play a few minutes together, then quit.
MSG
fi
cat <<MSG
Capture lands at: $CAP
Run this once per machine, once per role. To remove the harness later:
  rm "$BIN/version.dll" "$BIN/fc5mp-client.dll" "$BIN/version_real.dll"
MSG
