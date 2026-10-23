use fc5mp_server::config::{Cli, Config};
use fc5mp_server::state::ServerState;
use clap::Parser;
use tracing_subscriber::EnvFilter;
use tokio::net::TcpListener;
use axum_server::tls_openssl::OpenSSLConfig;
use openssl::ssl::{SslAcceptor, SslFiletype, SslMethod, SslVersion};

/// Build the TLS acceptor for the game's HTTPS. The game uses a 2017-era static
/// OpenSSL (≈1.0.2) that tops out at TLS 1.2 and offers legacy ciphers, so we
/// match socat's old permissiveness: allow down to TLS 1.0, cap at 1.2, and drop
/// the OpenSSL-3 security level so old ciphers negotiate. Default acceptors are
/// TLS 1.3-only and refuse the game outright (tlsv1 alert protocol version).
fn build_tls_config() -> anyhow::Result<OpenSSLConfig> {
    let mut b = SslAcceptor::mozilla_intermediate(SslMethod::tls())?;
    b.set_min_proto_version(Some(SslVersion::TLS1))?;
    b.set_max_proto_version(Some(SslVersion::TLS1_2))?;
    b.set_cipher_list("ALL:@SECLEVEL=0")?;
    b.set_certificate_chain_file("certs/cert.pem")?;
    b.set_private_key_file("certs/key.pem", SslFiletype::PEM)?;
    b.check_private_key()?;
    Ok(OpenSSLConfig::try_from(b)?)
}

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

    // Native TLS on rest_port, terminated in-process — REPLACES the socat relay,
    // which intermittently dropped the game's HTTPS responses ("socat E I/O error"
    // after config; a run where login got 0 bytes back). The game's HTTPS to
    // :rest_port is now served directly by the REST app over OpenSSL TLS.
    let tls_addr = std::net::SocketAddr::new(cfg.listen_ip, cfg.rest_port);
    let tls_app = fc5mp_server::rest::router(state.clone(), &cfg, kerberos.clone());
    match build_tls_config() {
        Ok(tls_cfg) => {
            tokio::spawn(async move {
                tracing::info!(%tls_addr, "REST TLS (native openssl) listening");
                if let Err(e) = axum_server::bind_openssl(tls_addr, tls_cfg)
                    .serve(tls_app.into_make_service())
                    .await
                {
                    tracing::error!(%e, "TLS server error");
                }
            });
        }
        Err(e) => tracing::error!(%e,
            "failed to load TLS cert (certs/cert.pem + certs/key.pem); TLS port disabled"),
    }

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
