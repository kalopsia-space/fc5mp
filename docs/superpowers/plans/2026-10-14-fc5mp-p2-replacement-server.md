# P2 Replacement Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Rust server that emulates Ubisoft's login + matchmaking services so Far Cry 5 connects to us for co-op session setup.

**Architecture:** Single async binary with two modules — an HTTP server (axum) serving the REST endpoints the game hits during login/config, and a UDP server speaking PRUDP v0 / NEX for matchmaking. Shared in-memory state tracks players and sessions. No TLS needed (the injected DLL redirects before encryption).

**Tech Stack:** Rust, tokio, axum, serde/serde_json, toml, clap, tracing, hmac/md-5, uuid, rand

**Spec:** `docs/superpowers/specs/2026-10-14-p2-replacement-server-design.md`

## Global Constraints

- Rust 2021 edition, stable toolchain
- No external services (no database, no Redis, no TLS stack)
- All state is in-memory; resets on server restart
- Server listens on configurable ports (defaults: REST 21000, PRUDP 6570)
- PRUDP v0 packet format follows Kinnay's NEX documentation and Pretendo prior art
- Quazal Kerberos uses HMAC-MD5 (legacy crypto matching the game's expectation)
- `tracing` structured logging on every PRUDP packet and RMC call (critical for iterative development)

## Review Focus

1. **Malformed PRUDP packets** (truncated, wrong checksum, unknown type) — the server must log and drop them without crashing, since real network traffic includes garbage.
2. **Concurrent player registration** — two players calling login/CONNECT simultaneously must not corrupt shared state; `Arc<RwLock>` protects the registries.
3. **Connection cleanup on silent disconnect** — if a client vanishes without DISCONNECT, the connection entry must eventually be reaped (a periodic sweep or inactivity timeout).
4. **REST requests with unexpected Content-Type or missing headers** — the game always sends specific headers, but the server should not panic on curl/testing requests that omit them.
5. **PRUDP sequence number wraparound** — u16 wraps at 65535; the reliability layer must handle this correctly in ack tracking.

---

### Task 1: Project scaffold, config, and server startup

**Files:**
- Create: `server/Cargo.toml`
- Create: `server/src/main.rs`
- Create: `server/src/config.rs`

**Interfaces:**
- Consumes: nothing (foundation task)
- Produces: `Config` struct with fields `listen_ip: IpAddr`, `rest_port: u16`, `prudp_port: u16`, `max_players: u8`; `Config::load(path: Option<&Path>) -> Result<Config>`; CLI override via clap

- [ ] **Step 1: Create `server/Cargo.toml`**

```toml
[package]
name = "fc5mp-server"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio = { version = "1.38", features = ["full"] }
axum = "0.8"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
toml = "0.8"
tracing = "0.1"
tracing-subscriber = { version = "0.3", features = ["env-filter"] }
hmac = "0.12"
md-5 = "0.10"
uuid = { version = "1", features = ["v4"] }
clap = { version = "4", features = ["derive"] }
rand = "0.8"

[dev-dependencies]
reqwest = { version = "0.12", features = ["json"] }
tokio-test = "0.4"
```

- [ ] **Step 2: Write the config test**

Create `server/src/config.rs`:

```rust
use clap::Parser;
use serde::Deserialize;
use std::net::IpAddr;
use std::path::Path;

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct Config {
    pub listen_ip: IpAddr,
    pub rest_port: u16,
    pub prudp_port: u16,
    pub max_players: u8,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            listen_ip: "0.0.0.0".parse().unwrap(),
            rest_port: 21000,
            prudp_port: 6570,
            max_players: 4,
        }
    }
}

#[derive(Parser, Debug)]
#[command(name = "fc5mp-server")]
pub struct Cli {
    #[arg(long)]
    pub config: Option<std::path::PathBuf>,
    #[arg(long)]
    pub rest_port: Option<u16>,
    #[arg(long)]
    pub prudp_port: Option<u16>,
    #[arg(long)]
    pub listen_ip: Option<IpAddr>,
}

impl Config {
    pub fn load(path: Option<&Path>) -> anyhow::Result<Self> {
        let mut cfg = if let Some(p) = path {
            let text = std::fs::read_to_string(p)?;
            toml::from_str(&text)?
        } else {
            Self::default()
        };
        let cli = Cli::try_parse().unwrap_or_else(|_| Cli {
            config: None,
            rest_port: None,
            prudp_port: None,
            listen_ip: None,
        });
        if let Some(v) = cli.rest_port { cfg.rest_port = v; }
        if let Some(v) = cli.prudp_port { cfg.prudp_port = v; }
        if let Some(v) = cli.listen_ip { cfg.listen_ip = v; }
        Ok(cfg)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults() {
        let cfg = Config::default();
        assert_eq!(cfg.rest_port, 21000);
        assert_eq!(cfg.prudp_port, 6570);
        assert_eq!(cfg.max_players, 4);
        assert_eq!(cfg.listen_ip, "0.0.0.0".parse::<IpAddr>().unwrap());
    }

    #[test]
    fn from_toml() {
        let text = r#"
            listen_ip = "127.0.0.1"
            rest_port = 8080
            prudp_port = 7000
            max_players = 2
        "#;
        let cfg: Config = toml::from_str(text).unwrap();
        assert_eq!(cfg.rest_port, 8080);
        assert_eq!(cfg.prudp_port, 7000);
        assert_eq!(cfg.max_players, 2);
        assert_eq!(cfg.listen_ip, "127.0.0.1".parse::<IpAddr>().unwrap());
    }

    #[test]
    fn partial_toml_uses_defaults() {
        let text = r#"rest_port = 9999"#;
        let cfg: Config = toml::from_str(text).unwrap();
        assert_eq!(cfg.rest_port, 9999);
        assert_eq!(cfg.prudp_port, 6570);
    }
}
```

Note: add `anyhow = "1"` to `[dependencies]` in Cargo.toml.

- [ ] **Step 3: Run tests**

```bash
cd server && cargo test --lib config::tests
```

Expected: 3 tests pass.

- [ ] **Step 4: Write `main.rs` skeleton**

```rust
mod config;

use config::Config;
use tracing_subscriber::EnvFilter;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env().add_directive("fc5mp_server=info".parse()?))
        .init();

    let cli = <config::Cli as clap::Parser>::parse();
    let cfg = Config::load(cli.config.as_deref())?;

    tracing::info!(
        rest = %format!("{}:{}", cfg.listen_ip, cfg.rest_port),
        prudp = %format!("{}:{}", cfg.listen_ip, cfg.prudp_port),
        max_players = cfg.max_players,
        "fc5mp-server starting"
    );

    // REST and PRUDP servers will be added in later tasks
    tracing::info!("server ready");
    tokio::signal::ctrl_c().await?;
    tracing::info!("shutting down");
    Ok(())
}
```

- [ ] **Step 5: Verify it compiles and runs**

```bash
cd server && cargo run -- --rest-port 21000
```

Expected: prints startup log with addresses, waits for Ctrl+C.

- [ ] **Step 6: Commit**

```bash
git add server/
git commit -m "feat(p2): project scaffold with config parsing and CLI overrides"
```

---

### Task 2: Shared state registries

**Files:**
- Create: `server/src/state.rs`
- Modify: `server/src/main.rs` (add `mod state;`)

**Interfaces:**
- Consumes: nothing
- Produces: `ServerState` (wraps `Arc<RwLock<Inner>>`), `ServerState::new(max_players: u8) -> Self`, `ServerState::add_player(&self, id: Uuid, name: String) -> Result<(), StateError>`, `ServerState::remove_player(&self, id: &Uuid)`, `ServerState::create_session(&self, host_id: Uuid) -> Result<Uuid, StateError>`, `ServerState::join_session(&self, session_id: &Uuid, player_id: Uuid) -> Result<(), StateError>`, `ServerState::get_session(&self, id: &Uuid) -> Option<Session>`

- [ ] **Step 1: Write state tests and implementation**

Create `server/src/state.rs`:

```rust
use std::collections::HashMap;
use std::sync::{Arc, RwLock};
use uuid::Uuid;

#[derive(Debug, Clone)]
pub struct Player {
    pub id: Uuid,
    pub name: String,
}

#[derive(Debug, Clone)]
pub struct Session {
    pub id: Uuid,
    pub host_id: Uuid,
    pub players: Vec<Uuid>,
    pub max_players: u8,
}

#[derive(Debug, thiserror::Error)]
pub enum StateError {
    #[error("server full")]
    ServerFull,
    #[error("session not found")]
    SessionNotFound,
    #[error("session full")]
    SessionFull,
    #[error("player not found")]
    PlayerNotFound,
}

struct Inner {
    players: HashMap<Uuid, Player>,
    sessions: HashMap<Uuid, Session>,
    max_players: u8,
}

#[derive(Clone)]
pub struct ServerState(Arc<RwLock<Inner>>);

impl ServerState {
    pub fn new(max_players: u8) -> Self {
        Self(Arc::new(RwLock::new(Inner {
            players: HashMap::new(),
            sessions: HashMap::new(),
            max_players,
        })))
    }

    pub fn add_player(&self, id: Uuid, name: String) -> Result<(), StateError> {
        let mut inner = self.0.write().unwrap();
        inner.players.insert(id, Player { id, name });
        Ok(())
    }

    pub fn remove_player(&self, id: &Uuid) {
        let mut inner = self.0.write().unwrap();
        inner.players.remove(id);
        inner.sessions.retain(|_, s| {
            s.players.retain(|p| p != id);
            s.host_id != *id
        });
    }

    pub fn get_player(&self, id: &Uuid) -> Option<Player> {
        self.0.read().unwrap().players.get(id).cloned()
    }

    pub fn create_session(&self, host_id: Uuid) -> Result<Uuid, StateError> {
        let mut inner = self.0.write().unwrap();
        if !inner.players.contains_key(&host_id) {
            return Err(StateError::PlayerNotFound);
        }
        let id = Uuid::new_v4();
        inner.sessions.insert(id, Session {
            id,
            host_id,
            players: vec![host_id],
            max_players: inner.max_players,
        });
        Ok(id)
    }

    pub fn join_session(&self, session_id: &Uuid, player_id: Uuid) -> Result<(), StateError> {
        let mut inner = self.0.write().unwrap();
        let session = inner.sessions.get_mut(session_id)
            .ok_or(StateError::SessionNotFound)?;
        if session.players.len() >= session.max_players as usize {
            return Err(StateError::SessionFull);
        }
        if !session.players.contains(&player_id) {
            session.players.push(player_id);
        }
        Ok(())
    }

    pub fn get_session(&self, id: &Uuid) -> Option<Session> {
        self.0.read().unwrap().sessions.get(id).cloned()
    }

    pub fn list_sessions(&self) -> Vec<Session> {
        self.0.read().unwrap().sessions.values().cloned().collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_and_get_player() {
        let state = ServerState::new(4);
        let id = Uuid::new_v4();
        state.add_player(id, "alice".into()).unwrap();
        let p = state.get_player(&id).unwrap();
        assert_eq!(p.name, "alice");
    }

    #[test]
    fn remove_player_cleans_sessions() {
        let state = ServerState::new(4);
        let host = Uuid::new_v4();
        state.add_player(host, "host".into()).unwrap();
        let sid = state.create_session(host).unwrap();
        assert!(state.get_session(&sid).is_some());
        state.remove_player(&host);
        assert!(state.get_session(&sid).is_none());
    }

    #[test]
    fn join_session_respects_max() {
        let state = ServerState::new(2);
        let host = Uuid::new_v4();
        let g1 = Uuid::new_v4();
        let g2 = Uuid::new_v4();
        state.add_player(host, "h".into()).unwrap();
        state.add_player(g1, "g1".into()).unwrap();
        state.add_player(g2, "g2".into()).unwrap();
        let sid = state.create_session(host).unwrap();
        state.join_session(&sid, g1).unwrap();
        assert!(state.join_session(&sid, g2).is_err());
    }

    #[test]
    fn create_session_requires_player() {
        let state = ServerState::new(4);
        let ghost = Uuid::new_v4();
        assert!(state.create_session(ghost).is_err());
    }
}
```

Note: add `thiserror = "1"` to `[dependencies]` in Cargo.toml.

- [ ] **Step 2: Add `mod state;` to main.rs**

- [ ] **Step 3: Run tests**

```bash
cd server && cargo test --lib state::tests
```

Expected: 4 tests pass.

- [ ] **Step 4: Commit**

```bash
git add server/src/state.rs server/src/main.rs server/Cargo.toml
git commit -m "feat(p2): shared state registries for players and sessions"
```

---

### Task 3: REST login endpoint

**Files:**
- Create: `server/src/rest/mod.rs`
- Create: `server/src/rest/handlers.rs`
- Modify: `server/src/main.rs` (add `mod rest;`, start HTTP server)

**Interfaces:**
- Consumes: `ServerState` from Task 2, `Config` from Task 1
- Produces: `rest::router(state: ServerState) -> axum::Router` (Task 4 adds a `config` parameter), `POST /v3/profiles/sessions` returning JSON with fields `platformType`, `ticket`, `profileId`, `sessionId`, `sessionKey`, `spaceId`, `nameOnPlatform`, `expiration`, `serverTime`

The exact response format will be refined by examining the captured TLS dump (`capdump.py --dump`), but the structure below matches the known UbiConnect session response schema. The game parses specific fields from this JSON; if it rejects our response, we iterate.

- [ ] **Step 1: Create `server/src/rest/handlers.rs`**

```rust
use axum::extract::State;
use axum::http::StatusCode;
use axum::Json;
use serde_json::{json, Value};
use uuid::Uuid;

use crate::state::ServerState;

pub async fn login(State(state): State<ServerState>) -> (StatusCode, Json<Value>) {
    let profile_id = Uuid::new_v4();
    let session_id = Uuid::new_v4();
    let session_key = base64_rand(32);
    let ticket = base64_rand(64);

    let name = format!("fc5mp-player-{}", &profile_id.to_string()[..8]);
    if let Err(e) = state.add_player(profile_id, name.clone()) {
        tracing::error!(%e, "login failed");
        return (StatusCode::INTERNAL_SERVER_ERROR, Json(json!({"error": e.to_string()})));
    }

    tracing::info!(%profile_id, %name, "player logged in");

    let body = json!({
        "platformType": "uplay",
        "ticket": ticket,
        "profileId": profile_id.to_string(),
        "userId": profile_id.to_string(),
        "sessionId": session_id.to_string(),
        "sessionKey": session_key,
        "nameOnPlatform": name,
        "spaceId": "f40e304d-8e8d-4343-8270-d06487c35add",
        "environment": "prod",
        "expiration": "2030-01-01T00:00:00Z",
        "serverTime": chrono_now_iso(),
        "clientIp": "127.0.0.1",
        "clientIpCountry": "XX",
    });
    (StatusCode::OK, Json(body))
}

fn base64_rand(len: usize) -> String {
    use rand::RngCore;
    let mut buf = vec![0u8; len];
    rand::thread_rng().fill_bytes(&mut buf);
    base64_encode(&buf)
}

fn base64_encode(data: &[u8]) -> String {
    use std::fmt::Write;
    const CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity((data.len() + 2) / 3 * 4);
    for chunk in data.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = chunk.get(1).copied().unwrap_or(0) as u32;
        let b2 = chunk.get(2).copied().unwrap_or(0) as u32;
        let triple = (b0 << 16) | (b1 << 8) | b2;
        out.push(CHARS[((triple >> 18) & 0x3F) as usize] as char);
        out.push(CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 { out.push(CHARS[((triple >> 6) & 0x3F) as usize] as char); } else { out.push('='); }
        if chunk.len() > 2 { out.push(CHARS[(triple & 0x3F) as usize] as char); } else { out.push('='); }
    }
    out
}

fn chrono_now_iso() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs();
    format!("2026-01-01T00:00:{}Z", secs % 86400)
}
```

Note: the `chrono_now_iso` helper is intentionally approximate — the game may not validate the timestamp format strictly. If it does, refine during iteration. Alternatively, add the `time` crate for proper formatting.

- [ ] **Step 2: Create `server/src/rest/mod.rs`**

```rust
mod handlers;

use axum::{routing::{get, post}, Router};
use crate::state::ServerState;

pub fn router(state: ServerState) -> Router {
    Router::new()
        .route("/v3/profiles/sessions", post(handlers::login))
        .with_state(state)
}
```

- [ ] **Step 3: Wire into main.rs**

Add `mod rest;` and start the HTTP server in the main function:

```rust
// In main(), after config loading:
let state = state::ServerState::new(cfg.max_players);
let rest_addr = std::net::SocketAddr::new(cfg.listen_ip, cfg.rest_port);
let app = rest::router(state.clone());
let listener = tokio::net::TcpListener::bind(rest_addr).await?;
tracing::info!(%rest_addr, "REST server listening");
tokio::spawn(async move {
    axum::serve(listener, app).await.unwrap();
});
```

- [ ] **Step 4: Write integration test**

Create `server/tests/rest_login.rs`:

```rust
use reqwest::Client;
use serde_json::Value;
use tokio::net::TcpListener;

#[tokio::test]
async fn login_returns_session_ticket() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    let state = fc5mp_server::state::ServerState::new(4);
    let app = fc5mp_server::rest::router(state);
    tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });

    let resp = Client::new()
        .post(format!("http://{addr}/v3/profiles/sessions"))
        .send().await.unwrap();
    assert_eq!(resp.status(), 200);

    let body: Value = resp.json().await.unwrap();
    assert_eq!(body["platformType"], "uplay");
    assert!(body["profileId"].as_str().unwrap().len() > 0);
    assert!(body["ticket"].as_str().unwrap().len() > 0);
    assert!(body["sessionKey"].as_str().unwrap().len() > 0);
    assert_eq!(body["spaceId"], "f40e304d-8e8d-4343-8270-d06487c35add");
}
```

Note: for the integration test to use `fc5mp_server::state` and `fc5mp_server::rest`, make those modules `pub` in `main.rs` (or use a `lib.rs`). The cleanest approach: create `server/src/lib.rs` that declares the public modules, and have `main.rs` use `fc5mp_server::*`.

- [ ] **Step 5: Run integration test**

```bash
cd server && cargo test --test rest_login
```

Expected: PASS — login returns valid JSON with all expected fields.

- [ ] **Step 6: Commit**

```bash
git add server/src/rest/ server/src/main.rs server/src/lib.rs server/tests/
git commit -m "feat(p2): REST login endpoint returning fake session ticket"
```

---

### Task 4: REST configuration and router endpoints

**Files:**
- Modify: `server/src/rest/handlers.rs` (add `rendezvous_config`, `storm_router`)
- Modify: `server/src/rest/mod.rs` (add routes)

**Interfaces:**
- Consumes: `Config` from Task 1 (needs `listen_ip`, `prudp_port`)
- Produces: `GET /v1/spaces/{space_id}/sandboxes/{sandbox}/rendezvous/configuration` and `GET /v1/router/19` returning JSON with `prudp_loadbalancer_url` pointing at our server and `public_key` containing our NEX key blob

The response format is known from the captured TLS traffic (see protocol map §1):

```json
{
    "http_urls": [],
    "prudp_loadbalancer_url": "prudp:/address=<our_ip>;port=<our_port>",
    "prudp_urls": [],
    "http_loadbalancer_url": "",
    "public_key": "<base64 ECS1 key blob>"
}
```

- [ ] **Step 1: Add a REST-specific config extractor**

The handlers need access to the server's address info. Add a `RestContext` that carries both state and config:

In `server/src/rest/mod.rs`, change the router to accept config:

```rust
use crate::config::Config;

#[derive(Clone)]
pub struct RestContext {
    pub state: ServerState,
    pub prudp_host: String,
    pub prudp_port: u16,
}

pub fn router(state: ServerState, config: &Config) -> Router {
    let ctx = RestContext {
        state,
        prudp_host: config.listen_ip.to_string(),
        prudp_port: config.prudp_port,
    };
    Router::new()
        .route("/v3/profiles/sessions", post(handlers::login))
        .route("/v1/spaces/:space_id/sandboxes/:sandbox/rendezvous/configuration",
               get(handlers::rendezvous_config))
        .route("/v1/router/19", get(handlers::storm_router))
        .with_state(ctx)
}
```

Update handlers to use `State<RestContext>` instead of `State<ServerState>`.

- [ ] **Step 2: Add the rendezvous configuration handler**

In `server/src/rest/handlers.rs`:

```rust
pub async fn rendezvous_config(State(ctx): State<RestContext>) -> Json<Value> {
    let prudp_url = format!(
        "prudp:/address={};port={}",
        ctx.prudp_host, ctx.prudp_port
    );
    tracing::info!(%prudp_url, "serving rendezvous configuration");
    Json(json!({
        "http_urls": [],
        "prudp_loadbalancer_url": prudp_url,
        "prudp_urls": [],
        "http_loadbalancer_url": "",
        "public_key": crate::prudp::kerberos::public_key_blob_b64(),
    }))
}
```

Note: `crate::prudp::kerberos::public_key_blob_b64()` will be implemented in Task 7. For now, create a stub in `server/src/prudp/kerberos.rs` that returns a hardcoded test key: `pub fn public_key_blob_b64() -> String { "RUNTMSAAAAAAAAtest".to_string() }`. Task 7 replaces this stub with the real implementation. The test below validates the response shape, not the key content.

- [ ] **Step 3: Add the storm router handler**

```rust
pub async fn storm_router() -> Json<Value> {
    Json(json!({"router": "127.0.0.1:12000"}))
}
```

- [ ] **Step 4: Write integration test**

Add to `server/tests/rest_login.rs` (or create `server/tests/rest_config.rs`):

```rust
#[tokio::test]
async fn rendezvous_config_returns_our_prudp_address() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    let state = fc5mp_server::state::ServerState::new(4);
    let config = fc5mp_server::config::Config {
        listen_ip: "10.0.0.5".parse().unwrap(),
        prudp_port: 6570,
        ..Default::default()
    };
    let app = fc5mp_server::rest::router(state, &config);
    tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });

    let resp = Client::new()
        .get(format!(
            "http://{addr}/v1/spaces/f40e304d/sandboxes/FCZ_PC_A/rendezvous/configuration"
        ))
        .send().await.unwrap();
    let body: Value = resp.json().await.unwrap();
    assert_eq!(
        body["prudp_loadbalancer_url"],
        "prudp:/address=10.0.0.5;port=6570"
    );
    assert!(body["public_key"].as_str().unwrap().len() > 0);
}

#[tokio::test]
async fn storm_router_returns_address() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    let state = fc5mp_server::state::ServerState::new(4);
    let config = fc5mp_server::config::Config::default();
    let app = fc5mp_server::rest::router(state, &config);
    tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });

    let resp = Client::new()
        .get(format!("http://{addr}/v1/router/19"))
        .send().await.unwrap();
    let body: Value = resp.json().await.unwrap();
    assert!(body["router"].as_str().unwrap().contains(":12000"));
}
```

- [ ] **Step 5: Run tests**

```bash
cd server && cargo test --test rest_config
```

Expected: both tests pass.

- [ ] **Step 6: Commit**

```bash
git add server/src/rest/
git commit -m "feat(p2): REST rendezvous configuration and storm router endpoints"
```

---

### Task 5: PRUDP v0 packet codec

**Files:**
- Create: `server/src/prudp/mod.rs`
- Create: `server/src/prudp/transport.rs`

**Interfaces:**
- Consumes: nothing (standalone codec)
- Produces: `Packet { source: u8, dest: u8, packet_type: PacketType, flags: u8, session_id: u8, signature: u32, sequence: u16, payload: Vec<u8> }`, `PacketType` enum (`Syn, Connect, Data, Disconnect, Ping`), `Packet::decode(data: &[u8]) -> Result<Packet, DecodeError>`, `Packet::encode(&self) -> Vec<u8>`, `compute_checksum(data: &[u8], key: u32) -> u8`

PRUDP v0 packet layout (from Kinnay's NEX documentation):

```
Offset  Size  Description
0       1     Source virtual port
1       1     Destination virtual port
2       1     Type (bits 3-0) and flags (bits 7-4)
3       1     Session ID
4       4     Packet signature (LE)
8       2     Sequence number (LE)
[10..n] var   Payload (optional, depends on type)
[n]     1     Checksum
```

Type values: SYN=0, CONNECT=1, DATA=2, DISCONNECT=3, PING=4

FC5 captured values for validation:
- SYN:     `3f 31 20 00 00000000 ...` → src=0x3f, dst=0x31, type_flags=0x20 (type=0=SYN, flags=0x2)
- CONNECT: `3f 31 b1 1c 9d1a89a0 0100 ...` → type_flags=0xb1 (type=1=CONNECT, flags=0xb), session=0x1c, sig=0x9d1a89a0, seq=1

- [ ] **Step 1: Implement the packet codec with tests**

Create `server/src/prudp/transport.rs`:

```rust
use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PacketType {
    Syn = 0,
    Connect = 1,
    Data = 2,
    Disconnect = 3,
    Ping = 4,
}

impl PacketType {
    fn from_u8(v: u8) -> Option<Self> {
        match v {
            0 => Some(Self::Syn),
            1 => Some(Self::Connect),
            2 => Some(Self::Data),
            3 => Some(Self::Disconnect),
            4 => Some(Self::Ping),
            _ => None,
        }
    }
}

impl fmt::Display for PacketType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Syn => write!(f, "SYN"),
            Self::Connect => write!(f, "CONNECT"),
            Self::Data => write!(f, "DATA"),
            Self::Disconnect => write!(f, "DISCONNECT"),
            Self::Ping => write!(f, "PING"),
        }
    }
}

pub const FLAG_ACK: u8 = 0x1;
pub const FLAG_RELIABLE: u8 = 0x2;
pub const FLAG_NEED_ACK: u8 = 0x4;
pub const FLAG_HAS_SIZE: u8 = 0x8;

#[derive(Debug, Clone)]
pub struct Packet {
    pub source: u8,
    pub dest: u8,
    pub packet_type: PacketType,
    pub flags: u8,
    pub session_id: u8,
    pub signature: u32,
    pub sequence: u16,
    pub payload: Vec<u8>,
}

#[derive(Debug, thiserror::Error)]
pub enum DecodeError {
    #[error("packet too short: {0} bytes")]
    TooShort(usize),
    #[error("unknown packet type: {0}")]
    UnknownType(u8),
    #[error("bad checksum: expected {expected:#x}, got {got:#x}")]
    BadChecksum { expected: u8, got: u8 },
}

impl Packet {
    pub fn decode(data: &[u8], checksum_key: u32) -> Result<Self, DecodeError> {
        if data.len() < 11 {
            return Err(DecodeError::TooShort(data.len()));
        }
        let source = data[0];
        let dest = data[1];
        let type_flags = data[2];
        let raw_type = type_flags & 0x0F;
        let flags = (type_flags >> 4) & 0x0F;
        let packet_type = PacketType::from_u8(raw_type)
            .ok_or(DecodeError::UnknownType(raw_type))?;
        let session_id = data[3];
        let signature = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        let sequence = u16::from_le_bytes([data[8], data[9]]);
        let checksum_byte = data[data.len() - 1];
        let expected = compute_checksum(&data[..data.len() - 1], checksum_key);
        if checksum_byte != expected {
            return Err(DecodeError::BadChecksum { expected, got: checksum_byte });
        }
        let payload = data[10..data.len() - 1].to_vec();
        Ok(Self { source, dest, packet_type, flags, session_id, signature, sequence, payload })
    }

    pub fn encode(&self, checksum_key: u32) -> Vec<u8> {
        let type_flags = (self.packet_type as u8) | (self.flags << 4);
        let mut buf = Vec::with_capacity(11 + self.payload.len());
        buf.push(self.source);
        buf.push(self.dest);
        buf.push(type_flags);
        buf.push(self.session_id);
        buf.extend_from_slice(&self.signature.to_le_bytes());
        buf.extend_from_slice(&self.sequence.to_le_bytes());
        buf.extend_from_slice(&self.payload);
        let cs = compute_checksum(&buf, checksum_key);
        buf.push(cs);
        buf
    }
}

pub fn compute_checksum(data: &[u8], key: u32) -> u8 {
    let mut sum: u32 = key.wrapping_add(
        data.chunks(4)
            .map(|c| {
                let mut arr = [0u8; 4];
                arr[..c.len()].copy_from_slice(c);
                u32::from_le_bytes(arr)
            })
            .fold(0u32, |a, b| a.wrapping_add(b))
    );
    let bytes = sum.to_le_bytes();
    let folded = bytes[0].wrapping_add(bytes[1]).wrapping_add(bytes[2]).wrapping_add(bytes[3]);
    folded
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_syn() {
        let pkt = Packet {
            source: 0x3f,
            dest: 0x31,
            packet_type: PacketType::Syn,
            flags: FLAG_NEED_ACK,
            session_id: 0,
            signature: 0,
            sequence: 0,
            payload: vec![],
        };
        let encoded = pkt.encode(0);
        let decoded = Packet::decode(&encoded, 0).unwrap();
        assert_eq!(decoded.source, 0x3f);
        assert_eq!(decoded.dest, 0x31);
        assert_eq!(decoded.packet_type, PacketType::Syn);
        assert_eq!(decoded.sequence, 0);
        assert!(decoded.payload.is_empty());
    }

    #[test]
    fn round_trip_data_with_payload() {
        let pkt = Packet {
            source: 0x3f,
            dest: 0x31,
            packet_type: PacketType::Data,
            flags: FLAG_RELIABLE | FLAG_NEED_ACK | FLAG_HAS_SIZE,
            session_id: 0x1c,
            signature: 0x9d1a89a0,
            sequence: 42,
            payload: vec![0xDE, 0xAD, 0xBE, 0xEF],
        };
        let encoded = pkt.encode(0);
        let decoded = Packet::decode(&encoded, 0).unwrap();
        assert_eq!(decoded.packet_type, PacketType::Data);
        assert_eq!(decoded.session_id, 0x1c);
        assert_eq!(decoded.signature, 0x9d1a89a0);
        assert_eq!(decoded.sequence, 42);
        assert_eq!(decoded.payload, vec![0xDE, 0xAD, 0xBE, 0xEF]);
    }

    #[test]
    fn decode_rejects_truncated() {
        let short = vec![0x3f, 0x31, 0x20, 0x00];
        assert!(Packet::decode(&short, 0).is_err());
    }

    #[test]
    fn decode_rejects_bad_checksum() {
        let pkt = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Syn,
            flags: 0, session_id: 0, signature: 0, sequence: 0, payload: vec![],
        };
        let mut encoded = pkt.encode(0);
        let last = encoded.len() - 1;
        encoded[last] ^= 0xFF;
        assert!(matches!(Packet::decode(&encoded, 0), Err(DecodeError::BadChecksum { .. })));
    }
}
```

**Important note:** The checksum algorithm above is a best-effort implementation based on the documented PRUDP v0 spec. If decoded captured packets don't match, the checksum computation needs adjustment. The first live test against the game will reveal this — structured logging will show the raw bytes and computed vs. received checksums. FC5 may use a variant (e.g., different initial key or algorithm). If checksum validation fails, disable it temporarily (log a warning instead of rejecting) and iterate.

- [ ] **Step 2: Create `server/src/prudp/mod.rs`**

```rust
pub mod transport;
pub mod kerberos;
pub mod rmc;
```

(The `kerberos` and `rmc` modules will be empty files until Tasks 7 and 8.)

- [ ] **Step 3: Add `mod prudp;` to `lib.rs`**

- [ ] **Step 4: Run tests**

```bash
cd server && cargo test --lib prudp::transport::tests
```

Expected: 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add server/src/prudp/
git commit -m "feat(p2): PRUDP v0 packet codec with encode/decode and checksum"
```

---

### Task 6: PRUDP connection manager and UDP socket

**Files:**
- Create: `server/src/prudp/connection.rs`
- Modify: `server/src/prudp/mod.rs` (add `connection` module, add `run(addr, state)` entry point)
- Modify: `server/src/main.rs` (spawn the PRUDP server)

**Interfaces:**
- Consumes: `Packet`, `PacketType`, `encode`/`decode` from Task 5; `ServerState` from Task 2
- Produces: `prudp::run(addr: SocketAddr, state: ServerState) -> Result<()>` (the UDP event loop); `Connection` struct tracking per-client state (session_id, sequence counters, connection phase); responds to SYN with SYN-ACK, accepts CONNECT, dispatches DATA to the RMC layer (Task 8)

- [ ] **Step 1: Implement the connection state machine**

Create `server/src/prudp/connection.rs`:

```rust
use std::collections::HashMap;
use std::net::SocketAddr;
use super::transport::{Packet, PacketType, FLAG_ACK, FLAG_NEED_ACK};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Phase {
    AwaitingSyn,
    AwaitingConnect,
    Connected,
}

#[derive(Debug)]
pub struct Connection {
    pub addr: SocketAddr,
    pub phase: Phase,
    pub session_id: u8,
    pub server_signature: u32,
    pub client_signature: u32,
    pub client_sequence: u16,
    pub server_sequence: u16,
}

impl Connection {
    pub fn new(addr: SocketAddr) -> Self {
        Self {
            addr,
            phase: Phase::AwaitingSyn,
            session_id: 0,
            server_signature: rand::random(),
            client_signature: 0,
            client_sequence: 0,
            server_sequence: 0,
        }
    }

    pub fn handle_syn(&mut self, pkt: &Packet) -> Option<Packet> {
        if self.phase != Phase::AwaitingSyn {
            return None;
        }
        self.client_signature = pkt.signature;
        self.phase = Phase::AwaitingConnect;
        tracing::info!(addr = %self.addr, "SYN received, sending SYN-ACK");
        Some(Packet {
            source: pkt.dest,
            dest: pkt.source,
            packet_type: PacketType::Syn,
            flags: FLAG_ACK | FLAG_NEED_ACK,
            session_id: 0,
            signature: self.server_signature,
            sequence: 0,
            payload: vec![],
        })
    }

    pub fn handle_connect(&mut self, pkt: &Packet) -> Option<Packet> {
        if self.phase != Phase::AwaitingConnect {
            return None;
        }
        self.session_id = pkt.session_id;
        self.phase = Phase::Connected;
        tracing::info!(
            addr = %self.addr,
            session_id = self.session_id,
            payload_len = pkt.payload.len(),
            "CONNECT received (Kerberos ticket), sending CONNECT-ACK"
        );
        Some(Packet {
            source: pkt.dest,
            dest: pkt.source,
            packet_type: PacketType::Connect,
            flags: FLAG_ACK,
            session_id: self.session_id,
            signature: self.server_signature,
            sequence: 0,
            payload: vec![],
        })
    }

    pub fn next_server_sequence(&mut self) -> u16 {
        let seq = self.server_sequence;
        self.server_sequence = self.server_sequence.wrapping_add(1);
        seq
    }

    pub fn make_ack(&self, sequence: u16, source: u8, dest: u8) -> Packet {
        Packet {
            source,
            dest,
            packet_type: PacketType::Data,
            flags: FLAG_ACK,
            session_id: self.session_id,
            signature: self.server_signature,
            sequence,
            payload: vec![],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::prudp::transport::*;

    #[test]
    fn syn_produces_syn_ack() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        let syn = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Syn,
            flags: FLAG_NEED_ACK, session_id: 0, signature: 0, sequence: 0, payload: vec![],
        };
        let resp = conn.handle_syn(&syn).unwrap();
        assert_eq!(resp.packet_type, PacketType::Syn);
        assert!(resp.flags & FLAG_ACK != 0);
        assert_eq!(conn.phase, Phase::AwaitingConnect);
    }

    #[test]
    fn connect_after_syn_succeeds() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        let syn = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Syn,
            flags: FLAG_NEED_ACK, session_id: 0, signature: 0, sequence: 0, payload: vec![],
        };
        conn.handle_syn(&syn);
        let connect = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Connect,
            flags: FLAG_RELIABLE | FLAG_NEED_ACK, session_id: 0x1c,
            signature: 0x12345678, sequence: 1, payload: vec![0x45, 0x43, 0x4b, 0x31],
        };
        let resp = conn.handle_connect(&connect).unwrap();
        assert_eq!(resp.packet_type, PacketType::Connect);
        assert!(resp.flags & FLAG_ACK != 0);
        assert_eq!(conn.phase, Phase::Connected);
        assert_eq!(conn.session_id, 0x1c);
    }

    #[test]
    fn connect_before_syn_returns_none() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        let connect = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Connect,
            flags: 0, session_id: 0x1c, signature: 0, sequence: 1, payload: vec![],
        };
        assert!(conn.handle_connect(&connect).is_none());
    }

    #[test]
    fn sequence_wraps() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        conn.server_sequence = 0xFFFF;
        assert_eq!(conn.next_server_sequence(), 0xFFFF);
        assert_eq!(conn.next_server_sequence(), 0x0000);
    }
}
```

- [ ] **Step 2: Implement the UDP event loop**

Add to `server/src/prudp/mod.rs`:

```rust
pub mod transport;
pub mod connection;
pub mod kerberos;
pub mod rmc;

