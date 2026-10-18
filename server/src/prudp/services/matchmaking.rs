use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Mutex;

use super::{
    ddl_read_string, ddl_read_u32, ddl_write_list_empty, ddl_write_string, hex_preview,
    RmcResult, RmcService,
};

struct GatheringData {
    id: u32,
    owner_pid: u32,
    class_name: String,
    raw_inner: Vec<u8>,
    participants: Vec<u32>,
    urls: HashMap<u32, String>,
}

impl GatheringData {
    fn serialize_anydata(&self) -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(&ddl_write_string(&self.class_name));
        let mut content = Vec::new();
        content.extend_from_slice(&ddl_write_string(&self.class_name));
        if self.raw_inner.len() >= 8 {
            content.extend_from_slice(&self.id.to_le_bytes());
            content.extend_from_slice(&self.owner_pid.to_le_bytes());
            content.extend_from_slice(&self.raw_inner[8..]);
        } else {
            content.extend_from_slice(&self.raw_inner);
        }
        out.extend_from_slice(&(content.len() as u32).to_le_bytes());
        out.extend_from_slice(&content);
        out
    }
}

struct State {
    gatherings: HashMap<u32, GatheringData>,
    next_id: u32,
}

pub struct MatchMakingService {
    state: Mutex<State>,
}

impl MatchMakingService {
    pub fn new() -> Self {
        Self {
            state: Mutex::new(State {
                gatherings: HashMap::new(),
                next_id: 39000,
            }),
        }
    }

    fn parse_anydata<'a>(&self, payload: &'a [u8]) -> Option<(String, &'a [u8])> {
        let (class_name, off) = ddl_read_string(payload, 0)?;
        let (content_len, off) = ddl_read_u32(payload, off)?;
        let end = off + content_len as usize;
        if end > payload.len() {
            return None;
        }
        Some((class_name, &payload[off..end]))
    }
}

impl RmcService for MatchMakingService {
    fn protocol_id(&self) -> u8 {
        0x15
    }

    fn handle(&self, method_id: u32, payload: &[u8], peer: SocketAddr) -> RmcResult {
        tracing::info!(
            method_id,
            payload_len = payload.len(),
            %peer,
            payload_hex = %hex_preview(payload, 128),
            "MatchMaking request"
        );
        match method_id {
            1 => self.register_gathering(payload),
            2 => self.unregister_gathering(payload),
            4 => self.update_gathering(payload),
            11 => self.participate(payload),
            13 => self.get_participants(payload),
            21 => self.find_by_single_id(payload),
            26 => self.launch_session(payload),
            27 => self.update_session_url(payload),
            29 => self.get_state(payload),
            30 => self.set_state(payload),
            38 => self.register_local_url(payload),
            39 => self.register_local_urls(payload),
            40 => self.update_session_host(payload),
            41 => self.get_session_urls(payload),
            _ => {
                tracing::warn!(method_id, "unimplemented MatchMaking method");
                RmcResult::Error(0x00010002)
            }
        }
    }
}

impl MatchMakingService {
    fn register_gathering(&self, payload: &[u8]) -> RmcResult {
        let (class_name, inner) = match self.parse_anydata(payload) {
            Some(v) => v,
            None => {
                tracing::warn!("RegisterGathering: failed to parse AnyData");
                return RmcResult::Error(0x00010001);
            }
        };

        let (_inner_name, off) = match ddl_read_string(inner, 0) {
            Some(v) => v,
            None => return RmcResult::Error(0x00010001),
        };
        let raw_inner = inner[off..].to_vec();

        let owner_pid = if raw_inner.len() >= 8 {
            u32::from_le_bytes([raw_inner[4], raw_inner[5], raw_inner[6], raw_inner[7]])
        } else {
            100
        };

        let mut state = self.state.lock().unwrap();
        state.next_id += 1;
        let id = state.next_id;

        tracing::info!(
            gathering_id = id,
            owner_pid,
            class = %class_name,
            raw_len = raw_inner.len(),
            "RegisterGathering -> created"
        );

        state.gatherings.insert(
            id,
            GatheringData {
                id,
                owner_pid,
                class_name,
                raw_inner,
                participants: vec![owner_pid],
                urls: HashMap::new(),
            },
        );

        let mut resp = Vec::new();
        resp.extend_from_slice(&id.to_le_bytes());
        RmcResult::Response(resp)
    }

    fn unregister_gathering(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let mut state = self.state.lock().unwrap();
        let removed = state.gatherings.remove(&id).is_some();
        tracing::info!(gathering_id = id, removed, "UnregisterGathering");
        RmcResult::Response(vec![removed as u8])
    }

    fn update_gathering(&self, payload: &[u8]) -> RmcResult {
        let (class_name, inner) = match self.parse_anydata(payload) {
            Some(v) => v,
            None => {
                tracing::warn!("UpdateGathering: failed to parse AnyData");
                return RmcResult::Error(0x00010001);
            }
        };

        let (_, off) = match ddl_read_string(inner, 0) {
            Some(v) => v,
            None => return RmcResult::Error(0x00010001),
        };
        let raw_inner = inner[off..].to_vec();

        let id = if raw_inner.len() >= 4 {
            u32::from_le_bytes([raw_inner[0], raw_inner[1], raw_inner[2], raw_inner[3]])
        } else {
            0
        };

        let mut state = self.state.lock().unwrap();
        if let Some(gathering) = state.gatherings.get_mut(&id) {
            gathering.class_name = class_name;
            gathering.raw_inner = raw_inner;
            tracing::info!(gathering_id = id, "UpdateGathering -> updated");
            RmcResult::Response(vec![1u8])
        } else {
            tracing::warn!(gathering_id = id, "UpdateGathering -> not found");
            RmcResult::Response(vec![0u8])
        }
    }

