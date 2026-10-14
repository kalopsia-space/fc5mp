pub mod connection;
pub mod kerberos;
pub mod rc4;
pub mod rmc;
pub mod transport;

use std::collections::HashMap;
use std::net::SocketAddr;
use tokio::net::UdpSocket;
use connection::{Connection, Phase};
use transport::{Packet, PacketType, FLAG_NEED_ACK, FLAG_RELIABLE, FLAG_HAS_SIZE};

pub async fn run(addr: SocketAddr) -> anyhow::Result<()> {
    let sock = UdpSocket::bind(addr).await?;
    tracing::info!(%addr, "PRUDP server listening");
    let mut buf = [0u8; 2048];
    let mut connections: HashMap<SocketAddr, Connection> = HashMap::new();
    let checksum_key: u32 = 0;

    loop {
        let (len, peer) = sock.recv_from(&mut buf).await?;
        let data = &buf[..len];

        let pkt = match Packet::decode(data, checksum_key) {
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

        let conn = connections.entry(peer).or_insert_with(|| Connection::new(peer));

        let response = match pkt.packet_type {
            PacketType::Syn => conn.handle_syn(&pkt),
            PacketType::Connect => conn.handle_connect(&pkt),
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
                    sequence: pkt.sequence, payload: vec![],
                })
            }
        };

        if let Some(resp) = response {
            let out = resp.encode(checksum_key);
            tracing::debug!(
                %peer, typ = %resp.packet_type, seq = resp.sequence, len = out.len(),
                "PRUDP -> send"
            );
            sock.send_to(&out, peer).await?;
        }
    }
}
