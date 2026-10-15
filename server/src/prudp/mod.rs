pub mod connection;
pub mod kerberos;
pub mod rc4;
pub mod rmc;
pub mod transport;

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::net::UdpSocket;
use connection::{Connection, Phase};
use kerberos::KerberosServer;
use transport::{Packet, PacketType, FLAG_NEED_ACK, FLAG_RELIABLE, FLAG_HAS_SIZE};

pub async fn run(addr: SocketAddr, kerberos: Arc<KerberosServer>) -> anyhow::Result<()> {
    let sock = UdpSocket::bind(addr).await?;
    tracing::info!(%addr, "PRUDP server listening");
    let mut buf = [0u8; 2048];
    let mut connections: HashMap<SocketAddr, Connection> = HashMap::new();
    let access_key_sum: u8 = 0;

    loop {
        let (len, peer) = match sock.recv_from(&mut buf).await {
            Ok(v) => v,
            Err(e) => {
                tracing::warn!(%e, "PRUDP recv error (transient), continuing");
                continue;
            }
        };
        let data = &buf[..len];

        let pkt = match Packet::decode(data, access_key_sum) {
            Ok(p) => p,
            Err(e) => {
                tracing::warn!(%peer, %e, bytes = len, "dropping malformed PRUDP packet");
                continue;
            }
        };

        tracing::debug!(
            %peer, typ = %pkt.packet_type, seq = pkt.sequence,
            flags = pkt.flags, payload_len = pkt.payload.len(),
            "PRUDP <- recv"
        );

        if pkt.packet_type == PacketType::Syn {
            if let Some(old) = connections.get(&peer) {
                if old.phase != Phase::AwaitingSyn {
                    tracing::info!(%peer, old_phase = ?old.phase, "SYN from known peer, resetting connection");
                    connections.insert(peer, Connection::new(peer));
                }
            }
        }

        let conn = connections.entry(peer).or_insert_with(|| Connection::new(peer));

        let response = match pkt.packet_type {
            PacketType::Syn => conn.handle_syn(&pkt),
            PacketType::Connect => {
                let resp = conn.handle_connect(&pkt);
                if conn.phase == Phase::Connected && !pkt.payload.is_empty() {
                    match kerberos.process_ticket(&pkt.payload) {
                        Ok(keys) => {
                            conn.rc4_recv = Some(rc4::Rc4::new(&keys.rc4_key));
                            conn.rc4_send = Some(rc4::Rc4::new(&keys.rc4_key));
                            tracing::info!(%peer, "RC4 session keys established");
                        }
                        Err(e) => {
                            tracing::warn!(%peer, %e, "failed to process Kerberos ticket");
                        }
                    }
                }
                resp
            }
            PacketType::Data => {
                if conn.phase == Phase::Connected {
                    let mut decrypted = pkt.payload.clone();
                    if let Some(ref mut cipher) = conn.rc4_recv {
                        cipher.apply(&mut decrypted);
                    }
                    match rmc::RmcRequest::decode(&decrypted) {
                        Ok(req) => {
                            let resp_data = rmc::encode_error(req.call_id, req.protocol_id, 0x00010001);
                            let mut encrypted = resp_data;
                            if let Some(ref mut cipher) = conn.rc4_send {
                                cipher.apply(&mut encrypted);
                            }
                            let seq = conn.next_server_sequence();
                            Some(Packet {
                                source: pkt.dest, dest: pkt.source,
                                packet_type: PacketType::Data,
                                flags: FLAG_RELIABLE | FLAG_NEED_ACK | FLAG_HAS_SIZE,
                                session_id: conn.session_id,
                                signature: conn.server_signature,
                                sequence: seq,
                                fragment_id: 0,
                                connection_signature: 0,
                                payload: encrypted,
                            })
                        }
                        Err(e) => {
                            tracing::warn!(%peer, %e, "failed to decode RMC");
                            if pkt.flags & FLAG_NEED_ACK != 0 {
                                Some(conn.make_ack(pkt.sequence, pkt.dest, pkt.source))
                            } else {
                                None
                            }
                        }
                    }
                } else {
                    tracing::warn!(%peer, "DATA before CONNECT, ignoring");
                    None
                }
            }
            PacketType::Disconnect => {
                tracing::info!(%peer, "client disconnecting");
                connections.remove(&peer);
                None
            }
            PacketType::Ping => {
                tracing::debug!(%peer, "PING");
                Some(Packet {
                    source: pkt.dest, dest: pkt.source,
                    packet_type: PacketType::Ping, flags: transport::FLAG_ACK,
                    session_id: conn.session_id, signature: conn.server_signature,
                    sequence: pkt.sequence, fragment_id: 0,
                    connection_signature: 0, payload: vec![],
                })
            }
        };

        if let Some(resp) = response {
            let out = resp.encode(access_key_sum);
            tracing::debug!(
                %peer, typ = %resp.packet_type, seq = resp.sequence, len = out.len(),
                "PRUDP -> send"
            );
            if let Err(e) = sock.send_to(&out, peer).await {
                tracing::warn!(%peer, %e, "PRUDP send error");
            }
        }
    }
}
