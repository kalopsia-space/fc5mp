use std::net::SocketAddr;
use tokio::net::UdpSocket;

#[tokio::test]
async fn syn_gets_syn_ack() {
    let server_addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
    let sock = UdpSocket::bind(server_addr).await.unwrap();
    let actual_addr = sock.local_addr().unwrap();
    drop(sock);

    let kerberos = fc5mp_server::prudp::kerberos::KerberosServer::new();
    tokio::spawn(async move {
        let state = fc5mp_server::state::ServerState::new(4);
        fc5mp_server::prudp::run(actual_addr, kerberos, state).await.unwrap();
    });
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
    let syn = fc5mp_server::prudp::transport::Packet {
        source: 0x3f, dest: 0x31,
        packet_type: fc5mp_server::prudp::transport::PacketType::Syn,
        flags: fc5mp_server::prudp::transport::FLAG_NEED_ACK,
        session_id: 0, signature: 0, sequence: 0, fragment_id: 0,
        connection_signature: 0, payload: vec![],
    };
    client.send_to(&syn.encode(0), actual_addr).await.unwrap();

    let mut buf = [0u8; 2048];
    let (len, _) = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        client.recv_from(&mut buf),
    ).await.unwrap().unwrap();

    let resp = fc5mp_server::prudp::transport::Packet::decode(&buf[..len], 0).unwrap();
    assert_eq!(resp.packet_type, fc5mp_server::prudp::transport::PacketType::Syn);
    assert!(resp.flags & fc5mp_server::prudp::transport::FLAG_ACK != 0);
}

#[tokio::test]
async fn reconnect_after_stale_connection() {
    let server_addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
    let sock = UdpSocket::bind(server_addr).await.unwrap();
    let actual_addr = sock.local_addr().unwrap();
    drop(sock);

    let kerberos = fc5mp_server::prudp::kerberos::KerberosServer::new();
    tokio::spawn(async move {
        let state = fc5mp_server::state::ServerState::new(4);
        fc5mp_server::prudp::run(actual_addr, kerberos, state).await.unwrap();
    });
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    let client = UdpSocket::bind("127.0.0.1:0").await.unwrap();
    let syn = fc5mp_server::prudp::transport::Packet {
        source: 0x3f, dest: 0x31,
        packet_type: fc5mp_server::prudp::transport::PacketType::Syn,
        flags: fc5mp_server::prudp::transport::FLAG_NEED_ACK,
        session_id: 0, signature: 0, sequence: 0, fragment_id: 0,
        connection_signature: 0, payload: vec![],
    };

    // First SYN — gets SYN-ACK
    client.send_to(&syn.encode(0), actual_addr).await.unwrap();
    let mut buf = [0u8; 2048];
    let (len, _) = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        client.recv_from(&mut buf),
    ).await.unwrap().unwrap();
    let resp = fc5mp_server::prudp::transport::Packet::decode(&buf[..len], 0).unwrap();
    assert_eq!(resp.packet_type, fc5mp_server::prudp::transport::PacketType::Syn);

    // Second SYN from same client (simulating crash + reconnect) — should also get SYN-ACK
    client.send_to(&syn.encode(0), actual_addr).await.unwrap();
    let (len, _) = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        client.recv_from(&mut buf),
    ).await.unwrap().unwrap();
    let resp = fc5mp_server::prudp::transport::Packet::decode(&buf[..len], 0).unwrap();
    assert_eq!(resp.packet_type, fc5mp_server::prudp::transport::PacketType::Syn);
    assert!(resp.flags & fc5mp_server::prudp::transport::FLAG_ACK != 0);
}
