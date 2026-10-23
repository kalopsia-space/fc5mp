use axum::extract::State;
use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::http::{StatusCode, HeaderMap};
use axum::response::IntoResponse;
use axum::Json;
use serde_json::{json, Value};
use uuid::Uuid;

use super::RestContext;

pub async fn login(State(ctx): State<RestContext>) -> (StatusCode, Json<Value>) {
    let profile_id = ctx.state.get_or_create_profile_id();
    let session_id = Uuid::new_v4();
    let session_key = base64_rand(32);
    let ticket = base64_rand(64);

    let name = format!("fc5mp-player-{}", &profile_id.to_string()[..8]);
    let _ = ctx.state.add_player(profile_id, name.clone());

    // Register the Ubi-SessionId so later requests can be mapped to this player
    ctx.state.register_ubi_session(session_id.to_string(), profile_id);

    tracing::info!(%profile_id, %name, %session_id, "player logged in");

    let token = base64_rand(16);
    let body = json!({
        "platformType": "uplay",
        "ticket": ticket,
        "twoFactorAuthenticationTicket": null,
        "token": token,
        "profileId": profile_id.to_string(),
        "userId": profile_id.to_string(),
        "nameOnPlatform": name,
        "username": name,
        "initializeUser": false,
        "environment": "Prod",
        "expiration": "2030-01-01T00:00:00.0000000Z",
        "spaceId": "f40e304d-8e8d-4343-8270-d06487c35add",
        "clientIp": "127.0.0.1",
        "clientIpCountry": "XX",
        "serverTime": chrono_now_iso(),
        "sessionId": session_id.to_string(),
        "sessionKey": session_key,
        "rememberMeTicket": null,
        "hasAcceptedLegalOptins": true,
        "accountIssues": null,
    });
    (StatusCode::OK, Json(body))
}

pub async fn rendezvous_config(State(ctx): State<RestContext>) -> Json<Value> {
    let host = if ctx.prudp_host == "0.0.0.0" { "127.0.0.1".to_string() } else { ctx.prudp_host.clone() };
    let prudp_url = format!(
        "prudp:/address={};port={}",
        host, ctx.prudp_port
    );
    tracing::info!(%prudp_url, "serving rendezvous configuration");
    Json(json!({
        "http_urls": [],
        "prudp_loadbalancer_url": prudp_url,
        "prudp_urls": [],
        "http_loadbalancer_url": "lb-rdv-ne1-prod-http.ubi.com/FCZ_PC_LNCH_A",
        "public_key": ctx.kerberos.public_key_blob_b64(),
    }))
}

pub async fn storm_router() -> Json<Value> {
    Json(json!({"router": "127.0.0.1:12000"}))
}

// Store/items catalog fetched right after login. The game expects a JSON wrapper
// whose `compressedItems` is base64(gzip(<items JSON>)); we return an empty
// catalog (gzip of "[]"). Shape from the real capture
// (docs/research/2026-10-19-real-ubisoft-online-capture.md). GUESS: empty is
// assumed sufficient to enter co-op — verify via capture if the game stalls here.
pub async fn items_all() -> Json<Value> {
    Json(json!({
        "lastModified": chrono_now_iso(),
        "compressedItems": "H4sIAAAAAAAC/4uOBQApu0wNAgAAAA==",
    }))
}

// Rewards/challenges list (msr host). Empty list. GUESS at top-level shape and
// emptiness — verify against capture if the game stalls here.
pub async fn rewards() -> Json<Value> {
    Json(json!([]))
}

// Game PUTs to the contactUrl from the US_CONNECTION_ESTABLISHED push to register
// for notifications; acknowledge so it does not retry/stall.
pub async fn push_notification_ack() -> StatusCode {
    StatusCode::OK
}

// Client telemetry/log upload during online init; just accept it.
pub async fn remote_log() -> StatusCode {
    StatusCode::OK
}

// Game PUTs its population/presence data while going online; accept it.
pub async fn populations_data() -> Json<Value> {
    Json(json!({}))
}

