use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PacketType {
    Syn = 0,
    Connect = 1,
    Data = 2,
    Disconnect = 3,
    Ping = 4,
}

impl PacketType {
    fn from_u8(v: u8) -> Option<Self> {
        match v {
            0 => Some(Self::Syn),
            1 => Some(Self::Connect),
            2 => Some(Self::Data),
            3 => Some(Self::Disconnect),
            4 => Some(Self::Ping),
            _ => None,
        }
    }
}

impl fmt::Display for PacketType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Syn => write!(f, "SYN"),
            Self::Connect => write!(f, "CONNECT"),
            Self::Data => write!(f, "DATA"),
            Self::Disconnect => write!(f, "DISCONNECT"),
            Self::Ping => write!(f, "PING"),
        }
    }
}

pub const FLAG_ACK: u8 = 0x1;
pub const FLAG_RELIABLE: u8 = 0x2;
pub const FLAG_NEED_ACK: u8 = 0x4;
pub const FLAG_HAS_SIZE: u8 = 0x8;

const CHECKSUM_SIZE: usize = 4;

/// PRUDP V0 packet (Quazal Rendez-Vous wire format).
///
/// Header (10 bytes):
///   source(1) + dest(1) + type_flags(1) + session_id(1)
///   + packet_signature(4) + sequence(2)
///
/// type_flags: (flags << 3) | type
///
/// After header, packet-type-specific data:
///   SYN/CONNECT: connection_signature(4)
///   DATA:        fragment_id(1)
///   others:      nothing
///
/// Then payload, then 4-byte Quazal checksum.
#[derive(Debug, Clone)]
pub struct Packet {
    pub source: u8,
    pub dest: u8,
    pub packet_type: PacketType,
    pub flags: u8,
    pub session_id: u8,
    pub signature: u32,
    pub sequence: u16,
    pub fragment_id: u8,
    pub connection_signature: u32,
    pub payload: Vec<u8>,
}

#[derive(Debug, thiserror::Error)]
pub enum DecodeError {
    #[error("packet too short: {0} bytes")]
    TooShort(usize),
    #[error("unknown packet type: {0}")]
    UnknownType(u8),
    #[error("bad checksum: expected {expected:#x}, got {got:#x}")]
    BadChecksum { expected: u32, got: u32 },
}

const HEADER_SIZE: usize = 10;

impl Packet {
    pub fn decode(data: &[u8], access_key_sum: u8) -> Result<Self, DecodeError> {
        if data.len() < HEADER_SIZE + CHECKSUM_SIZE {
            return Err(DecodeError::TooShort(data.len()));
        }
        let source = data[0];
        let dest = data[1];
        let type_flags = data[2];
        let raw_type = type_flags & 0x07;
        let flags = type_flags >> 3;
        let packet_type = PacketType::from_u8(raw_type)
            .ok_or(DecodeError::UnknownType(raw_type))?;
        let session_id = data[3];
        let signature = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        let sequence = u16::from_le_bytes([data[8], data[9]]);

        let (connection_signature, fragment_id, payload_start) = match packet_type {
            PacketType::Syn | PacketType::Connect => {
                if data.len() < HEADER_SIZE + 4 + CHECKSUM_SIZE {
                    return Err(DecodeError::TooShort(data.len()));
                }
                let cs = u32::from_le_bytes([data[10], data[11], data[12], data[13]]);
                (cs, 0, 14)
            }
            PacketType::Data => {
                if data.len() < HEADER_SIZE + 1 + CHECKSUM_SIZE {
                    return Err(DecodeError::TooShort(data.len()));
                }
                (0, data[10], 11)
            }
            _ => (0, 0, 10),
        };

        let cs_offset = data.len() - CHECKSUM_SIZE;
        let got = u32::from_le_bytes([data[cs_offset], data[cs_offset+1], data[cs_offset+2], data[cs_offset+3]]);
        let expected = compute_checksum(&data[..cs_offset], access_key_sum);
        if got != expected && !(packet_type == PacketType::Syn) {
            return Err(DecodeError::BadChecksum { expected, got });
        }
        let payload = data[payload_start..cs_offset].to_vec();
        Ok(Self {
            source, dest, packet_type, flags, session_id, signature, sequence,
            fragment_id, connection_signature, payload,
        })
    }

    pub fn encode(&self, access_key_sum: u8) -> Vec<u8> {
        let type_flags = (self.packet_type as u8) | (self.flags << 3);
        let mut buf = Vec::with_capacity(18 + self.payload.len());
        buf.push(self.source);
        buf.push(self.dest);
        buf.push(type_flags);
        buf.push(self.session_id);
        buf.extend_from_slice(&self.signature.to_le_bytes());
        buf.extend_from_slice(&self.sequence.to_le_bytes());

        match self.packet_type {
            PacketType::Syn | PacketType::Connect => {
                buf.extend_from_slice(&self.connection_signature.to_le_bytes());
            }
            PacketType::Data => {
                buf.push(self.fragment_id);
            }
            _ => {}
        }

        buf.extend_from_slice(&self.payload);
        let cs = compute_checksum(&buf, access_key_sum);
        buf.extend_from_slice(&cs.to_le_bytes());
        buf
    }
}

