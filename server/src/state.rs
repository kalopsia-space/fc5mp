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

#[derive(Debug, Clone)]
pub struct Entity {
    pub entity_id: Uuid,
    pub profile_id: Uuid,
    pub space_id: Option<String>,
    pub name: String,
    pub entity_type: String,
    pub obj: serde_json::Value,
    pub revision: u64,
    pub last_modified: String,
}

struct Inner {
    players: HashMap<Uuid, Player>,
    sessions: HashMap<Uuid, Session>,
    /// Maps Ubi-SessionId string → player profile UUID
    ubi_sessions: HashMap<String, Uuid>,
    /// Entity storage: entity_id → Entity
    entities: HashMap<Uuid, Entity>,
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
            ubi_sessions: HashMap::new(),
            entities: HashMap::new(),
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

    /// Register a Ubi-SessionId → profile mapping (called at login).
    pub fn register_ubi_session(&self, session_id: String, profile_id: Uuid) {
        self.0.write().unwrap().ubi_sessions.insert(session_id, profile_id);
    }

    /// Look up which player owns a Ubi-SessionId.
    pub fn resolve_ubi_session(&self, session_id: &str) -> Option<Uuid> {
        self.0.read().unwrap().ubi_sessions.get(session_id).copied()
    }

    /// Look up multiple players by their profile IDs.
    pub fn get_players(&self, ids: &[Uuid]) -> Vec<Player> {
        let inner = self.0.read().unwrap();
        ids.iter().filter_map(|id| inner.players.get(id).cloned()).collect()
    }

    /// Store or update an entity. Returns the stored entity.
    pub fn put_entity(&self, profile_id: Uuid, space_id: Option<String>, name: String, entity_type: String, obj: serde_json::Value) -> Entity {
        let mut inner = self.0.write().unwrap();
        // Check if entity with same profile+name already exists
        let existing = inner.entities.values().find(|e| e.profile_id == profile_id && e.name == name).map(|e| (e.entity_id, e.revision));
        let (entity_id, revision) = match existing {
            Some((id, rev)) => (id, rev + 1),
            None => (Uuid::new_v4(), 1),
        };
        let now = {
            use std::time::{SystemTime, UNIX_EPOCH};
            let secs = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs();
            format!("{}Z", secs)
        };
        let entity = Entity {
            entity_id,
            profile_id,
            space_id,
            name,
            entity_type,
            obj,
            revision,
            last_modified: now,
        };
        inner.entities.insert(entity_id, entity.clone());
        entity
    }

    /// Get entities for a profile, optionally filtered by type.
    pub fn get_entities(&self, profile_id: &Uuid, entity_type: Option<&str>) -> Vec<Entity> {
        let inner = self.0.read().unwrap();
        inner.entities.values()
            .filter(|e| e.profile_id == *profile_id)
            .filter(|e| entity_type.map_or(true, |t| e.entity_type == t))
            .cloned()
            .collect()
    }

    /// Get entities for a space, optionally filtered by type.
    pub fn get_space_entities(&self, space_id: &str, entity_type: Option<&str>) -> Vec<Entity> {
        let inner = self.0.read().unwrap();
        inner.entities.values()
            .filter(|e| e.space_id.as_deref() == Some(space_id))
            .filter(|e| entity_type.map_or(true, |t| e.entity_type == t))
            .cloned()
            .collect()
    }

    /// Get a single entity by ID.
    pub fn get_entity(&self, entity_id: &Uuid) -> Option<Entity> {
        self.0.read().unwrap().entities.get(entity_id).cloned()
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
