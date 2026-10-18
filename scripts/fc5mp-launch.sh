#!/bin/bash
# FC5MP launcher — switches vanilla game dir between clean and modded mode
set -euo pipefail

GAME_BIN="$HOME/.local/share/Steam/steamapps/common/FarCry5/bin"
CRACK_SRC="$HOME/fc5mp/FarCry5/bin/crack_backup"
BUILD="$HOME/fc5mp/client/build-win"
PROTON_DIR="$HOME/.local/share/Steam/compatibilitytools.d/proton-cachyos-11.0-20261005-slr-x86_64_v3"
WINE="$PROTON_DIR/files/bin/wine"
WINEPREFIX="$HOME/.local/share/Steam/steamapps/compatdata/552520/pfx"
BACKUP_DIR="$GAME_BIN/.vanilla_backup"
SERVER_BIN="$HOME/fc5mp/server"
REDIRECT_CONF="$WINEPREFIX/drive_c/users/steamuser/.fc5mp/redirect.toml"

install_steam() {
    mkdir -p "$BACKUP_DIR"

    for f in uplay_r1_loader64.dll dbdata.dll; do
        [ -f "$GAME_BIN/$f" ] && [ ! -f "$BACKUP_DIR/$f" ] && cp "$GAME_BIN/$f" "$BACKUP_DIR/$f"
    done

    cp "$BUILD/version.dll"              "$GAME_BIN/"
    cp "$BUILD/fc5mp-client.dll"         "$GAME_BIN/"
    cp "$BUILD/uplay_r1_loader64.dll"    "$GAME_BIN/"
    cp "$BUILD/uplay_aux_r164.dll"       "$GAME_BIN/"
    cp "$BUILD/dbdata.dll"               "$GAME_BIN/"

    cp "$PROTON_DIR/files/lib/wine/x86_64-windows/version.dll" "$GAME_BIN/version_real.dll"

    # Write redirect config (hooks read this to know where to send traffic)
    mkdir -p "$(dirname "$REDIRECT_CONF")"
    cat > "$REDIRECT_CONF" <<REDIR
server_ip = "127.0.0.1"
prudp_port = 6570
rest_port = 21000
REDIR

    echo "[fc5mp] Steam-mode DLLs installed (stubs + hooks, no crack)"
}

install_crack() {
    mkdir -p "$BACKUP_DIR"

    for f in uplay_r1_loader64.dll dbdata.dll; do
        [ ! -f "$BACKUP_DIR/$f" ] && cp "$GAME_BIN/$f" "$BACKUP_DIR/$f"
    done

    cp "$CRACK_SRC/winmm.dll"              "$GAME_BIN/"
    cp "$CRACK_SRC/0xZeOn.dll"             "$GAME_BIN/"
    cp "$CRACK_SRC/steam_api64.dll"        "$GAME_BIN/"
    cp "$CRACK_SRC/steam_emu.ini"          "$GAME_BIN/"
    cp "$CRACK_SRC/uplay_r1_loader.ini"    "$GAME_BIN/"
    cp "$CRACK_SRC/uplay_r1_loader64.dll"  "$GAME_BIN/"
    cp "$CRACK_SRC/dbdata.dll"             "$GAME_BIN/"

    cp "$BUILD/version.dll"       "$GAME_BIN/"
    cp "$BUILD/fc5mp-client.dll"  "$GAME_BIN/"

    cp "$PROTON_DIR/files/lib/wine/x86_64-windows/version.dll" "$GAME_BIN/version_real.dll"
    mkdir -p "$GAME_BIN/SAVES/uplay_emu/0xZeOn/1803"

    echo "[fc5mp] Crack-mode DLLs installed"
}

uninstall_mod() {
    if [ -d "$BACKUP_DIR" ]; then
        cp "$BACKUP_DIR/uplay_r1_loader64.dll" "$GAME_BIN/"
        cp "$BACKUP_DIR/dbdata.dll"             "$GAME_BIN/"
    fi

    rm -f "$GAME_BIN/winmm.dll" "$GAME_BIN/0xZeOn.dll"
    rm -f "$GAME_BIN/steam_api64.dll" "$GAME_BIN/steam_emu.ini" "$GAME_BIN/uplay_r1_loader.ini"
    rm -f "$GAME_BIN/version.dll" "$GAME_BIN/version_real.dll" "$GAME_BIN/fc5mp-client.dll"
    rm -f "$GAME_BIN/uplay_aux_r164.dll"
    rm -f "$GAME_BIN/uplay_stub.log" "$GAME_BIN/dbdata_stub.log" "$GAME_BIN/fc5mp-tls-trace.txt"
    rm -f "$GAME_BIN/fc5mp-skip-diag.txt"

    echo "[fc5mp] Vanilla restored"
}

