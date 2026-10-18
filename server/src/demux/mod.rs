mod proto;

use prost::Message;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

use proto::demux as dmx;
use proto::ownership as own;

fn handle_service_request(service: &str, data: &[u8]) -> Option<Vec<u8>> {
    match service {
        "utility_service" => {
            tracing::debug!("utility_service: returning empty");
            None
        }
        "client_configuration_service" => {
            if let Ok(upstream) = proto::client_config::Upstream::decode(data) {
                if let Some(req) = upstream.request {
                    let request_id = req.request_id;
                    if let Some(ref patch_req) = req.get_patch_info_req {
                        tracing::info!(request_id, track = %patch_req.patch_track_id, "client_config: GetPatchInfoReq");
                        let rsp = proto::client_config::Downstream {
                            response: Some(proto::client_config::Rsp {
                                request_id,
                                get_patch_info_rsp: Some(dmx::GetPatchInfoRsp {
                                    success: true,
                                    patch_track_id: patch_req.patch_track_id.clone(),
                                    test_config: patch_req.test_config,
                                    patch_base_url: "https://localhost/patch/".to_string(),
                                    latest_version: 13389,
                                    track_type: None,
                                }),
                                ..Default::default()
                            }),
                        };
                        let mut buf = Vec::with_capacity(rsp.encoded_len());
                        rsp.encode(&mut buf).unwrap();
                        return Some(buf);
                    }
                    if let Some(ref patch_req_v2) = req.get_patch_info_req_v2 {
                        tracing::info!(request_id, track = %patch_req_v2.patch_track_id, "client_config: GetPatchInfoReqV2");
                        let rsp_v2 = proto::client_config::GetPatchInfoRsp {
                            success: true,
                            patch_track_id: patch_req_v2.patch_track_id.clone(),
                            test_config: patch_req_v2.test_config,
                            patch_base_url: "https://localhost/patch/".to_string(),
                            latest_version: 13389,
                            track_type: None,
                            mandatory_on_startup: None,
                        };
                        let rsp = proto::client_config::Downstream {
                            response: Some(proto::client_config::Rsp {
                                request_id,
                                get_patch_info_rsp_v2: Some(rsp_v2),
                                ..Default::default()
                            }),
                        };
                        let mut buf = Vec::with_capacity(rsp.encoded_len());
                        rsp.encode(&mut buf).unwrap();
                        return Some(buf);
                    }
                    if let Some(ref stats_req) = req.get_statistics_status_req {
                        tracing::info!(request_id, count = stats_req.build_versions.len(), "client_config: GetStatisticsStatusReq");
                        let rsp = proto::client_config::Downstream {
                            response: Some(proto::client_config::Rsp {
                                request_id,
                                get_statistics_status_rsp: Some(proto::client_config::GetStatisticsStatusRsp {
                                    enabled_build_versions: stats_req.build_versions.clone(),
                                }),
                                ..Default::default()
                            }),
                        };
                        let mut buf = Vec::with_capacity(rsp.encoded_len());
                        rsp.encode(&mut buf).unwrap();
                        return Some(buf);
                    }
                    tracing::debug!(request_id, "client_config: unhandled request");
                }
            }
            None
        }
        _ => {
            tracing::debug!(%service, "service request: no handler");
            None
        }
    }
}

fn hex_preview(data: &[u8]) -> String {
    data.iter().take(32).map(|b| format!("{:02x}", b)).collect::<Vec<_>>().join(" ")
}

const FC5_PRODUCT_ID: u32 = 5067;
const FC5_ADDON_PRODUCT_ID: u32 = 5068;
const FC5_UPLAY_ID: u32 = 4311;
const FC5_SPACE_ID: &str = "f40e304d-8e8d-4343-8270-d06487c35add";
const FC5_APP_ID: &str = "e0b29a98-ce38-4bc7-96ee-9d20e793e9e3";

async fn read_frame(stream: &mut TcpStream) -> anyhow::Result<Option<Vec<u8>>> {
    let mut len_buf = [0u8; 4];
    match stream.read_exact(&mut len_buf).await {
        Ok(_) => {}
        Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => return Ok(None),
        Err(e) => return Err(e.into()),
    }
    let len = u32::from_be_bytes(len_buf) as usize;
    if len > 1_048_576 {
        anyhow::bail!("demux frame too large: {len}");
    }
    let mut data = vec![0u8; len];
    stream.read_exact(&mut data).await?;
    Ok(Some(data))
}