use std::collections::HashMap;
use std::net::SocketAddr;
use tokio::net::UdpSocket;
use connection::{Connection, Phase};
use transport::{Packet, PacketType, FLAG_NEED_ACK};

pub async fn run(addr: SocketAddr) -> anyhow::Result<()> {
    let sock = UdpSocket::bind(addr).await?;
    tracing::info!(%addr, "PRUDP server listening");
    let mut buf = [0u8; 2048];
    let mut connections: HashMap<SocketAddr, Connection> = HashMap::new();
    let checksum_key: u32 = 0;

    loop {
        let (len, peer) = sock.recv_from(&mut buf).await?;
        let data = &buf[..len];

        let pkt = match Packet::decode(data, checksum_key) {
            Ok(p) => p,
            Err(e) => {
                tracing::warn!(%peer, %e, bytes = len, "dropping malformed PRUDP packet");
                continue;
            }
        };

        tracing::debug!(
            %peer, typ = %pkt.packet_type, seq = pkt.sequence,
            flags = pkt.flags, payload_len = pkt.payload.len(),
            "PRUDP ← recv"
        );

        let conn = connections.entry(peer).or_insert_with(|| Connection::new(peer));

        let response = match pkt.packet_type {
            PacketType::Syn => conn.handle_syn(&pkt),
            PacketType::Connect => conn.handle_connect(&pkt),
            PacketType::Data => {
                if conn.phase == Phase::Connected {
                    tracing::info!(
                        %peer, seq = pkt.sequence, payload_len = pkt.payload.len(),
                        "DATA received (RMC dispatch will go here)"
                    );
                    if pkt.flags & FLAG_NEED_ACK != 0 {
                        Some(conn.make_ack(pkt.sequence, pkt.dest, pkt.source))
                    } else {
                        None
                    }
                } else {
                    tracing::warn!(%peer, "DATA before CONNECT, ignoring");
                    None
                }
            }
            PacketType::Disconnect => {
                tracing::info!(%peer, "client disconnecting");
                connections.remove(&peer);
                None
            }
            PacketType::Ping => {
                tracing::debug!(%peer, "PING");
                Some(Packet {
                    source: pkt.dest, dest: pkt.source,
                    packet_type: PacketType::Ping, flags: transport::FLAG_ACK,
                    session_id: conn.session_id, signature: conn.server_signature,
                    sequence: pkt.sequence, payload: vec![],
                })
            }
        };

        if let Some(resp) = response {
            let out = resp.encode(checksum_key);
            tracing::debug!(
                %peer, typ = %resp.packet_type, seq = resp.sequence, len = out.len(),
                "PRUDP → send"
            );
            sock.send_to(&out, peer).await?;
        }
    }
}
```

- [ ] **Step 3: Spawn PRUDP server in main.rs**

```rust
// After REST server spawn:
let prudp_addr = std::net::SocketAddr::new(cfg.listen_ip, cfg.prudp_port);
tokio::spawn(async move {
    if let Err(e) = prudp::run(prudp_addr).await {
        tracing::error!(%e, "PRUDP server error");
    }
});
```

- [ ] **Step 4: Write UDP integration test**

Create `server/tests/prudp_handshake.rs`:

```rust
use std::net::SocketAddr;
use tokio::net::UdpSocket;