    fn participate(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let mut state = self.state.lock().unwrap();

        if let Some(gathering) = state.gatherings.get_mut(&id) {
            let pid = 101u32;
            if !gathering.participants.contains(&pid) {
                gathering.participants.push(pid);
            }
            tracing::info!(
                gathering_id = id,
                pid,
                count = gathering.participants.len(),
                "Participate -> joined"
            );
            RmcResult::Response(vec![1u8])
        } else {
            tracing::warn!(gathering_id = id, "Participate -> not found");
            RmcResult::Response(vec![0u8])
        }
    }

    fn get_participants(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let state = self.state.lock().unwrap();
        let pids = state
            .gatherings
            .get(&id)
            .map(|g| &g.participants[..])
            .unwrap_or(&[]);

        tracing::info!(gathering_id = id, count = pids.len(), "GetParticipants");

        let mut resp = Vec::new();
        resp.extend_from_slice(&(pids.len() as u32).to_le_bytes());
        for pid in pids {
            resp.extend_from_slice(&pid.to_le_bytes());
        }
        RmcResult::Response(resp)
    }

    fn find_by_single_id(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let state = self.state.lock().unwrap();

        if let Some(gathering) = state.gatherings.get(&id) {
            tracing::info!(gathering_id = id, "FindBySingleID -> found");
            let mut resp = Vec::new();
            resp.push(1u8);
            resp.extend_from_slice(&gathering.serialize_anydata());
            RmcResult::Response(resp)
        } else {
            tracing::info!(gathering_id = id, "FindBySingleID -> not found");
            let mut resp = Vec::new();
            resp.push(0u8);
            resp.extend_from_slice(&ddl_write_string(""));
            resp.extend_from_slice(&0u32.to_le_bytes());
            RmcResult::Response(resp)
        }
    }

    fn launch_session(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        tracing::info!(gathering_id = id, "LaunchSession");
        RmcResult::Response(vec![1u8])
    }

    fn update_session_url(&self, payload: &[u8]) -> RmcResult {
        let (id, off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let (url, _) = ddl_read_string(payload, off).unwrap_or((String::new(), off));
        tracing::info!(gathering_id = id, url = %url, "UpdateSessionURL");
        RmcResult::Response(vec![])
    }

    fn get_state(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        tracing::info!(gathering_id = id, "GetState -> 0");
        RmcResult::Response(0u32.to_le_bytes().to_vec())
    }

    fn set_state(&self, payload: &[u8]) -> RmcResult {
        let (id, off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let (new_state, _) = ddl_read_u32(payload, off).unwrap_or((0, 0));
        tracing::info!(gathering_id = id, new_state, "SetState");
        RmcResult::Response(vec![])
    }

    fn register_local_url(&self, payload: &[u8]) -> RmcResult {
        let (id, off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let (url, _) = ddl_read_string(payload, off).unwrap_or((String::new(), off));
        tracing::info!(gathering_id = id, url = %url, "RegisterLocalURL");

        let mut state = self.state.lock().unwrap();
        if let Some(gathering) = state.gatherings.get_mut(&id) {
            gathering.urls.insert(gathering.owner_pid, url);
        }
        RmcResult::Response(vec![])
    }

    fn register_local_urls(&self, payload: &[u8]) -> RmcResult {
        let (id, off) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let (count, mut off) = ddl_read_u32(payload, off).unwrap_or((0, off));
        tracing::info!(gathering_id = id, count, "RegisterLocalURLs");

        let mut state = self.state.lock().unwrap();
        for i in 0..count {
            let (url, new_off) = ddl_read_string(payload, off).unwrap_or((String::new(), off));
            off = new_off;
            tracing::info!(gathering_id = id, idx = i, url = %url, "  URL");
            if let Some(gathering) = state.gatherings.get_mut(&id) {
                gathering.urls.insert(gathering.owner_pid + i, url);
            }
        }
        RmcResult::Response(vec![])
    }

    fn update_session_host(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        tracing::info!(gathering_id = id, "UpdateSessionHost");
        RmcResult::Response(vec![])
    }

    fn get_session_urls(&self, payload: &[u8]) -> RmcResult {
        let (id, _) = ddl_read_u32(payload, 0).unwrap_or((0, 0));
        let state = self.state.lock().unwrap();

        if let Some(gathering) = state.gatherings.get(&id) {
            let urls: Vec<&String> = gathering.urls.values().collect();
            tracing::info!(gathering_id = id, count = urls.len(), "GetSessionURLs");

            let mut resp = Vec::new();
            resp.extend_from_slice(&(urls.len() as u32).to_le_bytes());
            for url in &urls {
                resp.extend_from_slice(&id.to_le_bytes());
                let mut url_list = Vec::new();
                url_list.extend_from_slice(&1u32.to_le_bytes());
                url_list.extend_from_slice(&ddl_write_string(url));
                resp.extend_from_slice(&url_list);
            }
            RmcResult::Response(resp)
        } else {
            tracing::info!(gathering_id = id, "GetSessionURLs -> not found");
            RmcResult::Response(ddl_write_list_empty())
        }
    }
}
