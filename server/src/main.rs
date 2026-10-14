use fc5mp_server::config::{Cli, Config};
use fc5mp_server::state::ServerState;
use clap::Parser;
use tracing_subscriber::EnvFilter;

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
    let rest_addr = std::net::SocketAddr::new(cfg.listen_ip, cfg.rest_port);
    let app = fc5mp_server::rest::router(state.clone(), &cfg, kerberos.clone());
    let listener = tokio::net::TcpListener::bind(rest_addr).await?;
    tracing::info!(%rest_addr, "REST server listening");
    tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });

    let prudp_addr = std::net::SocketAddr::new(cfg.listen_ip, cfg.prudp_port);
    tokio::spawn(async move {
        if let Err(e) = fc5mp_server::prudp::run(prudp_addr).await {
            tracing::error!(%e, "PRUDP server error");
        }
    });

    tracing::info!("server ready");
    tokio::signal::ctrl_c().await?;
    tracing::info!("shutting down");
    Ok(())
}