#[tokio::test]
async fn syn_gets_syn_ack() {
    let server_addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
    let sock = UdpSocket::bind(server_addr).await.unwrap();
    let actual_addr = sock.local_addr().unwrap();
    drop(sock);

    tokio::spawn(async move {
        fc5mp_server::prudp::run(actual_addr).await.unwrap();
    });
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
    let syn = fc5mp_server::prudp::transport::Packet {
        source: 0x3f, dest: 0x31,
        packet_type: fc5mp_server::prudp::transport::PacketType::Syn,
        flags: fc5mp_server::prudp::transport::FLAG_NEED_ACK,
        session_id: 0, signature: 0, sequence: 0, payload: vec![],
    };
    client.send_to(&syn.encode(0), actual_addr).await.unwrap();

    let mut buf = [0u8; 2048];
    let (len, _) = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        client.recv_from(&mut buf),
    ).await.unwrap().unwrap();

    let resp = fc5mp_server::prudp::transport::Packet::decode(&buf[..len], 0).unwrap();
    assert_eq!(resp.packet_type, fc5mp_server::prudp::transport::PacketType::Syn);
    assert!(resp.flags & fc5mp_server::prudp::transport::FLAG_ACK != 0);
}
```

- [ ] **Step 5: Run tests**

```bash
cd server && cargo test --test prudp_handshake
```

Expected: SYN → SYN-ACK test passes.

- [ ] **Step 6: Commit**

```bash
git add server/src/prudp/ server/src/main.rs server/tests/
git commit -m "feat(p2): PRUDP connection manager with SYN/CONNECT handshake and UDP event loop"
```

---

### Task 7: Kerberos ticket issuance and session key derivation

**Files:**
- Create: `server/src/prudp/kerberos.rs`
- Modify: `server/src/rest/handlers.rs` (wire real public key into rendezvous response)

**Interfaces:**
- Consumes: `Packet` payload from CONNECT (the ECK1 Kerberos blob)
- Produces: `KerberosServer::new() -> Self` (generates server keypair), `KerberosServer::public_key_blob_b64(&self) -> String`, `KerberosServer::process_ticket(&self, connect_payload: &[u8]) -> Result<SessionKeys, KerberosError>`, `SessionKeys { rc4_key: Vec<u8> }`, `pub fn public_key_blob_b64() -> String` (module-level convenience for the REST handler)

The Quazal Kerberos scheme (from Kinnay's NEX documentation):

1. The authentication server generates a random session key and encrypts it (along with metadata) into a **ticket** using the secure server's key.
2. The ticket is sent to the client, who forwards it to the secure server in the CONNECT packet.
3. The secure server decrypts the ticket with its own key, recovers the session key, and uses it to derive the RC4 encryption key for DATA packets.

Since our server plays both roles, we generate the ticket ourselves and decrypt it ourselves. The HMAC-MD5 is used for key derivation: `derived_key = HMAC-MD5(server_key, session_key)`.

**Note:** The exact ticket format (`ECK1` blob structure) will be determined iteratively when the game first connects. The implementation below follows the documented NEX Kerberos format. If the game's ticket doesn't parse, we log the raw bytes and adjust.

- [ ] **Step 1: Implement Kerberos module**

```rust
use hmac::{Hmac, Mac};
use md5::Md5;

