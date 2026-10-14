use std::net::SocketAddr;
use super::rc4::Rc4;
use super::transport::{Packet, PacketType, FLAG_ACK, FLAG_NEED_ACK};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Phase {
    AwaitingSyn,
    AwaitingConnect,
    Connected,
}

#[derive(Debug)]
pub struct Connection {
    pub addr: SocketAddr,
    pub phase: Phase,
    pub session_id: u8,
    pub server_signature: u32,
    pub client_signature: u32,
    pub client_sequence: u16,
    pub server_sequence: u16,
    pub rc4_recv: Option<Rc4>,
    pub rc4_send: Option<Rc4>,
}

impl Connection {
    pub fn new(addr: SocketAddr) -> Self {
        Self {
            addr,
            phase: Phase::AwaitingSyn,
            session_id: 0,
            server_signature: rand::random(),
            client_signature: 0,
            client_sequence: 0,
            server_sequence: 0,
            rc4_recv: None,
            rc4_send: None,
        }
    }

    pub fn handle_syn(&mut self, pkt: &Packet) -> Option<Packet> {
        if self.phase != Phase::AwaitingSyn {
            return None;
        }
        self.client_signature = pkt.signature;
        self.phase = Phase::AwaitingConnect;
        tracing::info!(addr = %self.addr, "SYN received, sending SYN-ACK");
        Some(Packet {
            source: pkt.dest,
            dest: pkt.source,
            packet_type: PacketType::Syn,
            flags: FLAG_ACK | FLAG_NEED_ACK,
            session_id: 0,
            signature: self.server_signature,
            sequence: 0,
            payload: vec![],
        })
    }

    pub fn handle_connect(&mut self, pkt: &Packet) -> Option<Packet> {
        if self.phase != Phase::AwaitingConnect {
            return None;
        }
        self.session_id = pkt.session_id;
        self.phase = Phase::Connected;
        tracing::info!(
            addr = %self.addr,
            session_id = self.session_id,
            payload_len = pkt.payload.len(),
            "CONNECT received (Kerberos ticket), sending CONNECT-ACK"
        );
        Some(Packet {
            source: pkt.dest,
            dest: pkt.source,
            packet_type: PacketType::Connect,
            flags: FLAG_ACK,
            session_id: self.session_id,
            signature: self.server_signature,
            sequence: 0,
            payload: vec![],
        })
    }

    pub fn next_server_sequence(&mut self) -> u16 {
        let seq = self.server_sequence;
        self.server_sequence = self.server_sequence.wrapping_add(1);
        seq
    }

    pub fn make_ack(&self, sequence: u16, source: u8, dest: u8) -> Packet {
        Packet {
            source,
            dest,
            packet_type: PacketType::Data,
            flags: FLAG_ACK,
            session_id: self.session_id,
            signature: self.server_signature,
            sequence,
            payload: vec![],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::prudp::transport::*;

    #[test]
    fn syn_produces_syn_ack() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        let syn = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Syn,
            flags: FLAG_NEED_ACK, session_id: 0, signature: 0, sequence: 0, payload: vec![],
        };
        let resp = conn.handle_syn(&syn).unwrap();
        assert_eq!(resp.packet_type, PacketType::Syn);
        assert!(resp.flags & FLAG_ACK != 0);
        assert_eq!(conn.phase, Phase::AwaitingConnect);
    }

    #[test]
    fn connect_after_syn_succeeds() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        let syn = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Syn,
            flags: FLAG_NEED_ACK, session_id: 0, signature: 0, sequence: 0, payload: vec![],
        };
        conn.handle_syn(&syn);
        let connect = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Connect,
            flags: FLAG_RELIABLE | FLAG_NEED_ACK, session_id: 0x1c,
            signature: 0x12345678, sequence: 1, payload: vec![0x45, 0x43, 0x4b, 0x31],
        };
        let resp = conn.handle_connect(&connect).unwrap();
        assert_eq!(resp.packet_type, PacketType::Connect);
        assert!(resp.flags & FLAG_ACK != 0);
        assert_eq!(conn.phase, Phase::Connected);
        assert_eq!(conn.session_id, 0x1c);
    }

    #[test]
    fn connect_before_syn_returns_none() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        let connect = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Connect,
            flags: 0, session_id: 0x1c, signature: 0, sequence: 1, payload: vec![],
        };
        assert!(conn.handle_connect(&connect).is_none());
    }

    #[test]
    fn sequence_wraps() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        conn.server_sequence = 0xFFFF;
        assert_eq!(conn.next_server_sequence(), 0xFFFF);
        assert_eq!(conn.next_server_sequence(), 0x0000);
    }
}
