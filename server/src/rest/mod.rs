mod handlers;

use std::sync::Arc;
use axum::{routing::{get, post}, Router};
use axum::extract::Request;
use axum::response::IntoResponse;
use axum::http::StatusCode;
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
        .route("/v1/users/onlineStatuses",
               get(handlers::online_statuses))
        .route("/v1/profiles/{profile_id}/inventory",
               get(handlers::inventory))
        .route("/v1/profiles/{profile_id}/entities",
               get(handlers::entities))
        .route("/v2/profiles/entities",
               get(handlers::entities))
        .route("/v2/profiles/entities/{entity_id}",
               get(handlers::entities))
        .route("/v2/spaces/{space_id}/entities",
               get(handlers::space_entities))
        .fallback(fallback)
        .with_state(ctx)
}