pub async fn connect_urls() -> Json<Value> {
    tracing::info!("serving connect URLs");
    Json(json!({
        "connectUrls": [],
        "expiration": "2030-01-01T00:00:00Z"
    }))
}

pub async fn ip_location() -> Json<Value> {
    tracing::info!("serving IP location");
    Json(json!({
        "country": "XX",
        "continent": "XX",
        "timeZone": "UTC"
    }))
}

pub async fn app_configuration(
    axum::extract::Path(app_id): axum::extract::Path<String>,
) -> Json<Value> {
    tracing::info!(%app_id, "serving app configuration");
    // Real Ubisoft SDK configuration shape (platformConfig + gatewayResources +
    // featuresSwitches + uplayServices + events...), adapted from a captured
    // config to FC5's ids with every service host pointed at a *.ubi.com host we
    // redirect. An empty config makes the game treat online as unavailable
    // (Maine-118); the gatewayResources tell it where each service lives.
    static CONFIG: &str = include_str!("fc5_app_config.json");
    match serde_json::from_str::<Value>(CONFIG) {
        Ok(v) => Json(v),
        Err(e) => {
            tracing::error!(%e, "fc5_app_config.json parse failed");
            Json(json!({ "configuration": { "platformConfig": {}, "gatewayResources": [] } }))
        }
    }
}

pub async fn users_me(State(ctx): State<RestContext>) -> Json<Value> {
    let profile_id = ctx.state.any_profile_id().unwrap_or_default();
    let name = format!("fc5mp-player-{}", &profile_id[..8.min(profile_id.len())]);
    tracing::info!(%profile_id, "serving users/me");
    Json(json!({
        "profileId": profile_id,
        "userId": profile_id,
        "platformType": "uplay",
        "idOnPlatform": profile_id,
        "nameOnPlatform": name,
        "username": name,
        "email": "fc5mp@localhost",
        "initializeUser": false,
        "environment": "Prod",
        "hasAcceptedLegalOptins": true,
        "accountIssues": null,
        "status": {
            "generalStatus": "activated"
        },
        "twoFactorAuthenticationTicket": null,
        "hasPassword": true,
        "rememberMeTicket": null,
        "country": "XX",
        "preferredLanguage": "en-US",
    }))
}

pub async fn twofa_status() -> Json<Value> {
    tracing::info!("serving 2FA status");
    Json(json!({
        "active": false
    }))
}

pub async fn user_subscriptions() -> Json<Value> {
    tracing::info!("serving user subscriptions");
    Json(json!({
        "subscriptions": []
    }))
}

pub async fn user_profiles(State(ctx): State<RestContext>) -> Json<Value> {
    let profile_id = ctx.state.any_profile_id().unwrap_or_default();
    let name = format!("fc5mp-player-{}", &profile_id[..8.min(profile_id.len())]);
    tracing::info!(%profile_id, "serving user profiles");
    Json(json!({
        "profiles": [{
            "profileId": profile_id,
            "userId": profile_id,
            "platformType": "uplay",
            "idOnPlatform": profile_id,
            "nameOnPlatform": name
        }, {
            "profileId": profile_id,
            "userId": profile_id,
            "platformType": "steam",
            "idOnPlatform": "76561198000000000",
            "nameOnPlatform": name
        }]
    }))
}

pub async fn link_steam_profile(
    State(ctx): State<RestContext>,
    axum::extract::Path(user_id): axum::extract::Path<String>,
) -> Json<Value> {
    let profile_id = ctx.state.any_profile_id().unwrap_or_default();
    let name = format!("fc5mp-player-{}", &profile_id[..8.min(profile_id.len())]);
    tracing::info!(%user_id, "linking steam profile");
    Json(json!({
        "profileId": profile_id,
        "userId": user_id,
        "platformType": "steam",
        "idOnPlatform": profile_id,
        "nameOnPlatform": name
    }))
}

pub async fn friends() -> Json<Value> {
    tracing::info!("serving friends");
    Json(json!({
        "groupId": "fc5mp-group",
        "friends": []
    }))
}