/// Quazal Rendez-Vous 4-byte checksum.
/// Sums complete u32 LE words, discards any trailing bytes that don't
/// form a full word, then adds access_key_sum.
pub fn compute_checksum(data: &[u8], access_key_sum: u8) -> u32 {
    let word_sum: u32 = data
        .chunks_exact(4)
        .map(|c| u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
        .fold(0u32, |a, b| a.wrapping_add(b));
    (access_key_sum as u32).wrapping_add(word_sum)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_syn() {
        let pkt = Packet {
            source: 0x3f, dest: 0x31,
            packet_type: PacketType::Syn,
            flags: FLAG_NEED_ACK,
            session_id: 0, signature: 0, sequence: 0,
            fragment_id: 0, connection_signature: 0,
            payload: vec![],
        };
        let encoded = pkt.encode(0);
        // 10 header + 4 connection_sig + 4 checksum = 18
        assert_eq!(encoded.len(), 18);
        assert_eq!(encoded[2], (FLAG_NEED_ACK << 3) | 0); // 0x20
        let decoded = Packet::decode(&encoded, 0).unwrap();
        assert_eq!(decoded.source, 0x3f);
        assert_eq!(decoded.dest, 0x31);
        assert_eq!(decoded.packet_type, PacketType::Syn);
        assert_eq!(decoded.flags, FLAG_NEED_ACK);
        assert_eq!(decoded.sequence, 0);
        assert_eq!(decoded.connection_signature, 0);
        assert!(decoded.payload.is_empty());
    }

    #[test]
    fn round_trip_data_with_payload() {
        let pkt = Packet {
            source: 0x3f, dest: 0x31,
            packet_type: PacketType::Data,
            flags: FLAG_RELIABLE | FLAG_NEED_ACK | FLAG_HAS_SIZE,
            session_id: 0x1c, signature: 0x9d1a89a0, sequence: 42,
            fragment_id: 0, connection_signature: 0,
            payload: vec![0xDE, 0xAD, 0xBE, 0xEF],
        };
        let encoded = pkt.encode(0);
        let decoded = Packet::decode(&encoded, 0).unwrap();
        assert_eq!(decoded.packet_type, PacketType::Data);
        assert_eq!(decoded.session_id, 0x1c);
        assert_eq!(decoded.signature, 0x9d1a89a0);
        assert_eq!(decoded.sequence, 42);
        assert_eq!(decoded.fragment_id, 0);
        assert_eq!(decoded.payload, vec![0xDE, 0xAD, 0xBE, 0xEF]);
    }

    #[test]
    fn decode_rejects_truncated() {
        let short = vec![0x3f, 0x31, 0x20, 0x00];
        assert!(Packet::decode(&short, 0).is_err());
    }

    #[test]
    fn decode_rejects_bad_checksum() {
        let pkt = Packet {
            source: 0x3f, dest: 0x31, packet_type: PacketType::Data,
            flags: FLAG_RELIABLE, session_id: 0, signature: 0, sequence: 0,
            fragment_id: 0, connection_signature: 0, payload: vec![1, 2, 3],
        };
        let mut encoded = pkt.encode(0);
        let last = encoded.len() - 1;
        encoded[last] ^= 0xFF;
        assert!(matches!(Packet::decode(&encoded, 0), Err(DecodeError::BadChecksum { .. })));
    }

    #[test]
    fn decode_game_syn() {
        let raw: Vec<u8> = vec![
            0x3f, 0x31, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x31,
            0x20, 0x00,
        ];
        let pkt = Packet::decode(&raw, 0).unwrap();
        assert_eq!(pkt.source, 0x3f);
        assert_eq!(pkt.dest, 0x31);
        assert_eq!(pkt.packet_type, PacketType::Syn);
        assert_eq!(pkt.flags, FLAG_NEED_ACK);
        assert_eq!(pkt.session_id, 0);
        assert_eq!(pkt.signature, 0);
        assert_eq!(pkt.connection_signature, 0);
        assert!(pkt.payload.is_empty());
    }

    #[test]
    fn syn_ack_encoding() {
        let pkt = Packet {
            source: 0x31, dest: 0x3f,
            packet_type: PacketType::Syn,
            flags: FLAG_ACK,
            session_id: 0, signature: 0, sequence: 0,
            fragment_id: 0, connection_signature: 0x12345678,
            payload: vec![],
        };
        let encoded = pkt.encode(0);
        assert_eq!(encoded[2], 0x08); // (ACK << 3) | SYN
        // 10 header + 4 conn_sig + 4 checksum = 18
        assert_eq!(encoded.len(), 18);
        // Verify round-trip
        let decoded = Packet::decode(&encoded, 0).unwrap();
        assert_eq!(decoded.packet_type, PacketType::Syn);
        assert_eq!(decoded.flags, FLAG_ACK);
        assert_eq!(decoded.connection_signature, 0x12345678);
    }

    #[test]
    fn quazal_checksum_matches_game_syn() {
        // The game's SYN data (before checksum): 14 bytes
        let data = vec![
            0x3f, 0x31, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        ];
        let cs = compute_checksum(&data, 0);
        // Expected: u32 LE [3f, 31, 20, 00] = 0x0020313f
        assert_eq!(cs, 0x0020313f);
    }
}