async fn write_frame(stream: &mut TcpStream, data: &[u8]) -> anyhow::Result<()> {
    let len = data.len() as u32;
    stream.write_all(&len.to_be_bytes()).await?;
    stream.write_all(data).await?;
    stream.flush().await?;
    Ok(())
}

fn encode_downstream(downstream: &dmx::Downstream) -> Vec<u8> {
    let mut buf = Vec::with_capacity(downstream.encoded_len());
    downstream.encode(&mut buf).unwrap();
    buf
}

const FC5_PRODUCT_CONFIG: &str = concat!(
    "version: \"2.0\"\n",
    "root:\n",
    "  name: Far Cry 5\n",
    "  background_image: background.jpg\n",
    "  thumb_image: thumb.jpg\n",
    "  cloud_saves: no\n",
    "  uplay_pipe_required: yes\n",
    "  start_game:\n",
    "    steam:\n",
    "      steam_app_id: 552520\n",
    "      overlay_supported: yes\n",
    "      overlay_required: no\n",
    "      after_game_report_enabled: no\n",
    "      legacy_offline_supported: no\n",
    "      offline_supported: no\n",
    "    online:\n",
    "      overlay_supported: yes\n",
    "      overlay_required: no\n",
    "      after_game_report_enabled: no\n",
    "      allow_multiple_instances: no\n",
    "      executables:\n",
    "        - path:\n",
    "            relative: bin\\FarCry5.exe\n",
    "          working_directory:\n",
    "            register: HKEY_CURRENT_USER\\SOFTWARE\\Ubisoft\\Launcher\\Installs\\4311\\InstallDir\n",
    "          description: Far Cry 5\n",
);

fn build_owned_game(product_id: u32, product_type: u32, state: u32) -> own::OwnedGame {
    build_owned_game_with_config(product_id, product_type, state, true)
}

fn build_owned_game_with_config(product_id: u32, product_type: u32, state: u32, include_config: bool) -> own::OwnedGame {
    own::OwnedGame {
        product_id,
        owned: Some(true),
        product_type: Some(product_type),
        state: Some(state),
        configuration: if include_config { Some(FC5_PRODUCT_CONFIG.to_string()) } else { None },
        config_version: if include_config { Some(1) } else { None },
        uplay_id: Some(FC5_UPLAY_ID),
        ubiservices_space_id: Some(FC5_SPACE_ID.to_string()),
        ubiservices_app_id: Some(FC5_APP_ID.to_string()),
        platform: Some(1),
        package_ownership_state: Some(0),
        ..Default::default()
    }
}

fn strip_length_prefix(data: &[u8]) -> &[u8] {
    if data.len() >= 4 {
        let len = u32::from_be_bytes([data[0], data[1], data[2], data[3]]) as usize;
        if len + 4 == data.len() {
            return &data[4..];
        }
    }
    data
}

fn add_length_prefix(data: &[u8]) -> Vec<u8> {
    let len = data.len() as u32;
    let mut result = Vec::with_capacity(4 + data.len());
    result.extend_from_slice(&len.to_be_bytes());
    result.extend_from_slice(data);
    result
}

