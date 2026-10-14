use reqwest::Client;
use serde_json::Value;
use tokio::net::TcpListener;

#[tokio::test]
async fn login_returns_session_ticket() {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();

    let state = fc5mp_server::state::ServerState::new(4);
    let config = fc5mp_server::config::Config::default();
    let kerberos = fc5mp_server::prudp::kerberos::KerberosServer::new();
    let app = fc5mp_server::rest::router(state, &config, kerberos);
    tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });

    let resp = Client::new()
        .post(format!("http://{addr}/v3/profiles/sessions"))
        .send().await.unwrap();
    assert_eq!(resp.status(), 200);

    let body: Value = resp.json().await.unwrap();
    assert_eq!(body["platformType"], "uplay");
    assert!(body["profileId"].as_str().unwrap().len() > 0);
    assert!(body["ticket"].as_str().unwrap().len() > 0);
    assert!(body["sessionKey"].as_str().unwrap().len() > 0);
    assert_eq!(body["spaceId"], "f40e304d-8e8d-4343-8270-d06487c35add");
}
