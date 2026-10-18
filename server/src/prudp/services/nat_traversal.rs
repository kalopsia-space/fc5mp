use std::net::SocketAddr;

use super::{ddl_read_string, ddl_read_u32, hex_preview, RmcResult, RmcService};

pub struct NatTraversalService;

impl NatTraversalService {
    pub fn new() -> Self {
        Self
    }
}

impl RmcService for NatTraversalService {
    fn protocol_id(&self) -> u8 {
        0x03
    }

    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult {
        tracing::info!(
            method_id,
            payload_len = payload.len(),
            %peer,
            payload_hex = %hex_preview(payload, 128),
            "NATTraversal request"
        );
        match method_id {
            1 => self.request_probe_initiation(payload, peer),
            2 => self.initiate_probe(payload),
            3 => self.request_probe_initiation_ext(payload, peer),
            4 => self.report_nat_traversal_result(payload),
            5 => self.report_nat_properties(payload, peer),
            6 => self.get_relay_signature_key(),
            7 => self.report_nat_traversal_result_detail(payload),
            _ => {
                tracing::warn!(method_id, "unknown NATTraversal method");
                RmcResult::Error(0x00010002)
            }
        }
    }
}

impl NatTraversalService {
    fn request_probe_initiation(&self, payload: &[u8], peer: SocketAddr) -> RmcResult {
        let (count, mut off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        tracing::info!(%peer, url_count = count, "RequestProbeInitiation");
        for i in 0..count {
            let (url, new_off) = ddl_read_string(payload, off).unwrap_or((String::new(), off));
            off = new_off;
            tracing::info!(idx = i, url = %url, "  target URL");
        }
        RmcResult::Response(vec![])
    }

    fn initiate_probe(&self, payload: &[u8]) -> RmcResult {
        let (url, _) = ddl_read_string(payload, 0).unwrap_or((String::new(), 0));
        tracing::info!(url = %url, "InitiateProbe (stub)");
        RmcResult::Response(vec![])
    }

    fn request_probe_initiation_ext(&self, payload: &[u8], peer: SocketAddr) -> RmcResult {
        let (count, mut off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        for _ in 0..count {
            let (_, new_off) = ddl_read_string(payload, off).unwrap_or((String::new(), off));
            off = new_off;
        }
        let (station_url, _) = ddl_read_string(payload, off).unwrap_or((String::new(), off));
        tracing::info!(%peer, url_count = count, station = %station_url, "RequestProbeInitiationExt");
        RmcResult::Response(vec![])
    }

    fn report_nat_traversal_result(&self, payload: &[u8]) -> RmcResult {
        let (cid, off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let success = payload.get(off).copied().unwrap_or(0) != 0;
        tracing::info!(cid, success, "ReportNATTraversalResult");
        RmcResult::Response(vec![])
    }

    fn report_nat_properties(&self, payload: &[u8], peer: SocketAddr) -> RmcResult {
        let (nat_mapping, off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let (nat_filtering, off) = ddl_read_u32(payload, off).unwrap_or((0, off));
        let (rtt, _) = ddl_read_u32(payload, off).unwrap_or((0, off));
        tracing::info!(
            %peer,
            nat_mapping,
            nat_filtering,
            rtt,
            "ReportNATProperties"
        );
        RmcResult::Response(vec![])
    }

    fn get_relay_signature_key(&self) -> RmcResult {
        tracing::info!("GetRelaySignatureKey (stub)");
        let mut resp = Vec::new();
        resp.extend_from_slice(&0u32.to_le_bytes()); // mode
        resp.extend_from_slice(&0u64.to_le_bytes()); // utc_time
        resp.extend_from_slice(&super::ddl_write_string("")); // address
        resp.extend_from_slice(&0u16.to_le_bytes()); // port
        resp.extend_from_slice(&0i32.to_le_bytes()); // relay_address_type
        resp.extend_from_slice(&0u32.to_le_bytes()); // game_server_id
        RmcResult::Response(resp)
    }

    fn report_nat_traversal_result_detail(&self, payload: &[u8]) -> RmcResult {
        let (cid, off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let success = payload.get(off).copied().unwrap_or(0) != 0;
        let (detail, off2) = ddl_read_u32(payload, off + 1).unwrap_or((0, off + 1));
        let (rtt, _) = ddl_read_u32(payload, off2).unwrap_or((0, off2));
        tracing::info!(cid, success, detail, rtt, "ReportNATTraversalResultDetail");
        RmcResult::Response(vec![])
    }
}