/// Returns (immediate_response, optional_delayed_push)
async fn handle_ownership_request(data: &[u8]) -> anyhow::Result<(Vec<u8>, Option<Vec<u8>>)> {
    let inner = strip_length_prefix(data);
    tracing::info!(data_len = data.len(), inner_len = inner.len(), first_bytes = ?&inner[..inner.len().min(16)], "ownership data");
    let upstream = own::Upstream::decode(inner)?;
    if let Some(req) = upstream.request {
        let request_id = req.request_id;
        tracing::info!(request_id, "ownership request");

        if req.initialize_req.is_some() {
            tracing::info!("ownership: InitializeReq — normal success=true with config");
            let container = own::OwnedGamesContainer {
                signature: vec![0u8; 64],
                product_ids: vec![FC5_UPLAY_ID, FC5_PRODUCT_ID, FC5_ADDON_PRODUCT_ID],
                visible_or_installable_product_ids: vec![FC5_UPLAY_ID, FC5_PRODUCT_ID, FC5_ADDON_PRODUCT_ID],
            };
            let rsp = own::Downstream {
                response: Some(own::Rsp {
                    request_id,
                    initialize_rsp: Some(own::InitializeRsp {
                        success: true,
                        owned_games_container: Some(container.encode_to_vec()),
                        owned_games: Some(own::OwnedGames {
                            owned_games: vec![
                                build_owned_game(FC5_UPLAY_ID, 0, 3),
                                build_owned_game(FC5_PRODUCT_ID, 0, 3),
                                build_owned_game(FC5_ADDON_PRODUCT_ID, 1, 3),
                            ],
                        }),
                        key_spam_ban_seconds: None,
                        subscription_state: None,
                        subscription_type: None,
                        uplay_pc_ticket: None,
                    }),
                    ..Default::default()
                }),
                ..Default::default()
            };
            let mut rsp_buf = Vec::with_capacity(rsp.encoded_len());
            rsp.encode(&mut rsp_buf)?;
            tracing::info!("InitializeRsp sent with success=true + owned games");
            return Ok((add_length_prefix(&rsp_buf), None));
        }

        if let Some(ref ticket_req) = req.get_uplay_pc_ticket_req {
            tracing::info!(uplay_id = ticket_req.uplay_id, "ownership: GetUplayPCTicketReq");
            let rsp = own::Downstream {
                response: Some(own::Rsp {
                    request_id,
                    get_uplay_pc_ticket_rsp: Some(own::GetUplayPcTicketRsp {
                        success: true,
                        uplay_pc_ticket: Some("fc5mp-uplay-ticket".to_string()),
                        ..Default::default()
                    }),
                    ..Default::default()
                }),
                ..Default::default()
            };
            let mut buf = Vec::with_capacity(rsp.encoded_len());
            rsp.encode(&mut buf)?;
            return Ok((add_length_prefix(&buf), None));
        }

        if let Some(ref config_req) = req.get_product_config_req {
            tracing::info!(product_id = config_req.product_id, "ownership: GetProductConfigReq");
            let rsp = own::Downstream {
                response: Some(own::Rsp {
                    request_id,
                    get_product_config_rsp: Some(own::GetProductConfigRsp {
                        result: 1,
                        configuration: Some(FC5_PRODUCT_CONFIG.to_string()),
                        ..Default::default()
                    }),
                    ..Default::default()
                }),
                ..Default::default()
            };
            let mut buf = Vec::with_capacity(rsp.encoded_len());
            rsp.encode(&mut buf)?;
            return Ok((add_length_prefix(&buf), None));
        }

        if let Some(ref steam_pop_req) = req.register_ownership_steam_pop_req {
            tracing::info!(confirm = steam_pop_req.confirm, "ownership: RegisterOwnershipSteamPopReq");
            // Return NoPackagesFound (4) to avoid triggering YAML reader via SteamPop path
            let rsp = own::Downstream {
                response: Some(own::Rsp {
                    request_id,
                    register_ownership_steam_pop_rsp: Some(own::RegisterOwnershipSteamPopRsp {
                        result: 4, // NoPackagesFound — tells launcher no Steam packages to register
                        configuration: vec![],
                    }),
                    ..Default::default()
                }),
                ..Default::default()
            };
            let mut rsp_buf = Vec::with_capacity(rsp.encoded_len());
            rsp.encode(&mut rsp_buf)?;
            tracing::info!("SteamPop: returning NoPackagesFound to suppress YAML trigger #2");
            return Ok((add_length_prefix(&rsp_buf), None));
        }

        if let Some(ref reg_req) = req.register_ownership_req {
            tracing::info!(product_id = reg_req.product_id, cd_key = ?reg_req.cd_key, "ownership: RegisterOwnershipReq (CD key)");
            let container = own::OwnedGamesContainer {
                signature: vec![0u8; 64],
                product_ids: vec![FC5_UPLAY_ID, FC5_PRODUCT_ID, FC5_ADDON_PRODUCT_ID],
                visible_or_installable_product_ids: vec![FC5_UPLAY_ID, FC5_PRODUCT_ID, FC5_ADDON_PRODUCT_ID],
            };
            let rsp = own::Downstream {
                response: Some(own::Rsp {
                    request_id,
                    register_ownership_rsp: Some(own::RegisterOwnershipRsp {
                        result: 1, // Success
                        owned_games_container: Some(container.encode_to_vec()),
                        owned_games: Some(own::OwnedGames {
                            owned_games: vec![
                                build_owned_game(FC5_UPLAY_ID, 0, 3),
                                build_owned_game(FC5_PRODUCT_ID, 0, 3),
                                build_owned_game(FC5_ADDON_PRODUCT_ID, 1, 3),
                            ],
                        }),
                    }),
                    ..Default::default()
                }),
                ..Default::default()
            };
            let mut buf = Vec::with_capacity(rsp.encoded_len());
            rsp.encode(&mut buf)?;
            return Ok((add_length_prefix(&buf), None));
        }

        tracing::warn!(request_id, "unhandled ownership request");
        let rsp = own::Downstream {
            response: Some(own::Rsp {
                request_id,
                ..Default::default()
            }),
            ..Default::default()
        };
        let mut buf = Vec::with_capacity(rsp.encoded_len());
        rsp.encode(&mut buf)?;
        return Ok((add_length_prefix(&buf), None));
    }

    Ok((Vec::new(), None))
}

