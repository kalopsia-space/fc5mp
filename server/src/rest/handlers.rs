use axum::extract::State;
use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::http::StatusCode;
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

    tracing::info!(%profile_id, %name, "player logged in");

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
        "http_loadbalancer_url": "",
        "public_key": ctx.kerberos.public_key_blob_b64(),
    }))
}

pub async fn storm_router() -> Json<Value> {
    Json(json!({"router": "127.0.0.1:12000"}))
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
    Json(json!({
        "configuration": {
            "platformConfig": {
                "environment": "Prod"
            },
            "featuresSwitches": [],
            "gatewayResources": []
        },
        "expiration": "2030-01-01T00:00:00.0000000Z"
    }))
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

pub async fn entities() -> Json<Value> {
    tracing::debug!("serving entities");
    Json(json!([]))
}

pub async fn space_entities() -> Json<Value> {
    tracing::debug!("serving space entities");
    Json(json!([]))
}

pub async fn me_populations() -> Json<Value> {
    tracing::info!("serving me/populations");
    Json(json!({
        "populations": []
    }))
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
