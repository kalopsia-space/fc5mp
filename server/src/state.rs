use std::collections::HashMap;
use std::sync::{Arc, RwLock};
use uuid::Uuid;

#[derive(Debug, Clone)]
pub struct Player {
    pub id: Uuid,
    pub name: String,
}

#[derive(Debug, Clone)]
pub struct Session {
    pub id: Uuid,
    pub host_id: Uuid,
    pub players: Vec<Uuid>,
    pub max_players: u8,
}

#[derive(Debug, thiserror::Error)]
pub enum StateError {
    #[error("server full")]
    ServerFull,
    #[error("session not found")]
    SessionNotFound,
    #[error("session full")]
    SessionFull,
    #[error("player not found")]
    PlayerNotFound,
}

struct Inner {
    players: HashMap<Uuid, Player>,
    sessions: HashMap<Uuid, Session>,
    max_players: u8,
    stable_profile_id: Option<Uuid>,
}

#[derive(Clone)]
pub struct ServerState(Arc<RwLock<Inner>>);

impl ServerState {
    pub fn new(max_players: u8) -> Self {
        Self(Arc::new(RwLock::new(Inner {
            players: HashMap::new(),
            sessions: HashMap::new(),
            max_players,
            stable_profile_id: None,
        })))
    }

    pub fn get_or_create_profile_id(&self) -> Uuid {
        let mut inner = self.0.write().unwrap();
        if let Some(id) = inner.stable_profile_id {
            id
        } else {
            let id = Uuid::new_v4();
            inner.stable_profile_id = Some(id);
            id
        }
    }

    pub fn add_player(&self, id: Uuid, name: String) -> Result<(), StateError> {
        let mut inner = self.0.write().unwrap();
        inner.players.insert(id, Player { id, name });
        Ok(())
    }

    pub fn remove_player(&self, id: &Uuid) {
        let mut inner = self.0.write().unwrap();
        inner.players.remove(id);
        inner.sessions.retain(|_, s| {
            s.players.retain(|p| p != id);
            s.host_id != *id
        });
    }

    pub fn get_player(&self, id: &Uuid) -> Option<Player> {
        self.0.read().unwrap().players.get(id).cloned()
    }

    pub fn any_profile_id(&self) -> Option<String> {
        self.0.read().unwrap().players.keys().next().map(|id| id.to_string())
    }

    pub fn create_session(&self, host_id: Uuid) -> Result<Uuid, StateError> {
        let mut inner = self.0.write().unwrap();
        if !inner.players.contains_key(&host_id) {
            return Err(StateError::PlayerNotFound);
        }
        let id = Uuid::new_v4();
        let max = inner.max_players;
        inner.sessions.insert(id, Session {
            id,
            host_id,
            players: vec![host_id],
            max_players: max,
        });
        Ok(id)
    }

    pub fn join_session(&self, session_id: &Uuid, player_id: Uuid) -> Result<(), StateError> {
        let mut inner = self.0.write().unwrap();
        let session = inner.sessions.get_mut(session_id)
            .ok_or(StateError::SessionNotFound)?;
        if session.players.len() >= session.max_players as usize {
            return Err(StateError::SessionFull);
        }
        if !session.players.contains(&player_id) {
            session.players.push(player_id);
        }
        Ok(())
    }

    pub fn get_session(&self, id: &Uuid) -> Option<Session> {
        self.0.read().unwrap().sessions.get(id).cloned()
    }

    pub fn list_sessions(&self) -> Vec<Session> {
        self.0.read().unwrap().sessions.values().cloned().collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_and_get_player() {
        let state = ServerState::new(4);
        let id = Uuid::new_v4();
        state.add_player(id, "alice".into()).unwrap();
        let p = state.get_player(&id).unwrap();
        assert_eq!(p.name, "alice");
    }

    #[test]
    fn remove_player_cleans_sessions() {
        let state = ServerState::new(4);
        let host = Uuid::new_v4();
        state.add_player(host, "host".into()).unwrap();
        let sid = state.create_session(host).unwrap();
        assert!(state.get_session(&sid).is_some());
        state.remove_player(&host);
        assert!(state.get_session(&sid).is_none());
    }

    #[test]
    fn join_session_respects_max() {
        let state = ServerState::new(2);
        let host = Uuid::new_v4();
        let g1 = Uuid::new_v4();
        let g2 = Uuid::new_v4();
        state.add_player(host, "h".into()).unwrap();
        state.add_player(g1, "g1".into()).unwrap();
        state.add_player(g2, "g2".into()).unwrap();
        let sid = state.create_session(host).unwrap();
        state.join_session(&sid, g1).unwrap();
        assert!(state.join_session(&sid, g2).is_err());
    }

    #[test]
    fn create_session_requires_player() {
        let state = ServerState::new(4);
        let ghost = Uuid::new_v4();
        assert!(state.create_session(ghost).is_err());
    }
}
