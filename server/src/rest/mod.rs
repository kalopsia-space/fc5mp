mod handlers;

use axum::{routing::post, Router};
use crate::state::ServerState;

pub fn router(state: ServerState) -> Router {
    Router::new()
        .route("/v3/profiles/sessions", post(handlers::login))
        .with_state(state)
}
