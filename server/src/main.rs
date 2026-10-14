use fc5mp_server::config::{Cli, Config};
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

    tracing::info!("server ready");
    tokio::signal::ctrl_c().await?;
    tracing::info!("shutting down");
    Ok(())
}