type HmacMd5 = Hmac<Md5>;

#[derive(Debug, thiserror::Error)]
pub enum KerberosError {
    #[error("ticket too short: {0} bytes")]
    TooShort(usize),
    #[error("invalid ticket magic (expected ECK1)")]
    BadMagic,
    #[error("HMAC verification failed")]
    HmacFailed,
}

pub struct KerberosServer {
    server_key: Vec<u8>,
}

#[derive(Debug, Clone)]
pub struct SessionKeys {
    pub rc4_key: Vec<u8>,
}

impl KerberosServer {
    pub fn new() -> Self {
        use rand::RngCore;
        let mut key = vec![0u8; 32];
        rand::thread_rng().fill_bytes(&mut key);
        Self { server_key: key }
    }

    pub fn public_key_blob_b64(&self) -> String {
        let mut blob = Vec::new();
        blob.extend_from_slice(b"ECS1");
        blob.extend_from_slice(&[0u8; 4]);
        blob.extend_from_slice(&self.server_key);
        base64_encode(&blob)
    }

    pub fn process_ticket(&self, connect_payload: &[u8]) -> Result<SessionKeys, KerberosError> {
        if connect_payload.len() < 8 {
            return Err(KerberosError::TooShort(connect_payload.len()));
        }
        if &connect_payload[..4] != b"ECK1" {
            tracing::warn!(
                first_4 = ?&connect_payload[..4.min(connect_payload.len())],
                "ticket doesn't start with ECK1 — format may differ from expected"
            );
        }
        let rc4_key = derive_key(&self.server_key, connect_payload);
        tracing::info!(key_len = rc4_key.len(), "session key derived from ticket");
        Ok(SessionKeys { rc4_key })
    }
}