pub async fn blocklist() -> Json<Value> {
    tracing::info!("serving blocklist");
    Json(json!({
        "groupId": "fc5mp-group",
        "profiles": []
    }))
}

pub async fn parameters() -> Json<Value> {
    tracing::info!("serving parameters");
    Json(json!({
        "parameters": []
    }))
}

pub async fn service_events() -> StatusCode {
    tracing::debug!("received service events");
    StatusCode::OK
}

pub async fn ownership(State(ctx): State<RestContext>) -> Json<Value> {
    let profile_id = ctx.state.any_profile_id().unwrap_or_default();
    Json(json!({
        "profileId": profile_id,
        "ownerships": [{
            "productId": "5067",
            "owned": true,
            "suspended": false,
            "quantity": 1,
            "balanceTimestamp": "2020-01-01T00:00:00Z"
        }, {
            "productId": "5068",
            "owned": true,
            "suspended": false,
            "quantity": 1,
            "balanceTimestamp": "2020-01-01T00:00:00Z"
        }]
    }))
}

pub async fn populations() -> Json<Value> {
    tracing::info!("serving populations");
    Json(json!({
        "populations": []
    }))
}

pub async fn inventory() -> Json<Value> {
    tracing::info!("serving inventory");
    Json(json!({
        "items": []
    }))
}

pub async fn entities(
    axum::extract::Query(params): axum::extract::Query<std::collections::HashMap<String, String>>,
) -> Json<Value> {
    let entity_type = params.get("type").map(|s| s.as_str()).unwrap_or("");
    tracing::info!(%entity_type, "serving profile entities");
    Json(json!([]))
}


pub async fn me_populations() -> Json<Value> {
    tracing::info!("serving me/populations");
    Json(json!({
        "populations": []
    }))
}

/// OAuth token endpoint — the SDK calls this for Bearer token refresh.
/// TCU returns a static token; we do the same.
pub async fn native_token() -> Json<Value> {
    tracing::info!("serving native token");
    Json(json!({
        "access_token": base64_rand(32),
        "expires_in": 800,
        "token_type": "Bearer"
    }))
}

/// Profile lookup by ID(s) — co-op player resolution.
/// Query: ?profileId=uuid1,uuid2,...
pub async fn profiles_by_id(
    State(ctx): State<RestContext>,
    axum::extract::Query(params): axum::extract::Query<std::collections::HashMap<String, String>>,
) -> Json<Value> {
    let profile_ids_str = params.get("profileId").map(|s| s.as_str()).unwrap_or("");
    let ids: Vec<Uuid> = profile_ids_str
        .split(',')
        .filter(|s| !s.is_empty())
        .filter_map(|s| s.trim().parse::<Uuid>().ok())
        .collect();

    tracing::info!(count = ids.len(), "profile lookup by ID");

    let players = ctx.state.get_players(&ids);
    let profiles: Vec<Value> = if players.is_empty() {
        // If no players registered yet, return synthetic profiles for each requested ID
        ids.iter().map(|id| {
            let id_str = id.to_string();
            let name = format!("fc5mp-player-{}", &id_str[..8]);
            json!({
                "profileId": id_str,
                "userId": id_str,
                "platformType": "uplay",
                "idOnPlatform": id_str,
                "nameOnPlatform": name
            })
        }).collect()
    } else {
        players.iter().map(|p| {
            let id_str = p.id.to_string();
            json!({
                "profileId": id_str,
                "userId": id_str,
                "platformType": "uplay",
                "idOnPlatform": id_str,
                "nameOnPlatform": p.name
            })
        }).collect()
    };

    Json(json!({ "profiles": profiles }))
}

