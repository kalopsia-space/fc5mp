use hmac::{Hmac, Mac};
use md5::Md5;
use std::sync::Arc;

type HmacMd5 = Hmac<Md5>;

#[derive(Debug, thiserror::Error)]
pub enum KerberosError {
    #[error("ticket too short: {0} bytes")]
    TooShort(usize),
    #[error("invalid ticket magic (expected ECK1)")]
    BadMagic,
    #[error("HMAC verification failed")]
    HmacFailed,
}

pub struct KerberosServer {
    server_key: Vec<u8>,
}

#[derive(Debug, Clone)]
pub struct SessionKeys {
    pub rc4_key: Vec<u8>,
}

impl KerberosServer {
    pub fn new() -> Arc<Self> {
        use rand::RngCore;
        let mut key = vec![0u8; 32];
        rand::thread_rng().fill_bytes(&mut key);
        Arc::new(Self { server_key: key })
    }

    pub fn public_key_blob_b64(&self) -> String {
        let mut blob = Vec::new();
        blob.extend_from_slice(b"ECS1");
        blob.extend_from_slice(&[0u8; 4]);
        blob.extend_from_slice(&self.server_key);
        base64_encode(&blob)
    }

    pub fn process_ticket(&self, connect_payload: &[u8]) -> Result<SessionKeys, KerberosError> {
        if connect_payload.len() < 8 {
            return Err(KerberosError::TooShort(connect_payload.len()));
        }
        if &connect_payload[..4] != b"ECK1" {
            tracing::warn!(
                first_4 = ?&connect_payload[..4.min(connect_payload.len())],
                "ticket doesn't start with ECK1 — format may differ from expected"
            );
        }
        let rc4_key = derive_key(&self.server_key, connect_payload);
        tracing::info!(key_len = rc4_key.len(), "session key derived from ticket");
        Ok(SessionKeys { rc4_key })
    }
}

fn derive_key(server_key: &[u8], ticket_data: &[u8]) -> Vec<u8> {
    let mut mac = HmacMd5::new_from_slice(server_key)
        .expect("HMAC accepts any key length");
    mac.update(ticket_data);
    mac.finalize().into_bytes().to_vec()
}

fn base64_encode(data: &[u8]) -> String {
    const CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity((data.len() + 2) / 3 * 4);
    for chunk in data.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = chunk.get(1).copied().unwrap_or(0) as u32;
        let b2 = chunk.get(2).copied().unwrap_or(0) as u32;
        let triple = (b0 << 16) | (b1 << 8) | b2;
        out.push(CHARS[((triple >> 18) & 0x3F) as usize] as char);
        out.push(CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 { out.push(CHARS[((triple >> 6) & 0x3F) as usize] as char); } else { out.push('='); }
        if chunk.len() > 2 { out.push(CHARS[(triple & 0x3F) as usize] as char); } else { out.push('='); }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn public_key_starts_with_ecs1() {
        let ks = KerberosServer::new();
        let b64 = ks.public_key_blob_b64();
        assert!(b64.starts_with("RUNT"), "should start with base64 of 'ECS1': {b64}");
    }

    #[test]
    fn process_ticket_derives_key() {
        let ks = KerberosServer::new();
        let fake_ticket = b"ECK1\x00\x00\x00\x00some_encrypted_session_key_data";
        let keys = ks.process_ticket(fake_ticket).unwrap();
        assert_eq!(keys.rc4_key.len(), 16);
    }

    #[test]
    fn derive_key_is_deterministic() {
        let key1 = derive_key(b"server_secret", b"ticket_data");
        let key2 = derive_key(b"server_secret", b"ticket_data");
        assert_eq!(key1, key2);
    }

    #[test]
    fn derive_key_changes_with_input() {
        let key1 = derive_key(b"server_secret", b"ticket_A");
        let key2 = derive_key(b"server_secret", b"ticket_B");
        assert_ne!(key1, key2);
    }

    #[test]
    fn too_short_ticket_errors() {
        let ks = KerberosServer::new();
        assert!(ks.process_ticket(b"ECK").is_err());
    }
}
