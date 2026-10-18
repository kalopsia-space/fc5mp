#[derive(Debug, thiserror::Error)]
pub enum RmcError {
    #[error("RMC message too short: {0} bytes")]
    TooShort(usize),
    #[error("declared size {declared} exceeds data length {actual}")]
    SizeMismatch { declared: usize, actual: usize },
}

#[derive(Debug, Clone)]
pub struct RmcRequest {
    pub protocol_id: u8,
    pub call_id: u32,
    pub method_id: u32,
    pub payload: Vec<u8>,
}

impl RmcRequest {
    pub fn decode(data: &[u8]) -> Result<Self, RmcError> {
        if data.len() < 13 {
            return Err(RmcError::TooShort(data.len()));
        }
        let size = u32::from_le_bytes([data[0], data[1], data[2], data[3]]) as usize;
        if size + 4 > data.len() {
            return Err(RmcError::SizeMismatch { declared: size, actual: data.len() - 4 });
        }
        let protocol_id = data[4] & 0x7F;
        let is_request = data[4] & 0x80 != 0;
        if !is_request {
            tracing::debug!(protocol_id, "received RMC response (unexpected on server side)");
        }
        let call_id = u32::from_le_bytes([data[5], data[6], data[7], data[8]]);
        let method_id = u32::from_le_bytes([data[9], data[10], data[11], data[12]]);
        let payload = data[13..4 + size].to_vec();

        tracing::info!(
            protocol_id,
            method_id,
            call_id,
            payload_len = payload.len(),
            payload_hex = %hex_preview(&payload, 64),
            "RMC request received"
        );

        Ok(Self { protocol_id, call_id, method_id, payload })
    }
}

pub fn encode_response(call_id: u32, protocol_id: u8, method_id: u32, result_data: &[u8]) -> Vec<u8> {
    let body_size = 1 + 1 + 4 + 4 + result_data.len();
    let mut out = Vec::with_capacity(4 + body_size);
    out.extend_from_slice(&(body_size as u32).to_le_bytes());
    out.push(protocol_id);
    out.push(0x01);
    out.extend_from_slice(&call_id.to_le_bytes());
    out.extend_from_slice(&(method_id | 0x8000).to_le_bytes());
    out.extend_from_slice(result_data);
    out
}

pub fn encode_error(call_id: u32, protocol_id: u8, error_code: u32) -> Vec<u8> {
    let body_size = 1 + 1 + 4 + 4;
    let mut out = Vec::with_capacity(4 + body_size);
    out.extend_from_slice(&(body_size as u32).to_le_bytes());
    out.push(protocol_id);
    out.push(0x00);
    out.extend_from_slice(&error_code.to_le_bytes());
    out.extend_from_slice(&call_id.to_le_bytes());
    out
}

fn hex_preview(data: &[u8], max: usize) -> String {
    let show = data.len().min(max);
    let hex: String = data[..show].iter().map(|b| format!("{b:02x}")).collect::<Vec<_>>().join(" ");
    if data.len() > max { format!("{hex} ...({} more)", data.len() - max) } else { hex }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decode_request() {
        let mut data = vec![];
        let body_size: u32 = 9 + 4;
        data.extend_from_slice(&body_size.to_le_bytes());
        data.push(0x80 | 10);
        data.extend_from_slice(&42u32.to_le_bytes());
        data.extend_from_slice(&3u32.to_le_bytes());
        data.extend_from_slice(b"test");
        let req = RmcRequest::decode(&data).unwrap();
        assert_eq!(req.protocol_id, 10);
        assert_eq!(req.call_id, 42);
        assert_eq!(req.method_id, 3);
        assert_eq!(req.payload, b"test");
    }

    #[test]
    fn decode_too_short() {
        assert!(RmcRequest::decode(&[0; 5]).is_err());
    }

    #[test]
    fn response_round_trip() {
        let resp = encode_response(42, 10, 3, b"result");
        assert!(resp.len() > 13);
        let size = u32::from_le_bytes([resp[0], resp[1], resp[2], resp[3]]) as usize;
        assert_eq!(size + 4, resp.len());
    }
}
