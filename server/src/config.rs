use clap::Parser;
use serde::Deserialize;
use std::net::IpAddr;
use std::path::Path;

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct Config {
    pub listen_ip: IpAddr,
    pub rest_port: u16,
    pub prudp_port: u16,
    pub max_players: u8,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            listen_ip: "0.0.0.0".parse().unwrap(),
            rest_port: 21000,
            prudp_port: 6570,
            max_players: 4,
        }
    }
}

#[derive(Parser, Debug)]
#[command(name = "fc5mp-server")]
pub struct Cli {
    #[arg(long)]
    pub config: Option<std::path::PathBuf>,
    #[arg(long)]
    pub rest_port: Option<u16>,
    #[arg(long)]
    pub prudp_port: Option<u16>,
    #[arg(long)]
    pub listen_ip: Option<IpAddr>,
}

impl Config {
    pub fn load(path: Option<&Path>) -> anyhow::Result<Self> {
        let mut cfg = if let Some(p) = path {
            let text = std::fs::read_to_string(p)?;
            toml::from_str(&text)?
        } else {
            Self::default()
        };
        if let Ok(cli) = Cli::try_parse() {
            if let Some(v) = cli.rest_port { cfg.rest_port = v; }
            if let Some(v) = cli.prudp_port { cfg.prudp_port = v; }
            if let Some(v) = cli.listen_ip { cfg.listen_ip = v; }
        }
        Ok(cfg)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults() {
        let cfg = Config::default();
        assert_eq!(cfg.rest_port, 21000);
        assert_eq!(cfg.prudp_port, 6570);
        assert_eq!(cfg.max_players, 4);
        assert_eq!(cfg.listen_ip, "0.0.0.0".parse::<IpAddr>().unwrap());
    }

    #[test]
    fn from_toml() {
        let text = r#"
            listen_ip = "127.0.0.1"
            rest_port = 8080
            prudp_port = 7000
            max_players = 2
        "#;
        let cfg: Config = toml::from_str(text).unwrap();
        assert_eq!(cfg.rest_port, 8080);
        assert_eq!(cfg.prudp_port, 7000);
        assert_eq!(cfg.max_players, 2);
        assert_eq!(cfg.listen_ip, "127.0.0.1".parse::<IpAddr>().unwrap());
    }

    #[test]
    fn partial_toml_uses_defaults() {
        let text = r#"rest_port = 9999"#;
        let cfg: Config = toml::from_str(text).unwrap();
        assert_eq!(cfg.rest_port, 9999);
        assert_eq!(cfg.prudp_port, 6570);
    }
}