collect_logs() {
    local LOG_DIR="/tmp/fc5mp-logs-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$LOG_DIR"
    for f in uplay_stub.log dbdata_stub.log fc5mp-hooks-diag.txt \
             fc5mp-redirect-diag.txt fc5mp-connect-trace.txt \
             fc5mp-tls-trace.txt fc5mp-skip-diag.txt; do
        [ -f "$GAME_BIN/$f" ] && cp "$GAME_BIN/$f" "$LOG_DIR/"
    done
    [ -f /tmp/fc5mp-server.log ] && cp /tmp/fc5mp-server.log "$LOG_DIR/"
    echo "[fc5mp] Logs collected in $LOG_DIR"
}

case "${1:-steam}" in
    steam)
        install_steam
        echo "[fc5mp] Steam mode — launch FC5 from Steam (app 552520)"
        echo "[fc5mp] Set these Steam launch options for Far Cry 5:"
        echo '  WINEDLLOVERRIDES="uplay_r1_loader64=n;uplay_aux_r164=n;dbdata=n;version=n" %command%'
        ;;
    run)
        install_steam
        echo "[fc5mp] Launching via Wine (Steam mode)..."
        cd "$GAME_BIN"
        WINEPREFIX="$WINEPREFIX" \
        WINEESYNC=1 WINEFSYNC=1 \
        WINEDLLOVERRIDES="uplay_r1_loader64=n;uplay_aux_r164=n;dbdata=n;version=n" \
        "$WINE" FarCry5.exe "${@:2}"
        echo "[fc5mp] Game exited"
        collect_logs
        uninstall_mod
        ;;
    crack)
        install_crack
        echo "[fc5mp] Crack mode — launching (WARNING: Granite hang likely)..."
        cd "$GAME_BIN"
        WINEPREFIX="$WINEPREFIX" \
        WINEESYNC=1 WINEFSYNC=1 \
        WINEDLLOVERRIDES="winmm=n;uplay_r1_loader64=n;dbdata=n;steam_api64=n;version=n;d3d11=n;dxgi=n" \
        "$WINE" FarCry5.exe "${@:2}"
        echo "[fc5mp] Game exited"
        collect_logs
        uninstall_mod
        ;;
    install)
        install_steam
        ;;
    uninstall)
        uninstall_mod
        ;;
    logs)
        collect_logs
        ;;
    server)
        echo "[fc5mp] Starting server..."
        cd "$SERVER_BIN"
        cargo run --release 2>&1 | tee /tmp/fc5mp-server.log
        ;;
    full)
        echo "[fc5mp] Starting server in background..."
        cd "$SERVER_BIN"
        cargo run --release 2>&1 | tee /tmp/fc5mp-server.log &
        SERVER_PID=$!
        sleep 3
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "[fc5mp] Server failed to start!"
            exit 1
        fi
        echo "[fc5mp] Server running (PID $SERVER_PID)"
        install_steam
        cd "$GAME_BIN"
        echo "[fc5mp] Launching via Wine..."
        WINEPREFIX="$WINEPREFIX" \
        WINEESYNC=1 WINEFSYNC=1 \
        WINEDLLOVERRIDES="uplay_r1_loader64=n;uplay_aux_r164=n;dbdata=n;version=n" \
        "$WINE" FarCry5.exe "${@:2}" || true
        echo "[fc5mp] Game exited"
        collect_logs
        uninstall_mod
        kill "$SERVER_PID" 2>/dev/null || true
        ;;
    *)
        echo "Usage: $0 [steam|run|crack|install|uninstall|logs|server|full]"
        echo "  steam     Install DLLs + print Steam launch instructions (default)"
        echo "  run       Install DLLs + launch directly via Wine"
        echo "  crack     Install crack DLLs + launch (deprecated — Granite hang)"
        echo "  install   Install DLLs only"
        echo "  uninstall Restore vanilla game"
        echo "  logs      Collect all fc5mp log files"
        echo "  server    Start fc5mp-server only"
        echo "  full      Start server + install + launch (all-in-one)"
        exit 1
        ;;
esac
