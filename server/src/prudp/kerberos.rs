use hmac::{Hmac, Mac};
use md5::Md5;
use std::sync::Arc;

use super::connection::md5_hash;
use super::rc4::Rc4;

type HmacMd5 = Hmac<Md5>;

pub const DUMMY_SESSION_KEY: [u8; 16] = [
    0x9C, 0xB0, 0x1D, 0x7A, 0x2C, 0x5A, 0x6C, 0x5B, 0xED, 0x12, 0x68, 0x45, 0x69, 0xAE, 0x09,
    0x0D,
];

const DUMMY_TICKET_DATA: [u8; 36] = [
    0x76, 0x21, 0x4B, 0xA6, 0x21, 0x96, 0xD3, 0xF3, 0x9A, 0x8C, 0x7A, 0x27, 0x0D, 0xD9, 0xB3,
    0xFA, 0x21, 0x0E, 0xED, 0xAF, 0x42, 0x63, 0x92, 0x95, 0xC1, 0x16, 0x54, 0x08, 0xEE, 0x6E,
    0x69, 0x17, 0x35, 0x78, 0x2E, 0x6E,
];

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

    pub fn generate_ticket_buffer(
        &self,
        user_pid: u32,
        server_pid: u32,
        password: &str,
    ) -> Vec<u8> {
        let key = derive_password_key(user_pid, password);
        let internal = build_ticket_internal(server_pid);
        encrypt_with_key(&internal, &key)
    }

    pub fn session_key(&self) -> &[u8; 16] {
        &DUMMY_SESSION_KEY
    }

    pub fn process_ticket(&self, connect_payload: &[u8]) -> Result<SessionKeys, KerberosError> {
        if connect_payload.len() < 8 {
            return Err(KerberosError::TooShort(connect_payload.len()));
        }

        tracing::info!(
            len = connect_payload.len(),
            first_4 = ?&connect_payload[..4.min(connect_payload.len())],
            "processing Kerberos ticket from CONNECT"
        );

        if connect_payload.len() > 16 {
            let encrypted = &connect_payload[..connect_payload.len() - 16];
            let expected_hmac = &connect_payload[connect_payload.len() - 16..];

            for password in &["UbiDummyPwd", "JaDe!", "h7fyctiuucf"] {
                for pid in &[100u32, 2, 0, 1] {
                    let key = derive_password_key(*pid, password);
                    let mut mac =
                        HmacMd5::new_from_slice(&key).expect("HMAC accepts any key length");
                    mac.update(encrypted);
                    if mac.verify_slice(expected_hmac).is_ok() {
                        let mut decrypted = encrypted.to_vec();
                        Rc4::new(&key).apply(&mut decrypted);
                        if decrypted.len() >= 16 {
                            let session_key = decrypted[..16].to_vec();
                            tracing::info!(pid, password, "Kerberos ticket decrypted");
                            return Ok(SessionKeys {
                                rc4_key: session_key,
                            });
                        }
                    }
                }
            }
        }

        tracing::warn!("could not decrypt Kerberos ticket, using dummy session key");
        Ok(SessionKeys {
            rc4_key: DUMMY_SESSION_KEY.to_vec(),
        })
    }
}

fn build_ticket_internal(server_pid: u32) -> Vec<u8> {
    let mut buf = Vec::with_capacity(16 + 4 + 4 + DUMMY_TICKET_DATA.len());
    buf.extend_from_slice(&DUMMY_SESSION_KEY);
    buf.extend_from_slice(&server_pid.to_le_bytes());
    buf.extend_from_slice(&(DUMMY_TICKET_DATA.len() as u32).to_le_bytes());
    buf.extend_from_slice(&DUMMY_TICKET_DATA);
    buf
}

fn encrypt_with_key(data: &[u8], key: &[u8]) -> Vec<u8> {
    let mut encrypted = data.to_vec();
    Rc4::new(key).apply(&mut encrypted);
    let mut mac = HmacMd5::new_from_slice(key).expect("HMAC accepts any key length");
    mac.update(&encrypted);
    let hmac = mac.finalize().into_bytes();
    let mut result = encrypted;
    result.extend_from_slice(&hmac);
    result
}

fn derive_password_key(pid: u32, password: &str) -> Vec<u8> {
    let iterations = 65000 + (pid as usize % 1024);
    let mut data = md5_hash(password.as_bytes());
    for _ in 1..iterations {
        data = md5_hash(&data);
    }
    data.to_vec()
}

fn base64_encode(data: &[u8]) -> String {
    const CHARS: &[u8] =
        b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity((data.len() + 2) / 3 * 4);
    for chunk in data.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = chunk.get(1).copied().unwrap_or(0) as u32;
        let b2 = chunk.get(2).copied().unwrap_or(0) as u32;
        let triple = (b0 << 16) | (b1 << 8) | b2;
        out.push(CHARS[((triple >> 18) & 0x3F) as usize] as char);
        out.push(CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 {
            out.push(CHARS[((triple >> 6) & 0x3F) as usize] as char);
        } else {
            out.push('=');
        }
        if chunk.len() > 2 {
            out.push(CHARS[(triple & 0x3F) as usize] as char);
        } else {
            out.push('=');
        }
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
        assert!(
            b64.starts_with("RUNT"),
            "should start with base64 of 'ECS1': {b64}"
        );
    }

    #[test]
    fn generate_and_decrypt_ticket() {
        let ks = KerberosServer::new();
        let ticket = ks.generate_ticket_buffer(100, 2, "UbiDummyPwd");
        assert!(ticket.len() > 16, "ticket should have encrypted data + HMAC");

        let key = derive_password_key(100, "UbiDummyPwd");
        let encrypted = &ticket[..ticket.len() - 16];
        let hmac_bytes = &ticket[ticket.len() - 16..];

        let mut mac = HmacMd5::new_from_slice(&key).unwrap();
        mac.update(encrypted);
        assert!(mac.verify_slice(hmac_bytes).is_ok(), "HMAC should verify");

        let mut decrypted = encrypted.to_vec();
        Rc4::new(&key).apply(&mut decrypted);
        assert_eq!(
            &decrypted[..16],
            &DUMMY_SESSION_KEY,
            "session key should match"
        );
    }

    #[test]
    fn process_ticket_with_known_format() {
        let ks = KerberosServer::new();
        let ticket = ks.generate_ticket_buffer(100, 2, "UbiDummyPwd");
        let keys = ks.process_ticket(&ticket).unwrap();
        assert_eq!(keys.rc4_key, DUMMY_SESSION_KEY.to_vec());
    }

    #[test]
    fn process_ticket_fallback_on_unknown() {
        let ks = KerberosServer::new();
        let fake_ticket = b"ECK1\x00\x00\x00\x00some_random_ticket_data_here_padding";
        let keys = ks.process_ticket(fake_ticket).unwrap();
        assert_eq!(
            keys.rc4_key,
            DUMMY_SESSION_KEY.to_vec(),
            "should fall back to dummy key"
        );
    }

    #[test]
    fn too_short_ticket_errors() {
        let ks = KerberosServer::new();
        assert!(ks.process_ticket(b"ECK").is_err());
    }

    #[test]
    fn password_key_derivation_is_deterministic() {
        let key1 = derive_password_key(100, "UbiDummyPwd");
        let key2 = derive_password_key(100, "UbiDummyPwd");
        assert_eq!(key1, key2);
    }

    #[test]
    fn password_key_changes_with_pid() {
        let key1 = derive_password_key(100, "UbiDummyPwd");
        let key2 = derive_password_key(200, "UbiDummyPwd");
        assert_ne!(key1, key2);
    }
}
