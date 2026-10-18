use std::net::SocketAddr;
use std::sync::atomic::{AtomicU32, Ordering};

use super::{ddl_write_string, hex_preview, RmcResult, RmcService};

pub struct SecureConnectionService {
    next_connection_id: AtomicU32,
}

impl SecureConnectionService {
    pub fn new() -> Self {
        Self {
            next_connection_id: AtomicU32::new(10),
        }
    }
}

impl RmcService for SecureConnectionService {
    fn protocol_id(&self) -> u8 {
        0x0B
    }

    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult {
        tracing::info!(
            method_id,
            payload_len = payload.len(),
            %peer,
            payload_hex = %hex_preview(payload, 128),
            "SecureConnection request"
        );
        match method_id {
            1 => self.handle_register(peer),
            2 => {
                tracing::info!("RequestConnectionData - stub");
                RmcResult::Error(0x00010002)
            }
            3 => {
                tracing::info!("RequestUrls - stub");
                RmcResult::Error(0x00010002)
            }
            4 => self.handle_register_ex(peer),
            _ => {
                tracing::warn!(method_id, "unknown SecureConnection method");
                RmcResult::Error(0x00010002)
            }
        }
    }
}

impl SecureConnectionService {
    fn handle_register(&self, peer: SocketAddr) -> RmcResult {
        let conn_id = self.next_connection_id.fetch_add(1, Ordering::Relaxed);
        let public_url = format!(
            "prudp:/address={};port={};CID=1;PID={};sid=1;type=3",
            peer.ip(),
            peer.port(),
            conn_id
        );
        tracing::info!(conn_id, url = %public_url, "Register -> success");

        let mut resp = Vec::new();
        resp.extend_from_slice(&0x00010001u32.to_le_bytes()); // retval
        resp.extend_from_slice(&conn_id.to_le_bytes()); // pidConnectionID
        resp.extend_from_slice(&ddl_write_string(&public_url)); // urlPublic

        RmcResult::Response(resp)
    }

    fn handle_register_ex(&self, peer: SocketAddr) -> RmcResult {
        self.handle_register(peer)
    }
}
