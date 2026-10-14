use reqwest::Client;
use serde_json::Value;
use tokio::net::TcpListener;

#[tokio::test]
async fn rendezvous_config_returns_our_prudp_address() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    let state = fc5mp_server::state::ServerState::new(4);
    let config = fc5mp_server::config::Config {
        listen_ip: "10.0.0.5".parse().unwrap(),
        prudp_port: 6570,
        ..Default::default()
    };
    let app = fc5mp_server::rest::router(state, &config);
    tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });

    let resp = Client::new()
        .get(format!(
            "http://{addr}/v1/spaces/f40e304d/sandboxes/FCZ_PC_A/rendezvous/configuration"
        ))
        .send().await.unwrap();
    let body: Value = resp.json().await.unwrap();
    assert_eq!(
        body["prudp_loadbalancer_url"],
        "prudp:/address=10.0.0.5;port=6570"
    );
    assert!(body["public_key"].as_str().unwrap().len() > 0);
}

#[tokio::test]
async fn storm_router_returns_address() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    let state = fc5mp_server::state::ServerState::new(4);
    let config = fc5mp_server::config::Config::default();
    let app = fc5mp_server::rest::router(state, &config);
    tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });

    let resp = Client::new()
        .get(format!("http://{addr}/v1/router/19"))
        .send().await.unwrap();
    let body: Value = resp.json().await.unwrap();
    assert!(body["router"].as_str().unwrap().contains(":12000"));
}
