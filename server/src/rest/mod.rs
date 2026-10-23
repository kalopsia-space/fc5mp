mod handlers;

use std::sync::Arc;
use axum::{routing::{get, post, put}, Router};
use axum::extract::Request;
use axum::response::IntoResponse;
use axum::http::StatusCode;
use axum::middleware::{self, Next};
use axum::response::Response;
use axum::http::{HeaderName, HeaderValue, header};
use crate::config::Config;
use crate::prudp::kerberos::KerberosServer;
use crate::state::ServerState;

#[derive(Clone)]
pub struct RestContext {
    pub state: ServerState,
    pub prudp_host: String,
    pub prudp_port: u16,
    pub kerberos: Arc<KerberosServer>,
}

async fn fallback(req: Request) -> impl IntoResponse {
    let method = req.method().clone();
    let uri = req.uri().clone();
    let auth = req.headers().get("authorization")
        .and_then(|v| v.to_str().ok())
        .map(|s| s.chars().take(40).collect::<String>())
        .unwrap_or_default();
    let app_id = req.headers().get("ubi-appid")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("")
        .to_string();
    tracing::warn!(
        %method, %uri, %auth, %app_id,
        "unhandled REST request"
    );
    (StatusCode::OK, "{}")
}

async fn request_logger(req: Request, next: Next) -> Response {
    let method = req.method().clone();
    let uri = req.uri().clone();
    tracing::info!(%method, %uri, ">> request");
    let resp = next.run(req).await;
    let status = resp.status();
    tracing::info!(%method, %uri, %status, "<< response");
    resp
}

/// Inject the response headers a real Ubisoft gateway sends. The game SDK
/// expects these on every response (notably Ubi-TransactionId); without them
/// it accepts the body but silently refuses to proceed past app-configuration.
async fn ubi_headers(req: Request, next: Next) -> Response {
    let mut resp = next.run(req).await;
    let h = resp.headers_mut();
    let txid = uuid::Uuid::new_v4().to_string();
    if let Ok(v) = HeaderValue::from_str(&txid) {
        h.insert(HeaderName::from_static("ubi-transactionid"), v);
    }
    h.insert(HeaderName::from_static("ubi-forwarded-by"),
             HeaderValue::from_static("gw-api-fc5mp-0"));
    h.insert(header::SERVER, HeaderValue::from_static("openresty/1.29.2.1"));
    h.insert(HeaderName::from_static("strict-transport-security"),
             HeaderValue::from_static("max-age=63072000"));
    h.insert(HeaderName::from_static("x-frame-options"),
             HeaderValue::from_static("DENY"));
    h.insert(HeaderName::from_static("x-content-type-options"),
             HeaderValue::from_static("nosniff"));
    // Match the real Content-Type exactly (charset=utf-8) for JSON responses.
    if h.get(header::CONTENT_TYPE).map(|v| v == "application/json").unwrap_or(false) {
        h.insert(header::CONTENT_TYPE,
                 HeaderValue::from_static("application/json; charset=utf-8"));
    }
    resp
}

pub fn router(state: ServerState, config: &Config, kerberos: Arc<KerberosServer>) -> Router {
    let ctx = RestContext {
        state,
        prudp_host: config.listen_ip.to_string(),
        prudp_port: config.prudp_port,
        kerberos,
    };
    Router::new()
        .route("/v3/profiles/sessions", post(handlers::login))
        .route("/v1/spaces/{space_id}/sandboxes/{sandbox}/rendezvous/configuration",
               get(handlers::rendezvous_config))
        .route("/v1/router/19", get(handlers::storm_router))
        .route("/v1/spaces/{space_id}/global/ubiconnect/configcache/api/connecturls",
               get(handlers::connect_urls))
        .route("/v2/profiles/me/iplocation", get(handlers::ip_location))
        .route("/v1/applications/{app_id}/configuration",
               get(handlers::app_configuration))
        .route("/v2/applications/{app_id}/configuration",
               get(handlers::app_configuration))
        .route("/v3/users/me", get(handlers::users_me))
        .route("/v2/profiles/me/2fa", get(handlers::twofa_status))
        .route("/v1/users/{user_id}/subscriptions",
               get(handlers::user_subscriptions))
        .route("/v3/users/me/profiles", get(handlers::user_profiles))
        .route("/v2/users/{user_id}/profiles",
               post(handlers::link_steam_profile))
        .route("/v5/spaces/{space_id}/serviceEvents",
               post(handlers::service_events))
        .route("/v3/profiles/{profile_id}/ownership",
               get(handlers::ownership))
        .route("/v3/profiles/{profile_id}/friends",
               get(handlers::friends))
        .route("/v1/profiles/{profile_id}/blocks",
               get(handlers::blocklist))
        .route("/v1/spaces/{space_id}/parameters",
               get(handlers::parameters))
        .route("/v1/profiles/me/populations",
               get(handlers::me_populations))
        .route("/v1/profiles/me/populations/data",
               put(handlers::populations_data))
        .route("/v1/profiles/me/remotelog",
               post(handlers::remote_log))
        .route("/v1/spaces/{space_id}/populations",
               get(handlers::populations))
        .route("/v4/spaces/{space_id}/configs/events",
               get(handlers::events_config))
        .route("/orbit/uplay_launcher_14_0/free_games/latest.txt",
               get(handlers::free_games))
        .route("/v1/applications/{app_id}/global/sre/opentelemetry/anon/metrics",
               post(handlers::opentelemetry_metrics))
        .route("/v2/applications/{app_id}/global/sre/opentelemetry/api/metrics",
               post(handlers::opentelemetry_metrics))
        .route("/v2/websocket/server",
               get(handlers::websocket_handler))
        // Real game opens GET /v2/websocket?NotificationTypes=... (query, not /server).
        .route("/v2/websocket",
               get(handlers::websocket_handler))
        // Game PUTs here (the contactUrl from US_CONNECTION_ESTABLISHED) to register
        // for push notifications; segments are opaque, so wildcard them.
        .route("/v2/connections/{conn}/{inst}/{sub}/notifications",
               put(handlers::push_notification_ack))
        // Critical-path fetches after login in the real flow (were 404ing).
        .route("/v1/spaces/{space_id}/items/all",
               get(handlers::items_all))
        .route("/v1/profiles/{profile_id}/rewards",
               get(handlers::rewards))
        .route("/v1/users/onlineStatuses",
               get(handlers::online_statuses))
        .route("/v1/profiles/{profile_id}/inventory",
               get(handlers::inventory))
        // --- Entity storage (stateful, from TCU cross-reference) ---
        .route("/v1/profiles/{profile_id}/entities",
               get(handlers::get_profile_entities).put(handlers::put_entity).post(handlers::put_entity))
        .route("/v2/profiles/entities",
               get(handlers::entities))
        .route("/v2/profiles/entities/{entity_id}",
               get(handlers::get_entity_by_id))
        .route("/v1/profiles/entities/{entity_id}",
               get(handlers::get_entity_by_id))
        .route("/v2/spaces/{space_id}/entities",
               get(handlers::get_space_entities_stateful))
        // OAuth token (SDK calls this for Bearer token refresh)
        .route("/native/token/",
               get(handlers::native_token).post(handlers::native_token))
        // Profile lookup by ?profileId=uuid,uuid (co-op player resolution)
        .route("/v2/profiles",
               get(handlers::profiles_by_id))
        .fallback(fallback)
        .layer(middleware::from_fn(request_logger))
        .layer(middleware::from_fn(ubi_headers))
        .with_state(ctx)
}
