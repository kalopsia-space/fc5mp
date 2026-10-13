#!/usr/bin/env bash
# launch-capture.sh — stage the fc5mp proxy beside Far Cry 5 and print the
# Steam launch option. Creates NEW files only; never overwrites game files.
set -euo pipefail
BIN="/home/kalopsia/.local/share/Steam/steamapps/common/FarCry5/bin"
BUILD="$(cd "$(dirname "$0")/.." && pwd)/client/build-win"
REAL_VERSION="$(find /home/kalopsia/.local/share/Steam/steamapps/common/Proton* \
  -name version.dll -path '*x86_64-windows*' 2>/dev/null | head -1)"

for f in version.dll fc5mp-client.dll; do
  [ -f "$BUILD/$f" ] || { echo "missing build artifact: $BUILD/$f (build client first)"; exit 1; }
done
[ -n "$REAL_VERSION" ] || { echo "could not find a Proton version.dll to forward to"; exit 1; }

echo "Staging into: $BIN"
echo "  (these are new files; the game's own files are untouched)"
cp -n "$BUILD/version.dll"       "$BIN/version.dll"
cp -n "$BUILD/fc5mp-client.dll"  "$BIN/fc5mp-client.dll"
cp -n "$REAL_VERSION"            "$BIN/version_real.dll"
mkdir -p "$HOME/.fc5mp/captures"

cat <<MSG

Staged. Now, in Steam:
  Far Cry 5 → Properties → Launch Options, paste:

    WINEDLLOVERRIDES="version=n,b" FC5MP_CAPTURE="$HOME/.fc5mp/captures/session.bin" %command%

Then launch, start a 2-player co-op session, play a few minutes, quit.
Captures land in ~/.fc5mp/captures/. To remove the harness later:
  rm "$BIN/version.dll" "$BIN/fc5mp-client.dll" "$BIN/version_real.dll"
MSG
