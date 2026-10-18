use std::net::SocketAddr;
use super::rc4::Rc4;
use super::transport::{Packet, PacketType, FLAG_ACK, FLAG_MULTI_ACK};

fn compute_connection_signature(addr: &SocketAddr) -> u32 {
    match addr {
        SocketAddr::V4(v4) => {
            let ip = v4.ip().octets();
            let port = v4.port().to_be_bytes();
            let data = [ip[0], ip[1], ip[2], ip[3], port[0], port[1]];
            let hash = md5_hash(&data);
            u32::from_be_bytes([hash[3], hash[2], hash[1], hash[0]])
        }
        _ => rand::random(),
    }
}

pub(crate) fn md5_hash(data: &[u8]) -> [u8; 16] {
    let mut state: [u32; 4] = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476];
    let bit_len = (data.len() as u64) * 8;
    let mut padded = data.to_vec();
    padded.push(0x80);
    while padded.len() % 64 != 56 {
        padded.push(0);
    }
    padded.extend_from_slice(&bit_len.to_le_bytes());

    static S: [u32; 64] = [
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21,
    ];
    static K: [u32; 64] = [
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391,
    ];

    for chunk in padded.chunks(64) {
        let mut m = [0u32; 16];
        for i in 0..16 {
            m[i] = u32::from_le_bytes([chunk[i*4], chunk[i*4+1], chunk[i*4+2], chunk[i*4+3]]);
        }
        let (mut a, mut b, mut c, mut d) = (state[0], state[1], state[2], state[3]);
        for i in 0..64 {
            let (f, g) = match i {
                0..=15 => ((b & c) | ((!b) & d), i),
                16..=31 => ((d & b) | ((!d) & c), (5*i + 1) % 16),
                32..=47 => (b ^ c ^ d, (3*i + 5) % 16),
                _ => (c ^ (b | (!d)), (7*i) % 16),
            };
            let temp = d;
            d = c;
            c = b;
            b = b.wrapping_add(
                a.wrapping_add(f).wrapping_add(K[i]).wrapping_add(m[g])
                    .rotate_left(S[i])
            );
            a = temp;
        }
        state[0] = state[0].wrapping_add(a);
        state[1] = state[1].wrapping_add(b);
        state[2] = state[2].wrapping_add(c);
        state[3] = state[3].wrapping_add(d);
    }

    let mut out = [0u8; 16];
    for (i, &s) in state.iter().enumerate() {
        out[i*4..i*4+4].copy_from_slice(&s.to_le_bytes());
    }
    out
}

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
    pub client_session_id: u8,
    pub server_session_id: u8,
    pub session_id: u8,
    pub server_signature: u32,
    pub client_signature: u32,
    pub client_sequence: u16,
    pub server_sequence: u16,
    pub rc4_recv: Option<Rc4>,
    pub rc4_send: Option<Rc4>,
    pub cached_connect_ack: Option<Packet>,
}

impl Connection {
    pub fn new(addr: SocketAddr) -> Self {
        Self {
            addr,
            phase: Phase::AwaitingSyn,
            client_session_id: 0,
            server_session_id: rand::random(),
            session_id: 0,
            server_signature: compute_connection_signature(&addr),
            client_signature: 0,
            client_sequence: 0,
            server_sequence: 0,
            rc4_recv: None,
            rc4_send: None,
            cached_connect_ack: None,
        }
    }

    pub fn handle_syn(&mut self, pkt: &Packet) -> Option<Packet> {
        if self.phase != Phase::AwaitingSyn {
            return None;
        }
        self.client_signature = pkt.connection_signature;
        self.phase = Phase::AwaitingConnect;
        tracing::info!(addr = %self.addr, "SYN received, sending SYN-ACK");
        Some(Packet {
            source: pkt.dest,
            dest: pkt.source,
            packet_type: PacketType::Syn,
            flags: FLAG_ACK,
            session_id: 0,
            signature: 0,
            sequence: 0,
            fragment_id: 0,
            connection_signature: self.server_signature,
            payload: vec![],
        })
    }