pub async fn handle_connection(mut stream: TcpStream) {
    let peer = stream.peer_addr().ok();
    tracing::info!(?peer, "demux connection accepted");

    let mut next_conn_id: u32 = 1;
    let mut connections: std::collections::HashMap<u32, String> = std::collections::HashMap::new();

    loop {
        let frame = match read_frame(&mut stream).await {
            Ok(Some(f)) => f,
            Ok(None) => {
                tracing::info!(?peer, "demux connection closed");
                return;
            }
            Err(e) => {
                tracing::warn!(?peer, %e, "demux read error");
                return;
            }
        };

        let upstream = match dmx::Upstream::decode(frame.as_slice()) {
            Ok(u) => u,
            Err(e) => {
                tracing::warn!(%e, "failed to decode demux upstream");
                continue;
            }
        };

        if let Some(req) = upstream.request {
            let request_id = req.request_id;

            if req.authenticate_req.is_some() {
                tracing::info!(request_id, "demux: AuthenticateReq");
                let rsp = dmx::Downstream {
                    response: Some(dmx::Rsp {
                        request_id,
                        authenticate_rsp: Some(dmx::AuthenticateRsp {
                            success: true,
                            ..Default::default()
                        }),
                        ..Default::default()
                    }),
                    ..Default::default()
                };
                if let Err(e) = write_frame(&mut stream, &encode_downstream(&rsp)).await {
                    tracing::warn!(%e, "demux write error");
                    return;
                }
            } else if let Some(ref open_req) = req.open_connection_req {
                let service = &open_req.service_name;
                let conn_id = next_conn_id;
                next_conn_id += 1;
                tracing::info!(request_id, %service, conn_id, "demux: OpenConnectionReq");
                connections.insert(conn_id, service.clone());
                let rsp = dmx::Downstream {
                    response: Some(dmx::Rsp {
                        request_id,
                        open_connection_rsp: Some(dmx::OpenConnectionRsp {
                            connection_id: conn_id,
                            success: true,
                        }),
                        ..Default::default()
                    }),
                    ..Default::default()
                };
                if let Err(e) = write_frame(&mut stream, &encode_downstream(&rsp)).await {
                    tracing::warn!(%e, "demux write error");
                    return;
                }
            } else if let Some(ref patch_req) = req.get_patch_info_req {
                tracing::info!(request_id, track = %patch_req.patch_track_id, test = patch_req.test_config, "demux: GetPatchInfoReq");
                let rsp = dmx::Downstream {
                    response: Some(dmx::Rsp {
                        request_id,
                        get_patch_info_rsp: Some(dmx::GetPatchInfoRsp {
                            success: true,
                            patch_track_id: patch_req.patch_track_id.clone(),
                            test_config: patch_req.test_config,
                            patch_base_url: "https://localhost/patch/".to_string(),
                            latest_version: 13389,
                            track_type: None,
                        }),
                        ..Default::default()
                    }),
                    ..Default::default()
                };
                if let Err(e) = write_frame(&mut stream, &encode_downstream(&rsp)).await {
                    tracing::warn!(%e, "demux write error");
                    return;
                }
            } else if let Some(ref service_req) = req.service_request {
                tracing::info!(request_id, service = %service_req.service, data_hex = ?hex_preview(&service_req.data), "demux: ServiceReq");
                let response_data = handle_service_request(&service_req.service, &service_req.data);
                let rsp = dmx::Downstream {
                    response: Some(dmx::Rsp {
                        request_id,
                        service_rsp: Some(dmx::ServiceRsp {
                            success: true,
                            data: response_data,
                        }),
                        ..Default::default()
                    }),
                    ..Default::default()
                };
                if let Err(e) = write_frame(&mut stream, &encode_downstream(&rsp)).await {
                    tracing::warn!(%e, "demux write error");
                    return;
                }
            } else {
                tracing::warn!(request_id, ip_override = ?req.client_ip_override, raw = ?hex_preview(&frame), "demux: unhandled request type, sending empty response");
                let rsp = dmx::Downstream {
                    response: Some(dmx::Rsp {
                        request_id,
                        ..Default::default()
                    }),
                    ..Default::default()
                };
                if let Err(e) = write_frame(&mut stream, &encode_downstream(&rsp)).await {
                    tracing::warn!(%e, "demux write error");
                    return;
                }
            }
        }

        if let Some(push) = upstream.push {
            if let Some(ref ver) = push.client_version {
                tracing::info!(version = ver.version, "demux: ClientVersionPush");
            }
            if let Some(ref ps) = push.product_started {
                tracing::info!(product_id = ps.product_id, "demux: ProductStartedPush");
            }
            if let Some(ref pe) = push.product_ended {
                tracing::info!(product_id = pe.product_id, "demux: ProductEndedPush");
            }
            if let Some(data_msg) = push.data {
                let conn_id = data_msg.connection_id;
                let service = connections.get(&conn_id).cloned().unwrap_or_default();
                tracing::debug!(conn_id, %service, data_len = data_msg.data.len(), "demux: DataMessage");

                let response_data = match service.as_str() {
                    "ownership_service" => {
                        match handle_ownership_request(&data_msg.data).await {
                            Ok((immediate, delayed_push)) => {
                                if let Some(delayed) = delayed_push {
                                    // Write immediate response first
                                    if !immediate.is_empty() {
                                        let push_rsp = dmx::Downstream {
                                            response: None,
                                            push: Some(dmx::Push {
                                                data: Some(dmx::DataMessage {
                                                    connection_id: conn_id,
                                                    data: immediate,
                                                }),
                                                ..Default::default()
                                            }),
                                        };
                                        if let Err(e) = write_frame(&mut stream, &encode_downstream(&push_rsp)).await {
                                            tracing::warn!(%e, "demux write error");
                                            return;
                                        }
                                    }
                                    // Sleep 2s then send the delayed message
                                    tracing::info!("sleeping 2s before delayed OwnedGamePush");
                                    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                                    let push_msg = dmx::Downstream {
                                        response: None,
                                        push: Some(dmx::Push {
                                            data: Some(dmx::DataMessage {
                                                connection_id: conn_id,
                                                data: delayed,
                                            }),
                                            ..Default::default()
                                        }),
                                    };
                                    if let Err(e) = write_frame(&mut stream, &encode_downstream(&push_msg)).await {
                                        tracing::warn!(%e, "demux write error (delayed push)");
                                        return;
                                    }
                                    tracing::info!("delayed OwnedGamePush sent");
                                    // Skip the normal write below
                                    Vec::new()
                                } else {
                                    immediate
                                }
                            }
                            Err(e) => {
                                tracing::warn!(%e, data_hex = ?hex_preview(&data_msg.data), "ownership handler error");
                                Vec::new()
                            }
                        }
                    }
                    "denuvo_service" => {
                        tracing::info!(%service, data_len = data_msg.data.len(), hex = ?hex_preview(&data_msg.data), "demux: denuvo data (stub response)");
                        Vec::new()
                    }
                    "friends_service" | "utility_service" | "playtime_service" |
                    "client_configuration_service" | "steam_service" | "store_service" => {
                        tracing::debug!(%service, data_len = data_msg.data.len(), "demux: ignoring service data");
                        Vec::new()
                    }
                    _ => {
                        tracing::warn!(%service, "demux: data for unknown service");
                        Vec::new()
                    }
                };

                if !response_data.is_empty() {
                    let push_rsp = dmx::Downstream {
                        response: None,
                        push: Some(dmx::Push {
                            data: Some(dmx::DataMessage {
                                connection_id: conn_id,
                                data: response_data,
                            }),
                            ..Default::default()
                        }),
                    };
                    if let Err(e) = write_frame(&mut stream, &encode_downstream(&push_rsp)).await {
                        tracing::warn!(%e, "demux write error");
                        return;
                    }
                }
            }

            if push.keep_alive.is_some() {
                let ka = dmx::Downstream {
                    response: None,
                    push: Some(dmx::Push {
                        keep_alive: Some(dmx::KeepAlivePush {}),
                        ..Default::default()
                    }),
                };
                if let Err(e) = write_frame(&mut stream, &encode_downstream(&ka)).await {
                    tracing::warn!(%e, "demux write error");
                    return;
                }
            }
        }
    }
}
