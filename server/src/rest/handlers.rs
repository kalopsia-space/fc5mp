use axum::extract::State;
use axum::http::StatusCode;
use axum::Json;
use serde_json::{json, Value};
use uuid::Uuid;

use super::RestContext;

pub async fn login(State(ctx): State<RestContext>) -> (StatusCode, Json<Value>) {
    let profile_id = Uuid::new_v4();
    let session_id = Uuid::new_v4();
    let session_key = base64_rand(32);
    let ticket = base64_rand(64);

    let name = format!("fc5mp-player-{}", &profile_id.to_string()[..8]);
    if let Err(e) = ctx.state.add_player(profile_id, name.clone()) {
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
        "public_key": ctx.kerberos.public_key_blob_b64(),
    }))
}

pub async fn storm_router() -> Json<Value> {
    Json(json!({"router": "127.0.0.1:12000"}))
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