    pub fn handle_connect(&mut self, pkt: &Packet) -> Option<Packet> {
        if self.phase == Phase::Connected {
            tracing::debug!(addr = %self.addr, "CONNECT retry, resending cached CONNECT-ACK");
            return self.cached_connect_ack.clone();
        }
        if self.phase != Phase::AwaitingConnect {
            return None;
        }
        self.client_session_id = pkt.session_id;
        self.session_id = pkt.session_id;
        self.phase = Phase::Connected;
        tracing::info!(
            addr = %self.addr,
            client_session = self.client_session_id,
            server_session = self.server_session_id,
            payload_len = pkt.payload.len(),
            "CONNECT received (Kerberos ticket), sending CONNECT-ACK"
        );
        let resp = Packet {
            source: pkt.dest,
            dest: pkt.source,
            packet_type: PacketType::Connect,
            flags: FLAG_ACK | FLAG_MULTI_ACK,
            session_id: self.server_session_id,
            signature: pkt.connection_signature,
            sequence: 1,
            fragment_id: 0,
            connection_signature: 0,
            payload: build_connect_response(&pkt.payload),
        };
        self.cached_connect_ack = Some(resp.clone());
        Some(resp)
    }

    pub fn next_server_sequence(&mut self) -> u16 {
        let seq = self.server_sequence;
        self.server_sequence = self.server_sequence.wrapping_add(1);
        seq
    }

    pub fn cached_connect_ack(&self) -> Option<&Packet> {
        self.cached_connect_ack.as_ref()
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
            fragment_id: 0,
            connection_signature: 0,
            payload: vec![],
        }
    }
}

fn build_connect_response(connect_payload: &[u8]) -> Vec<u8> {
    let mut resp = Vec::new();
    let mut response_data = vec![0u8; 64];
    rand::RngCore::fill_bytes(&mut rand::thread_rng(), &mut response_data);
    resp.extend_from_slice(&(response_data.len() as u16).to_le_bytes());
    resp.extend_from_slice(&response_data);

    let ticket = if connect_payload.len() >= 2 {
        let prefix_len = u16::from_le_bytes([connect_payload[0], connect_payload[1]]) as usize;
        if 2 + prefix_len <= connect_payload.len() {
            &connect_payload[2..2 + prefix_len]
        } else {
            connect_payload
        }
    } else {
        connect_payload
    };
    let mut token = Vec::with_capacity(72);
    token.extend_from_slice(b"ECK1");
    token.extend_from_slice(&32u32.to_le_bytes());
    let mut token_data = vec![0u8; 64];
    rand::RngCore::fill_bytes(&mut rand::thread_rng(), &mut token_data);
    let _ = ticket; // We echo the structure but with random data since we lack real keys
    token.extend_from_slice(&token_data);
    resp.extend_from_slice(&(token.len() as u16).to_le_bytes());
    resp.extend_from_slice(&token);
    resp
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
            flags: FLAG_NEED_ACK, session_id: 0, signature: 0, sequence: 0,
            fragment_id: 0, connection_signature: 0, payload: vec![],
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
            flags: FLAG_NEED_ACK, session_id: 0, signature: 0, sequence: 0,
            fragment_id: 0, connection_signature: 0, payload: vec![],
        };
        conn.handle_syn(&syn);
        let connect = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Connect,
            flags: FLAG_RELIABLE | FLAG_NEED_ACK, session_id: 0x1c,
            signature: 0x12345678, sequence: 1, fragment_id: 0,
            connection_signature: 0xAABBCCDD, payload: vec![0x45, 0x43, 0x4b, 0x31],
        };
        let resp = conn.handle_connect(&connect).unwrap();
        assert_eq!(resp.packet_type, PacketType::Connect);
        assert!(resp.flags & FLAG_ACK != 0);
        assert!(resp.flags & FLAG_MULTI_ACK != 0);
        assert_eq!(resp.sequence, 1);
        assert_eq!(conn.phase, Phase::Connected);
        assert_eq!(conn.client_session_id, 0x1c);
        assert_ne!(resp.session_id, 0x1c); // server generates its own session_id
        assert!(!resp.payload.is_empty());
    }

    #[test]
    fn connect_before_syn_returns_none() {
        let mut conn = Connection::new("127.0.0.1:9999".parse().unwrap());
        let connect = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Connect,
            flags: 0, session_id: 0x1c, signature: 0, sequence: 1,
            fragment_id: 0, connection_signature: 0, payload: vec![],
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
