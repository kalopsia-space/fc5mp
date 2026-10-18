use fc5mp_server::config::{Cli, Config};
use fc5mp_server::state::ServerState;
use clap::Parser;
use tracing_subscriber::EnvFilter;
use tokio::net::TcpListener;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::from_default_env()
                .add_directive("fc5mp_server=info".parse()?),
        )
        .init();

    let cli = Cli::parse();
    let cfg = Config::load(cli.config.as_deref())?;

    tracing::info!(
        rest = %format!("{}:{}", cfg.listen_ip, cfg.rest_port),
        prudp = %format!("{}:{}", cfg.listen_ip, cfg.prudp_port),
        max_players = cfg.max_players,
        "fc5mp-server starting"
    );

    let state = ServerState::new(cfg.max_players);
    let kerberos = fc5mp_server::prudp::kerberos::KerberosServer::new();

    // Protocol-detecting listener: socat terminates TLS on rest_port,
    // forwards to rest_port+1. We peek first byte to detect HTTP vs demux.
    let backend_port = cfg.rest_port + 1;
    let backend_addr = std::net::SocketAddr::new(cfg.listen_ip, backend_port);
    let app = fc5mp_server::rest::router(state.clone(), &cfg, kerberos.clone());
    let listener = TcpListener::bind(backend_addr).await?;
    tracing::info!(%backend_addr, tls_port = cfg.rest_port, "REST+demux backend");

    // Internal HTTP port for axum (localhost only)
    let http_port = cfg.rest_port + 2;
    let http_addr = std::net::SocketAddr::new("127.0.0.1".parse().unwrap(), http_port);
    let http_listener = TcpListener::bind(http_addr).await?;
    tokio::spawn(async move {
        axum::serve(http_listener, app).await.unwrap();
    });

    // Accept loop with protocol detection
    tokio::spawn(async move {
        loop {
            let (stream, _addr) = match listener.accept().await {
                Ok(v) => v,
                Err(e) => {
                    tracing::warn!(%e, "accept error");
                    continue;
                }
            };

            let mut peek_buf = [0u8; 1];
            match stream.peek(&mut peek_buf).await {
                Ok(0) => continue,
                Err(e) => {
                    tracing::warn!(%e, "peek error");
                    continue;
                }
                Ok(_) => {}
            }

            if peek_buf[0] >= b'A' && peek_buf[0] <= b'z' {
                // HTTP: proxy to internal axum port
                let http_port = http_port;
                tokio::spawn(async move {
                    let upstream = match tokio::net::TcpStream::connect(
                        format!("127.0.0.1:{}", http_port)
                    ).await {
                        Ok(u) => u,
                        Err(e) => {
                            tracing::warn!(%e, "failed to connect to internal HTTP");
                            return;
                        }
                    };
                    let (mut cr, mut cw) = stream.into_split();
                    let (mut ur, mut uw) = upstream.into_split();
                    let c2u = tokio::io::copy(&mut cr, &mut uw);
                    let u2c = tokio::io::copy(&mut ur, &mut cw);
                    let _ = tokio::try_join!(c2u, u2c);
                });
            } else {
                // Binary → demux protocol
                tokio::spawn(fc5mp_server::demux::handle_connection(stream));
            }
        }
    });

    let prudp_addr = std::net::SocketAddr::new(cfg.listen_ip, cfg.prudp_port);
    let kerberos_prudp = kerberos.clone();
    let state_prudp = state.clone();
    tokio::spawn(async move {
        if let Err(e) = fc5mp_server::prudp::run(prudp_addr, kerberos_prudp, state_prudp).await {
            tracing::error!(%e, "PRUDP server error");
        }
    });

    tracing::info!("server ready");
    tokio::signal::ctrl_c().await?;
    tracing::info!("shutting down");
    Ok(())
}