fn derive_key(server_key: &[u8], ticket_data: &[u8]) -> Vec<u8> {
    let mut mac = HmacMd5::new_from_slice(server_key)
        .expect("HMAC accepts any key length");
    mac.update(ticket_data);
    mac.finalize().into_bytes().to_vec()
}

fn base64_encode(data: &[u8]) -> String {
    const CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity((data.len() + 2) / 3 * 4);
    for chunk in data.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = chunk.get(1).copied().unwrap_or(0) as u32;
        let b2 = chunk.get(2).copied().unwrap_or(0) as u32;
        let triple = (b0 << 16) | (b1 << 8) | b2;
        out.push(CHARS[((triple >> 18) & 0x3F) as usize] as char);
        out.push(CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 { out.push(CHARS[((triple >> 6) & 0x3F) as usize] as char); } else { out.push('='); }
        if chunk.len() > 2 { out.push(CHARS[(triple & 0x3F) as usize] as char); } else { out.push('='); }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn public_key_starts_with_ecs1() {
        let ks = KerberosServer::new();
        let b64 = ks.public_key_blob_b64();
        assert!(b64.starts_with("RUNT"), "should start with base64 of 'ECS1': {b64}");
    }

    #[test]
    fn process_ticket_derives_key() {
        let ks = KerberosServer::new();
        let fake_ticket = b"ECK1\x00\x00\x00\x00some_encrypted_session_key_data";
        let keys = ks.process_ticket(fake_ticket).unwrap();
        assert_eq!(keys.rc4_key.len(), 16);
    }

    #[test]
    fn derive_key_is_deterministic() {
        let key1 = derive_key(b"server_secret", b"ticket_data");
        let key2 = derive_key(b"server_secret", b"ticket_data");
        assert_eq!(key1, key2);
    }

    #[test]
    fn derive_key_changes_with_input() {
        let key1 = derive_key(b"server_secret", b"ticket_A");
        let key2 = derive_key(b"server_secret", b"ticket_B");
        assert_ne!(key1, key2);
    }

    #[test]
    fn too_short_ticket_errors() {
        let ks = KerberosServer::new();
        assert!(ks.process_ticket(b"ECK").is_err());
    }
}
```

**Important note:** The `process_ticket` implementation is deliberately lenient — it derives a key from whatever the game sends rather than strictly parsing the NEX ticket structure. This lets us get through the CONNECT phase and start seeing DATA packets. Once we see the actual RMC calls (Task 8), we can validate that the derived key correctly decrypts them. If not, we refine the key derivation.

- [ ] **Step 2: Wire public key into REST handler**

Replace the stub `public_key_blob_b64()` function with the real implementation. The `KerberosServer` instance needs to be shared between REST (for serving the public key) and PRUDP (for verifying tickets). Add it to the `RestContext` struct and update `rendezvous_config` to call `ctx.kerberos.public_key_blob_b64()`.

- [ ] **Step 3: Run tests**

```bash
cd server && cargo test --lib prudp::kerberos::tests
```

Expected: 5 tests pass.

- [ ] **Step 4: Commit**

```bash
git add server/src/prudp/kerberos.rs server/src/rest/
git commit -m "feat(p2): Kerberos ticket processing and session key derivation"
```

---

### Task 8: NEX RMC framework with RC4 and structured logging

**Files:**
- Create: `server/src/prudp/rmc.rs`
- Create: `server/src/prudp/rc4.rs`
- Modify: `server/src/prudp/mod.rs` (integrate RMC dispatch into DATA handling)

**Interfaces:**
- Consumes: `SessionKeys` from Task 7 (the RC4 key), DATA packet payloads from Task 6
- Produces: `Rc4::new(key: &[u8]) -> Self`, `Rc4::apply(&mut self, data: &mut [u8])`, `RmcMessage { protocol_id: u8, method_id: u32, call_id: u32, payload: Vec<u8> }`, `RmcMessage::decode(decrypted: &[u8]) -> Result<RmcMessage, RmcError>`, `RmcMessage::encode_response(call_id: u32, protocol_id: u8, method_id: u32, data: &[u8]) -> Vec<u8>`

NEX RMC message format (from Kinnay's documentation):

```
Offset  Size  Description
0       4     Size of the RMC body (LE)
4       1     Protocol ID (0x80 bit = request)
5       1-3   If request: call ID (u32 LE), then method ID (u32 LE)
              If response: success flag, then call ID, then method ID or error code
```

The exact layout will be validated against live traffic. This implementation logs every incoming RMC call at INFO level with all decoded fields, which is the primary tool for the iterative development phase.

- [ ] **Step 1: Implement RC4**

Create `server/src/prudp/rc4.rs`:

```rust
pub struct Rc4 {
    s: [u8; 256],
    i: u8,
    j: u8,
}

impl Rc4 {
    pub fn new(key: &[u8]) -> Self {
        let mut s = [0u8; 256];
        for i in 0..256 { s[i] = i as u8; }
        let mut j: u8 = 0;
        for i in 0..256 {
            j = j.wrapping_add(s[i]).wrapping_add(key[i % key.len()]);
            s.swap(i, j as usize);
        }
        Self { s, i: 0, j: 0 }
    }

    pub fn apply(&mut self, data: &mut [u8]) {
        for byte in data.iter_mut() {
            self.i = self.i.wrapping_add(1);
            self.j = self.j.wrapping_add(self.s[self.i as usize]);
            self.s.swap(self.i as usize, self.j as usize);
            let k = self.s[self.s[self.i as usize].wrapping_add(self.s[self.j as usize]) as usize];
            *byte ^= k;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encrypt_decrypt_round_trip() {
        let key = b"test_key_123";
        let original = b"hello world, this is a test message for RC4";
        let mut encrypted = original.to_vec();
        Rc4::new(key).apply(&mut encrypted);
        assert_ne!(&encrypted, original);
        Rc4::new(key).apply(&mut encrypted);
        assert_eq!(&encrypted, original);
    }

    #[test]
    fn different_keys_produce_different_output() {
        let mut a = b"same_input".to_vec();
        let mut b = a.clone();
        Rc4::new(b"key_a").apply(&mut a);
        Rc4::new(b"key_b").apply(&mut b);
        assert_ne!(a, b);
    }

    #[test]
    fn empty_input() {
        let mut data = vec![];
        Rc4::new(b"key").apply(&mut data);
        assert!(data.is_empty());
    }
}
```

- [ ] **Step 2: Implement RMC message parsing and logging**

Create `server/src/prudp/rmc.rs`:

```rust
#[derive(Debug, thiserror::Error)]
pub enum RmcError {
    #[error("RMC message too short: {0} bytes")]
    TooShort(usize),
    #[error("declared size {declared} exceeds data length {actual}")]
    SizeMismatch { declared: usize, actual: usize },
}

#[derive(Debug, Clone)]
pub struct RmcRequest {
    pub protocol_id: u8,
    pub call_id: u32,
    pub method_id: u32,
    pub payload: Vec<u8>,
}

impl RmcRequest {
    pub fn decode(data: &[u8]) -> Result<Self, RmcError> {
        if data.len() < 13 {
            return Err(RmcError::TooShort(data.len()));
        }
        let size = u32::from_le_bytes([data[0], data[1], data[2], data[3]]) as usize;
        if size + 4 > data.len() {
            return Err(RmcError::SizeMismatch { declared: size, actual: data.len() - 4 });
        }
        let protocol_id = data[4] & 0x7F;
        let is_request = data[4] & 0x80 != 0;
        if !is_request {
            tracing::debug!(protocol_id, "received RMC response (unexpected on server side)");
        }
        let call_id = u32::from_le_bytes([data[5], data[6], data[7], data[8]]);
        let method_id = u32::from_le_bytes([data[9], data[10], data[11], data[12]]);
        let payload = data[13..4 + size].to_vec();

        tracing::info!(
            protocol_id,
            method_id,
            call_id,
            payload_len = payload.len(),
            payload_hex = %hex_preview(&payload, 64),
            "RMC request received"
        );

        Ok(Self { protocol_id, call_id, method_id, payload })
    }
}

pub fn encode_response(call_id: u32, protocol_id: u8, method_id: u32, result_data: &[u8]) -> Vec<u8> {
    let body_size = 1 + 4 + 4 + result_data.len();
    let mut out = Vec::with_capacity(4 + body_size);
    out.extend_from_slice(&(body_size as u32).to_le_bytes());
    out.push(protocol_id);
    out.push(0x01);
    out.extend_from_slice(&call_id.to_le_bytes());
    out.extend_from_slice(&method_id.to_le_bytes());
    out.extend_from_slice(result_data);
    out
}

pub fn encode_error(call_id: u32, protocol_id: u8, error_code: u32) -> Vec<u8> {
    let body_size = 1 + 1 + 4 + 4;
    let mut out = Vec::with_capacity(4 + body_size);
    out.extend_from_slice(&(body_size as u32).to_le_bytes());
    out.push(protocol_id);
    out.push(0x00);
    out.extend_from_slice(&error_code.to_le_bytes());
    out.extend_from_slice(&call_id.to_le_bytes());
    out
}

fn hex_preview(data: &[u8], max: usize) -> String {
    let show = data.len().min(max);
    let hex: String = data[..show].iter().map(|b| format!("{b:02x}")).collect::<Vec<_>>().join(" ");
    if data.len() > max { format!("{hex} ...({} more)", data.len() - max) } else { hex }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decode_request() {
        let mut data = vec![];
        let body_size: u32 = 9 + 4;
        data.extend_from_slice(&body_size.to_le_bytes());
        data.push(0x80 | 10);
        data.extend_from_slice(&42u32.to_le_bytes());
        data.extend_from_slice(&3u32.to_le_bytes());
        data.extend_from_slice(b"test");
        let req = RmcRequest::decode(&data).unwrap();
        assert_eq!(req.protocol_id, 10);
        assert_eq!(req.call_id, 42);
        assert_eq!(req.method_id, 3);
        assert_eq!(req.payload, b"test");
    }

    #[test]
    fn decode_too_short() {
        assert!(RmcRequest::decode(&[0; 5]).is_err());
    }

    #[test]
    fn response_round_trip() {
        let resp = encode_response(42, 10, 3, b"result");
        assert!(resp.len() > 13);
        let size = u32::from_le_bytes([resp[0], resp[1], resp[2], resp[3]]) as usize;
        assert_eq!(size + 4, resp.len());
    }
}
```

- [ ] **Step 3: Integrate RMC dispatch into the PRUDP event loop**

In `server/src/prudp/mod.rs`, update the DATA branch to:
1. RC4-decrypt the payload using the connection's session key
2. Pass the decrypted bytes to `RmcRequest::decode`
3. For now, log the call and respond with a generic success (or error) — specific RMC handlers will be added iteratively based on what the game sends

```rust
PacketType::Data => {
    if conn.phase == Phase::Connected {
        let mut decrypted = pkt.payload.clone();
        if let Some(ref mut cipher) = conn.rc4_recv {
            cipher.apply(&mut decrypted);
        }
        match rmc::RmcRequest::decode(&decrypted) {
            Ok(req) => {
                let resp_data = rmc::encode_error(req.call_id, req.protocol_id, 0x00010001);
                let mut encrypted = resp_data;
                if let Some(ref mut cipher) = conn.rc4_send {
                    cipher.apply(&mut encrypted);
                }
                let seq = conn.next_server_sequence();
                Some(Packet {
                    source: pkt.dest, dest: pkt.source,
                    packet_type: PacketType::Data,
                    flags: FLAG_RELIABLE | FLAG_NEED_ACK | FLAG_HAS_SIZE,
                    session_id: conn.session_id,
                    signature: conn.server_signature,
                    sequence: seq,
                    payload: encrypted,
                })
            }
            Err(e) => {
                tracing::warn!(%e, "failed to decode RMC");
                None
            }
        }
    } else { None }
}
```

This requires adding `rc4_recv: Option<Rc4>` and `rc4_send: Option<Rc4>` fields to `Connection` (set during CONNECT processing from Task 7's `SessionKeys`).

- [ ] **Step 4: Add `rc4` module to `prudp/mod.rs`**

```rust
pub mod rc4;
```

- [ ] **Step 5: Run all tests**

```bash
cd server && cargo test
```

Expected: all tests pass (config, state, transport, connection, kerberos, rc4, rmc).

- [ ] **Step 6: Manual smoke test**

```bash
cd server && RUST_LOG=fc5mp_server=debug cargo run
```

In another terminal:

```bash
curl -X POST http://localhost:21000/v3/profiles/sessions
curl http://localhost:21000/v1/spaces/test/sandboxes/FCZ_PC_A/rendezvous/configuration
```

Expected: JSON responses with session ticket and PRUDP configuration.

- [ ] **Step 7: Commit**

```bash
git add server/src/prudp/
git commit -m "feat(p2): NEX RMC framework with RC4 encryption and structured logging"
```

---

## Post-Plan: What Comes Next

After completing these 8 tasks, the server can:
- Accept REST login requests and return fake session tickets
- Serve rendezvous configuration pointing at itself
- Accept PRUDP connections (SYN → CONNECT handshake)
- Decrypt and log incoming NEX RMC calls with protocol/method IDs

**The iterative phase begins:** connect the real game (via the DLL redirect mode, which is a separate plan for the C++ client) and work through each RMC call until a co-op session succeeds. The structured logging from Task 8 is the primary tool — it shows exactly what the game asks for, so we can implement the right responses one at a time.

**DLL redirect mode** (not in this plan): extends the existing C++ client with a `redirect` mode that rewrites REST/PRUDP destinations to our server. This is a separate implementation plan because it's C++ code in a different build system (CMake cross-compile), with different testing (Wine integration tests). It should be planned and built after the server is running and testable with curl/UDP tools.
