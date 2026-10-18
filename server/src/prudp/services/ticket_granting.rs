use std::net::SocketAddr;
use std::sync::Arc;

use super::{
    ddl_write_buffer, ddl_write_list_empty, ddl_write_string, hex_preview, RmcResult, RmcService,
};
use crate::prudp::kerberos::KerberosServer;

const USER_PID: u32 = 100;
const SERVER_PID: u32 = 2;

pub struct TicketGrantingService {
    kerberos: Arc<KerberosServer>,
    secure_url: String,
}

impl TicketGrantingService {
    pub fn new(kerberos: Arc<KerberosServer>, prudp_addr: SocketAddr) -> Self {
        let url = format!(
            "prudps:/address={};port={};CID=1;PID={};sid=1;stream=3;type=2",
            prudp_addr.ip(),
            prudp_addr.port(),
            SERVER_PID
        );
        Self {
            kerberos,
            secure_url: url,
        }
    }
}

impl RmcService for TicketGrantingService {
    fn protocol_id(&self) -> u8 {
        0x0A
    }

    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult {
        tracing::info!(
            method_id,
            payload_len = payload.len(),
            %peer,
            payload_hex = %hex_preview(payload, 128),
            "TicketGranting request"
        );
        match method_id {
            1 => self.handle_login(payload),
            2 => self.handle_login_ex(payload),
            3 => self.handle_request_ticket(payload),
            _ => {
                tracing::warn!(method_id, "unknown TicketGranting method");
                RmcResult::Error(0x00010002)
            }
        }
    }
}

impl TicketGrantingService {
    fn handle_login(&self, _payload: &[u8]) -> RmcResult {
        let ticket = self
            .kerberos
            .generate_ticket_buffer(USER_PID, SERVER_PID, "UbiDummyPwd");
        tracing::info!(
            ticket_len = ticket.len(),
            url = %self.secure_url,
            "Login -> success"
        );

        let mut resp = Vec::new();
        // retVal
        resp.extend_from_slice(&0x00010001u32.to_le_bytes());
        // RVConnectionData.m_urlRegularProtocols
        resp.extend_from_slice(&ddl_write_string(&self.secure_url));
        // RVConnectionData.m_lstSpecialProtocols (empty)
        resp.extend_from_slice(&ddl_write_list_empty());
        // RVConnectionData.m_urlSpecialProtocols (empty)
        resp.extend_from_slice(&ddl_write_string(""));
        // RVConnectionData.m_serverTime
        resp.extend_from_slice(&0u64.to_le_bytes());
        // strReturnMsg
        resp.extend_from_slice(&ddl_write_string(""));
        // pbufResponse (Kerberos ticket)
        resp.extend_from_slice(&ddl_write_buffer(&ticket));

        RmcResult::Response(resp)
    }

    fn handle_login_ex(&self, payload: &[u8]) -> RmcResult {
        self.handle_login(payload)
    }

    fn handle_request_ticket(&self, payload: &[u8]) -> RmcResult {
        let source_pid = if payload.len() >= 4 {
            u32::from_le_bytes([payload[0], payload[1], payload[2], payload[3]])
        } else {
            USER_PID
        };

        let target_pid = if payload.len() >= 8 {
            u32::from_le_bytes([payload[4], payload[5], payload[6], payload[7]])
        } else {
            SERVER_PID
        };

        tracing::info!(source_pid, target_pid, "RequestTicket");

        let password = match source_pid {
            0 => "JaDe!",
            100 => "h7fyctiuucf",
            _ => "UbiDummyPwd",
        };

        let ticket = self
            .kerberos
            .generate_ticket_buffer(source_pid, target_pid, password);

        let mut resp = Vec::new();
        resp.extend_from_slice(&0x00010001u32.to_le_bytes());
        resp.extend_from_slice(&ddl_write_buffer(&ticket));

        RmcResult::Response(resp)
    }
}