/// Entity storage PUT — saves player entities (progress, challenges, UGC).
pub async fn put_entity(
    State(ctx): State<RestContext>,
    headers: HeaderMap,
    axum::extract::Path(profile_id_str): axum::extract::Path<String>,
    Json(body): Json<Value>,
) -> (StatusCode, Json<Value>) {
    let session_id = headers.get("ubi-sessionid")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");

    if session_id.is_empty() {
        return (StatusCode::BAD_REQUEST, Json(json!({"error": "Missing Ubi-SessionId header"})));
    }

    let owner = match ctx.state.resolve_ubi_session(session_id) {
        Some(id) => id,
        None => return (StatusCode::UNAUTHORIZED, Json(json!({"error": "Invalid session"}))),
    };

    let profile_id = match profile_id_str.parse::<Uuid>() {
        Ok(id) => id,
        Err(_) => return (StatusCode::BAD_REQUEST, Json(json!({"error": "Invalid profileId"}))),
    };

    if owner != profile_id {
        return (StatusCode::FORBIDDEN, Json(json!({"error": "Cannot modify another profile's entities"})));
    }

    let name = match body.get("name").and_then(|v| v.as_str()) {
        Some(n) => n.to_string(),
        None => return (StatusCode::BAD_REQUEST, Json(json!({"error": "Invalid or missing name field"}))),
    };

    let entity_type = body.get("type").and_then(|v| v.as_str()).unwrap_or("").to_string();
    let space_id = body.get("spaceId").and_then(|v| v.as_str()).map(|s| s.to_string());
    let obj = body.get("obj").cloned().unwrap_or(json!({}));

    tracing::info!(%profile_id, %name, entity_type = %entity_type, "entity PUT");

    let entity = ctx.state.put_entity(profile_id, space_id.clone(), name.clone(), entity_type, obj.clone());

    (StatusCode::OK, Json(json!({
        "entityId": entity.entity_id.to_string(),
        "lastModified": entity.last_modified,
        "name": entity.name,
        "profileId": entity.profile_id.to_string(),
        "obj": entity.obj,
        "revision": entity.revision,
        "spaceId": entity.space_id,
    })))
}

/// Entity storage GET — retrieves entities for a profile.
pub async fn get_profile_entities(
    State(ctx): State<RestContext>,
    axum::extract::Path(profile_id_str): axum::extract::Path<String>,
    axum::extract::Query(params): axum::extract::Query<std::collections::HashMap<String, String>>,
) -> Json<Value> {
    let entity_type = params.get("type").map(|s| s.as_str());
    let profile_id = profile_id_str.parse::<Uuid>().unwrap_or_default();
    tracing::info!(%profile_id, ?entity_type, "serving profile entities (stateful)");

    let entities = ctx.state.get_entities(&profile_id, entity_type);
    let result: Vec<Value> = entities.iter().map(|e| json!({
        "entityId": e.entity_id.to_string(),
        "lastModified": e.last_modified,
        "name": e.name,
        "profileId": e.profile_id.to_string(),
        "obj": e.obj,
        "revision": e.revision,
        "spaceId": e.space_id,
    })).collect();

    Json(json!(result))
}

/// Entity storage GET — retrieves entities for a space.
pub async fn get_space_entities_stateful(
    State(ctx): State<RestContext>,
    axum::extract::Path(space_id): axum::extract::Path<String>,
    axum::extract::Query(params): axum::extract::Query<std::collections::HashMap<String, String>>,
) -> Json<Value> {
    let entity_type = params.get("type").map(|s| s.as_str());
    let tags = params.get("tags").map(|s| s.as_str()).unwrap_or("");
    tracing::info!(%space_id, ?entity_type, %tags, "serving space entities (stateful)");

    let entities = ctx.state.get_space_entities(&space_id, entity_type);
    let result: Vec<Value> = entities.iter().map(|e| json!({
        "entityId": e.entity_id.to_string(),
        "lastModified": e.last_modified,
        "name": e.name,
        "profileId": e.profile_id.to_string(),
        "obj": e.obj,
        "revision": e.revision,
        "spaceId": e.space_id,
    })).collect();

    Json(json!(result))
}

