# FC5MP Game MCP Server

Agent-facing automation layer for the Far Cry 5 / fc5mp test rig.

## Run
```bash
python3 /home/kalopsia/fc5mp/mcp/fc5mp_game.py
```
Registered in `~/.pi/agent/mcp.json` as `fc5mp-game`.

## Key concepts
- **Monitor:** screenshots/clicks target `FC5MP_MONITOR` (default `DP-3`).
- **States:** `mcp/states.json` maps OCR keywords → game states. Edit to teach new screens.
- **Scenarios:** YAML files in `mcp/scenarios/`. Run with `run_scenario(name)`.

## Tool groups
- Server: `server_build`, `server_test`, `server_start`, `server_stop`, `server_logs`, `ensure_server`, `status`
- Game: `game_install`, `game_uninstall`, `game_launch`, `game_kill`, `game_wait_exit`
- Interaction: `screenshot`, `view_screenshot`, `click`, `double_click`, `move_mouse`, `press_key`, `type_text`, `wait_stable`
- Observation: `observe`, `classify_screen`, `ocr_screenshot`, `wait_for_state`, `wait_for_text`, `wait_for_process`
- Logs: `collect_logs`, `list_game_logs`, `read_log`, `grep_log`
- Scenarios: `list_scenarios`, `get_scenario`, `save_scenario`, `run_scenario`, `create_example_scenario`

## Scenario example
See `mcp/scenarios/login_smoke.yaml`.
