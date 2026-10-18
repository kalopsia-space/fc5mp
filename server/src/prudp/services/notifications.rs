use std::net::SocketAddr;

use super::{hex_preview, RmcResult, RmcService};

pub struct NotificationService;

impl NotificationService {
    pub fn new() -> Self {
        Self
    }
}

impl RmcService for NotificationService {
    fn protocol_id(&self) -> u8 {
        0x0E
    }

    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult {
        tracing::info!(
            method_id,
            payload_len = payload.len(),
            %peer,
            payload_hex = %hex_preview(payload, 128),
            "Notification request (unexpected client->server call)"
        );
        RmcResult::Response(vec![])
    }
}
