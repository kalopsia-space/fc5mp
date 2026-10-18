use std::net::SocketAddr;

use super::{hex_preview, RmcResult, RmcService};

pub struct MonitoringService;

impl MonitoringService {
    pub fn new() -> Self {
        Self
    }
}

impl RmcService for MonitoringService {
    fn protocol_id(&self) -> u8 {
        0x13
    }

    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult {
        tracing::info!(
            method_id,
            payload_len = payload.len(),
            %peer,
            payload_hex = %hex_preview(payload, 64),
            "Monitoring request"
        );
        match method_id {
            1 => {
                tracing::debug!("PingDaemon -> ok");
                RmcResult::Response(vec![1u8])
            }
            2 => {
                tracing::debug!("GetClusterMembers -> empty list");
                RmcResult::Response(0u32.to_le_bytes().to_vec())
            }
            _ => {
                tracing::warn!(method_id, "unknown Monitoring method");
                RmcResult::Response(vec![])
            }
        }
    }
}
