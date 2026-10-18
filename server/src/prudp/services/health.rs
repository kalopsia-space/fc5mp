use std::net::SocketAddr;

use super::{hex_preview, RmcResult, RmcService};

pub struct HealthService;

impl HealthService {
    pub fn new() -> Self {
        Self
    }
}

impl RmcService for HealthService {
    fn protocol_id(&self) -> u8 {
        0x12
    }

    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult {
        tracing::info!(
            method_id,
            payload_len = payload.len(),
            %peer,
            payload_hex = %hex_preview(payload, 64),
            "Health request"
        );
        match method_id {
            1 => {
                tracing::debug!("PingDaemon -> ok");
                RmcResult::Response(vec![1u8])
            }
            _ => {
                tracing::warn!(method_id, "unknown Health method");
                RmcResult::Response(vec![])
            }
        }
    }
}
