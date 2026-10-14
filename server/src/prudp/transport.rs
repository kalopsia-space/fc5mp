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

#[derive(Debug, Clone)]
pub struct Packet {
    pub source: u8,
    pub dest: u8,
    pub packet_type: PacketType,
    pub flags: u8,
    pub session_id: u8,
    pub signature: u32,
    pub sequence: u16,
    pub payload: Vec<u8>,
}

#[derive(Debug, thiserror::Error)]
pub enum DecodeError {
    #[error("packet too short: {0} bytes")]
    TooShort(usize),
    #[error("unknown packet type: {0}")]
    UnknownType(u8),
    #[error("bad checksum: expected {expected:#x}, got {got:#x}")]
    BadChecksum { expected: u8, got: u8 },
}

impl Packet {
    pub fn decode(data: &[u8], checksum_key: u32) -> Result<Self, DecodeError> {
        if data.len() < 11 {
            return Err(DecodeError::TooShort(data.len()));
        }
        let source = data[0];
        let dest = data[1];
        let type_flags = data[2];
        let raw_type = type_flags & 0x0F;
        let flags = (type_flags >> 4) & 0x0F;
        let packet_type = PacketType::from_u8(raw_type)
            .ok_or(DecodeError::UnknownType(raw_type))?;
        let session_id = data[3];
        let signature = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
        let sequence = u16::from_le_bytes([data[8], data[9]]);
        let checksum_byte = data[data.len() - 1];
        let expected = compute_checksum(&data[..data.len() - 1], checksum_key);
        if checksum_byte != expected {
            return Err(DecodeError::BadChecksum { expected, got: checksum_byte });
        }
        let payload = data[10..data.len() - 1].to_vec();
        Ok(Self { source, dest, packet_type, flags, session_id, signature, sequence, payload })
    }

    pub fn encode(&self, checksum_key: u32) -> Vec<u8> {
        let type_flags = (self.packet_type as u8) | (self.flags << 4);
        let mut buf = Vec::with_capacity(11 + self.payload.len());
        buf.push(self.source);
        buf.push(self.dest);
        buf.push(type_flags);
        buf.push(self.session_id);
        buf.extend_from_slice(&self.signature.to_le_bytes());
        buf.extend_from_slice(&self.sequence.to_le_bytes());
        buf.extend_from_slice(&self.payload);
        let cs = compute_checksum(&buf, checksum_key);
        buf.push(cs);
        buf
    }
}

pub fn compute_checksum(data: &[u8], key: u32) -> u8 {
    let sum: u32 = key.wrapping_add(
        data.chunks(4)
            .map(|c| {
                let mut arr = [0u8; 4];
                arr[..c.len()].copy_from_slice(c);
                u32::from_le_bytes(arr)
            })
            .fold(0u32, |a, b| a.wrapping_add(b))
    );
    let bytes = sum.to_le_bytes();
    bytes[0].wrapping_add(bytes[1]).wrapping_add(bytes[2]).wrapping_add(bytes[3])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_syn() {
        let pkt = Packet {
            source: 0x3f,
            dest: 0x31,
            packet_type: PacketType::Syn,
            flags: FLAG_NEED_ACK,
            session_id: 0,
            signature: 0,
            sequence: 0,
            payload: vec![],
        };
        let encoded = pkt.encode(0);
        let decoded = Packet::decode(&encoded, 0).unwrap();
        assert_eq!(decoded.source, 0x3f);
        assert_eq!(decoded.dest, 0x31);
        assert_eq!(decoded.packet_type, PacketType::Syn);
        assert_eq!(decoded.sequence, 0);
        assert!(decoded.payload.is_empty());
    }

    #[test]
    fn round_trip_data_with_payload() {
        let pkt = Packet {
            source: 0x3f,
            dest: 0x31,
            packet_type: PacketType::Data,
            flags: FLAG_RELIABLE | FLAG_NEED_ACK | FLAG_HAS_SIZE,
            session_id: 0x1c,
            signature: 0x9d1a89a0,
            sequence: 42,
            payload: vec![0xDE, 0xAD, 0xBE, 0xEF],
        };
        let encoded = pkt.encode(0);
        let decoded = Packet::decode(&encoded, 0).unwrap();
        assert_eq!(decoded.packet_type, PacketType::Data);
        assert_eq!(decoded.session_id, 0x1c);
        assert_eq!(decoded.signature, 0x9d1a89a0);
        assert_eq!(decoded.sequence, 42);
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
            source: 0x3f, dest: 0x31, packet_type: PacketType::Syn,
            flags: 0, session_id: 0, signature: 0, sequence: 0, payload: vec![],
        };
        let mut encoded = pkt.encode(0);
        let last = encoded.len() - 1;
        encoded[last] ^= 0xFF;
        assert!(matches!(Packet::decode(&encoded, 0), Err(DecodeError::BadChecksum { .. })));
    }
}
