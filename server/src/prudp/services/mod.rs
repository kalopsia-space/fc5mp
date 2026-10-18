pub mod health;
pub mod matchmaking;
pub mod monitoring;
pub mod nat_traversal;
pub mod notifications;
pub mod secure_connection;
pub mod ticket_granting;

use super::rmc::{self, RmcRequest};
use std::collections::HashMap;
use std::net::SocketAddr;

pub enum RmcResult {
    Response(Vec<u8>),
    Error(u32),
}

pub trait RmcService: Send + Sync {
    fn protocol_id(&self) -> u8;
    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult;
}

pub struct ServiceDispatcher {
    services: HashMap<u8, Box<dyn RmcService>>,
}

impl ServiceDispatcher {
    pub fn new() -> Self {
        Self { services: HashMap::new() }
    }

    pub fn register<S: RmcService + 'static>(&mut self, service: S) {
        let id = service.protocol_id();
        tracing::info!(protocol_id = id, "registered RMC service");
        self.services.insert(id, Box::new(service));
    }

    pub fn dispatch(&self, req: &RmcRequest, peer: SocketAddr) -> Vec<u8> {
        match self.services.get(&req.protocol_id) {
            Some(svc) => match svc.handle(req.method_id, &req.payload, peer) {
                RmcResult::Response(data) => {
                    tracing::debug!(
                        protocol = req.protocol_id, method = req.method_id,
                        call_id = req.call_id, resp_len = data.len(),
                        "RMC response (success)"
                    );
                    rmc::encode_response(req.call_id, req.protocol_id, req.method_id, &data)
                }
                RmcResult::Error(code) => {
                    tracing::debug!(
                        protocol = req.protocol_id, method = req.method_id,
                        call_id = req.call_id, error_code = code,
                        "RMC response (error)"
                    );
                    rmc::encode_error(req.call_id, req.protocol_id, code)
                }
            },
            None => {
                tracing::warn!(
                    protocol = req.protocol_id, method = req.method_id,
                    call_id = req.call_id, payload_len = req.payload.len(),
                    "unhandled RMC protocol"
                );
                rmc::encode_error(req.call_id, req.protocol_id, 0x00010002)
            }
        }
    }
}

pub fn ddl_write_string(s: &str) -> Vec<u8> {
    let bytes = s.as_bytes();
    let len = (bytes.len() + 1) as u16;
    let mut out = Vec::with_capacity(2 + bytes.len() + 1);
    out.extend_from_slice(&len.to_le_bytes());
    out.extend_from_slice(bytes);
    out.push(0);
    out
}

pub fn ddl_write_buffer(data: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(4 + data.len());
    out.extend_from_slice(&(data.len() as u32).to_le_bytes());
    out.extend_from_slice(data);
    out
}

pub fn ddl_write_list_empty() -> Vec<u8> {
    0u32.to_le_bytes().to_vec()
}

pub fn ddl_read_u32(data: &[u8], offset: usize) -> Option<(u32, usize)> {
    if offset + 4 > data.len() {
        return None;
    }
    let val = u32::from_le_bytes([
        data[offset],
        data[offset + 1],
        data[offset + 2],
        data[offset + 3],
    ]);
    Some((val, offset + 4))
}

pub fn ddl_read_u16(data: &[u8], offset: usize) -> Option<(u16, usize)> {
    if offset + 2 > data.len() {
        return None;
    }
    let val = u16::from_le_bytes([data[offset], data[offset + 1]]);
    Some((val, offset + 2))
}

pub fn ddl_read_string(data: &[u8], offset: usize) -> Option<(String, usize)> {
    let (len, off) = ddl_read_u16(data, offset)?;
    let len = len as usize;
    if off + len > data.len() {
        return None;
    }
    let str_bytes = if len > 0 { &data[off..off + len - 1] } else { &[] };
    let s = String::from_utf8_lossy(str_bytes).into_owned();
    Some((s, off + len))
}

pub fn hex_preview(data: &[u8], max: usize) -> String {
    let show = data.len().min(max);
    let hex: String = data[..show]
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect::<Vec<_>>()
        .join(" ");
    if data.len() > max {
        format!("{hex} ...({} more)", data.len() - max)
    } else {
        hex
    }
}