/// Single entity GET by ID.
pub async fn get_entity_by_id(
    State(ctx): State<RestContext>,
    axum::extract::Path(entity_id_str): axum::extract::Path<String>,
) -> (StatusCode, Json<Value>) {
    let entity_id = match entity_id_str.parse::<Uuid>() {
        Ok(id) => id,
        Err(_) => return (StatusCode::NOT_FOUND, Json(json!({"error": "Invalid entityId"}))),
    };

    match ctx.state.get_entity(&entity_id) {
        Some(e) => (StatusCode::OK, Json(json!({
            "entityId": e.entity_id.to_string(),
            "lastModified": e.last_modified,
            "name": e.name,
            "profileId": e.profile_id.to_string(),
            "obj": e.obj,
            "revision": e.revision,
            "spaceId": e.space_id,
        }))),
        None => (StatusCode::NOT_FOUND, Json(json!({"error": "Entity not found"}))),
    }
}

fn base64_rand(len: usize) -> String {
    use rand::RngCore;
    let mut buf = vec![0u8; len];
    rand::thread_rng().fill_bytes(&mut buf);
    base64_encode(&buf)
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

fn chrono_now_iso() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs();
    let days = secs / 86400;
    let rem = secs % 86400;
    let hours = rem / 3600;
    let mins = (rem % 3600) / 60;
    let s = rem % 60;
    let (year, month, day) = days_to_ymd(days);
    format!("{year:04}-{month:02}-{day:02}T{hours:02}:{mins:02}:{s:02}Z")
}

fn days_to_ymd(days_since_epoch: u64) -> (u64, u64, u64) {
    let z = days_since_epoch + 719468;
    let era = z / 146097;
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    (y, m, d)
}

pub async fn events_config() -> Json<Value> {
    tracing::info!("serving events config");
    Json(json!({
        "publicConfig": {},
        "sendPeriodSeconds": 60,
        "types": []
    }))
}

pub async fn free_games() -> (StatusCode, &'static str) {
    tracing::info!("serving free games");
    (StatusCode::OK, "none\n")
}

pub async fn opentelemetry_metrics() -> StatusCode {
    StatusCode::OK
}

pub async fn online_statuses(
    axum::extract::Query(params): axum::extract::Query<std::collections::HashMap<String, String>>,
) -> Json<Value> {
    let user_ids: Vec<&str> = params.get("UserIds")
        .map(|s| s.split(',').collect())
        .unwrap_or_default();
    tracing::debug!(count = user_ids.len(), "serving online statuses");
    let statuses: Vec<Value> = user_ids.iter().map(|uid| {
        json!({
            "userId": uid,
            "onlineStatus": "Online",
            "lastSeen": "2026-10-16T00:00:00Z"
        })
    }).collect();
    Json(json!({"onlineStatuses": statuses}))
}

pub async fn websocket_handler(ws: WebSocketUpgrade) -> impl IntoResponse {
    tracing::info!("WebSocket upgrade accepted");
    ws.on_upgrade(handle_websocket)
}

async fn handle_websocket(mut socket: WebSocket) {
    // The real push service sends a US_CONNECTION_ESTABLISHED notification
    // immediately on upgrade; the game waits for it before proceeding. Shape
    // captured from a real session
    // (docs/research/2026-10-19-real-ubisoft-online-capture.md).
    let connection_id = Uuid::new_v4().to_string();
    let conn = Uuid::new_v4().simple().to_string();
    let contact_url = format!(
        "https://public-im-ubiservices.ubi.com/v2/connections/{}/8183/{}/notifications",
        conn, connection_id
    );
    let established = json!({
        "content": {
            "connectionId": connection_id,
            "contactUrl": contact_url,
        },
        "notificationType": "US_CONNECTION_ESTABLISHED",
        "sourceApplicationId": "5dbe68ea-4d67-489f-9150-ff9f1ef543be",
        "datePosted": chrono_now_iso(),
    });
    if socket
        .send(Message::Text(established.to_string().into()))
        .await
        .is_err()
    {
        return;
    }
    loop {
        match socket.recv().await {
            Some(Ok(Message::Ping(data))) => {
                if socket.send(Message::Pong(data)).await.is_err() {
                    return;
                }
            }
            Some(Ok(Message::Close(_))) | None => return,
            Some(Err(_)) => return,
            _ => {}
        }
    }
}
