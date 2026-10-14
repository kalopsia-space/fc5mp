mod handlers;

use axum::{routing::{get, post}, Router};
use crate::config::Config;
use crate::state::ServerState;

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
        .route("/v1/spaces/{space_id}/sandboxes/{sandbox}/rendezvous/configuration",
               get(handlers::rendezvous_config))
        .route("/v1/router/19", get(handlers::storm_router))
        .with_state(ctx)
}
