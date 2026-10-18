pub mod demux {
    use prost::Message;

    #[derive(Clone, PartialEq, Message)]
    pub struct Token {
        #[prost(string, optional, tag = "1")]
        pub orbit_token: Option<String>,
        #[prost(bytes = "vec", optional, tag = "2")]
        pub ubi_token: Option<Vec<u8>>,
        #[prost(string, optional, tag = "3")]
        pub ubi_ticket: Option<String>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct AuthenticateReq {
        #[prost(message, required, tag = "1")]
        pub token: Token,
        #[prost(string, optional, tag = "2")]
        pub client_id: Option<String>,
        #[prost(string, optional, tag = "3")]
        pub logout_push_group_id: Option<String>,
        #[prost(bool, optional, tag = "4")]
        pub send_keep_alive: Option<bool>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct AuthenticateRsp {
        #[prost(bool, required, tag = "1")]
        pub success: bool,
        #[prost(bool, optional, tag = "2")]
        pub expired: Option<bool>,
        #[prost(bool, optional, tag = "3")]
        pub banned: Option<bool>,
        #[prost(bool, optional, tag = "4")]
        pub mfa_enforced: Option<bool>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct OpenConnectionReq {
        #[prost(string, required, tag = "2")]
        pub service_name: String,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct OpenConnectionRsp {
        #[prost(uint32, required, tag = "1")]
        pub connection_id: u32,
        #[prost(bool, required, tag = "2")]
        pub success: bool,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct KeepAlivePush {}

    #[derive(Clone, PartialEq, Message)]
    pub struct DataMessage {
        #[prost(uint32, required, tag = "1")]
        pub connection_id: u32,
        #[prost(bytes = "vec", required, tag = "2")]
        pub data: Vec<u8>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct ClientVersionPush {
        #[prost(uint32, required, tag = "1")]
        pub version: u32,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct ClientOutdatedPush {}

    #[derive(Clone, PartialEq, Message)]
    pub struct ProductStartedPush {
        #[prost(uint32, required, tag = "1")]
        pub product_id: u32,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct ProductEndedPush {
        #[prost(uint32, required, tag = "1")]
        pub product_id: u32,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct ConnectionClosedPush {
        #[prost(uint32, required, tag = "1")]
        pub connection_id: u32,
        #[prost(int32, optional, tag = "2")]
        pub error_code: Option<i32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct ServiceReq {
        #[prost(string, required, tag = "1")]
        pub service: String,
        #[prost(bytes = "vec", required, tag = "2")]
        pub data: Vec<u8>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct ServiceRsp {
        #[prost(bool, required, tag = "1")]
        pub success: bool,
        #[prost(bytes = "vec", optional, tag = "2")]
        pub data: Option<Vec<u8>>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Req {
        #[prost(uint32, required, tag = "1")]
        pub request_id: u32,
        #[prost(message, optional, tag = "2")]
        pub authenticate_req: Option<AuthenticateReq>,
        #[prost(message, optional, tag = "3")]
        pub open_connection_req: Option<OpenConnectionReq>,
        #[prost(message, optional, tag = "5")]
        pub get_patch_info_req: Option<GetPatchInfoReq>,
        #[prost(message, optional, tag = "6")]
        pub service_request: Option<ServiceReq>,
        #[prost(uint32, optional, tag = "99")]
        pub client_ip_override: Option<u32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetPatchInfoReq {
        #[prost(string, required, tag = "1")]
        pub patch_track_id: String,
        #[prost(bool, required, tag = "2")]
        pub test_config: bool,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetPatchInfoRsp {
        #[prost(bool, required, tag = "1")]
        pub success: bool,
        #[prost(string, required, tag = "2")]
        pub patch_track_id: String,
        #[prost(bool, required, tag = "3")]
        pub test_config: bool,
        #[prost(string, required, tag = "4")]
        pub patch_base_url: String,
        #[prost(uint32, required, tag = "5")]
        pub latest_version: u32,
        #[prost(uint32, optional, tag = "6")]
        pub track_type: Option<u32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Rsp {
        #[prost(uint32, required, tag = "1")]
        pub request_id: u32,
        #[prost(message, optional, tag = "2")]
        pub authenticate_rsp: Option<AuthenticateRsp>,
        #[prost(message, optional, tag = "3")]
        pub open_connection_rsp: Option<OpenConnectionRsp>,
        #[prost(message, optional, tag = "5")]
        pub get_patch_info_rsp: Option<GetPatchInfoRsp>,
        #[prost(message, optional, tag = "6")]
        pub service_rsp: Option<ServiceRsp>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Push {
        #[prost(message, optional, tag = "1")]
        pub data: Option<DataMessage>,
        #[prost(message, optional, tag = "2")]
        pub connection_closed: Option<ConnectionClosedPush>,
        #[prost(message, optional, tag = "3")]
        pub keep_alive: Option<KeepAlivePush>,
        #[prost(message, optional, tag = "4")]
        pub client_version: Option<ClientVersionPush>,
        #[prost(message, optional, tag = "5")]
        pub client_outdated: Option<ClientOutdatedPush>,
        #[prost(message, optional, tag = "6")]
        pub product_started: Option<ProductStartedPush>,
        #[prost(message, optional, tag = "7")]
        pub product_ended: Option<ProductEndedPush>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Upstream {
        #[prost(message, optional, tag = "1")]
        pub request: Option<Req>,
        #[prost(message, optional, tag = "2")]
        pub push: Option<Push>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Downstream {
        #[prost(message, optional, tag = "1")]
        pub response: Option<Rsp>,
        #[prost(message, optional, tag = "2")]
        pub push: Option<Push>,
    }
}

pub mod client_config {
    use prost::Message;

    #[derive(Clone, PartialEq, Message)]
    pub struct GetPatchInfoReq {
        #[prost(string, required, tag = "1")]
        pub patch_track_id: String,
        #[prost(bool, required, tag = "2")]
        pub test_config: bool,
        #[prost(uint32, optional, tag = "3")]
        pub track_type: Option<u32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetPatchInfoRsp {
        #[prost(bool, required, tag = "1")]
        pub success: bool,
        #[prost(string, required, tag = "2")]
        pub patch_track_id: String,
        #[prost(bool, required, tag = "3")]
        pub test_config: bool,
        #[prost(string, required, tag = "4")]
        pub patch_base_url: String,
        #[prost(uint32, required, tag = "5")]
        pub latest_version: u32,
        #[prost(uint32, optional, tag = "6")]
        pub track_type: Option<u32>,
        #[prost(bool, optional, tag = "7")]
        pub mandatory_on_startup: Option<bool>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct BuildVersion {
        #[prost(uint32, required, tag = "1")]
        pub version_number: u32,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetStatisticsStatusReq {
        #[prost(message, repeated, tag = "1")]
        pub build_versions: Vec<BuildVersion>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetStatisticsStatusRsp {
        #[prost(message, repeated, tag = "1")]
        pub enabled_build_versions: Vec<BuildVersion>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Req {
        #[prost(uint32, required, tag = "1")]
        pub request_id: u32,
        #[prost(message, optional, tag = "2")]
        pub get_patch_info_req: Option<super::demux::GetPatchInfoReq>,
        #[prost(message, optional, tag = "3")]
        pub get_statistics_status_req: Option<GetStatisticsStatusReq>,
        #[prost(message, optional, tag = "4")]
        pub get_patch_info_req_v2: Option<GetPatchInfoReq>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Rsp {
        #[prost(uint32, required, tag = "1")]
        pub request_id: u32,
        #[prost(message, optional, tag = "2")]
        pub get_patch_info_rsp: Option<super::demux::GetPatchInfoRsp>,
        #[prost(message, optional, tag = "3")]
        pub get_statistics_status_rsp: Option<GetStatisticsStatusRsp>,
        #[prost(message, optional, tag = "4")]
        pub get_patch_info_rsp_v2: Option<GetPatchInfoRsp>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Upstream {
        #[prost(message, optional, tag = "1")]
        pub request: Option<Req>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Downstream {
        #[prost(message, optional, tag = "1")]
        pub response: Option<Rsp>,
    }
}

pub mod ownership {
    use prost::Message;

    #[derive(Clone, PartialEq, Message)]
    pub struct OwnedGame {
        #[prost(uint32, required, tag = "1")]
        pub product_id: u32,
        #[prost(string, optional, tag = "2")]
        pub cd_key: Option<String>,
        #[prost(uint32, optional, tag = "3")]
        pub orbit_product_id: Option<u32>,
        #[prost(uint32, optional, tag = "5")]
        pub download_id: Option<u32>,
        #[prost(uint32, optional, tag = "6")]
        pub title_id: Option<u32>,
        #[prost(uint32, optional, tag = "7")]
        pub platform: Option<u32>,
        #[prost(uint32, optional, tag = "8")]
        pub product_type: Option<u32>,
        #[prost(uint32, optional, tag = "9")]
        pub orbit_game_version: Option<u32>,
        #[prost(string, optional, tag = "10")]
        pub orbit_game_version_url: Option<String>,
        #[prost(uint32, optional, tag = "11")]
        pub config_version: Option<u32>,
        #[prost(uint32, optional, tag = "12")]
        pub download_version: Option<u32>,
        #[prost(uint32, optional, tag = "13")]
        pub state: Option<u32>,
        #[prost(uint32, repeated, tag = "16")]
        pub product_associations: Vec<u32>,
        #[prost(string, optional, tag = "17")]
        pub configuration: Option<String>,
        #[prost(uint32, optional, tag = "19")]
        pub uplay_id: Option<u32>,
        #[prost(bool, optional, tag = "20")]
        pub owned: Option<bool>,
        #[prost(string, optional, tag = "21")]
        pub latest_manifest: Option<String>,
        #[prost(string, optional, tag = "22")]
        pub game_code: Option<String>,
        #[prost(uint32, optional, tag = "23")]
        pub balance: Option<u32>,
        #[prost(uint32, optional, tag = "25")]
        pub active_branch_id: Option<u32>,
        #[prost(uint32, repeated, tag = "27")]
        pub activation_ids: Vec<u32>,
        #[prost(uint32, optional, tag = "28")]
        pub brand_id: Option<u32>,
        #[prost(string, optional, tag = "30")]
        pub ubiservices_space_id: Option<String>,
        #[prost(string, optional, tag = "31")]
        pub ubiservices_app_id: Option<String>,
        #[prost(int32, optional, tag = "33")]
        pub package_ownership_state: Option<i32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct OwnedGames {
        #[prost(message, repeated, tag = "1")]
        pub owned_games: Vec<OwnedGame>,
    }

    #[derive(Clone, Debug, PartialEq)]
    pub struct OwnedGamesContainer {
        pub signature: Vec<u8>,
        pub product_ids: Vec<u32>,
        pub visible_or_installable_product_ids: Vec<u32>,
    }

    impl OwnedGamesContainer {
        pub fn encode_to_vec(&self) -> Vec<u8> {
            let mut buf = Vec::new();
            // field 1: product_ids (repeated uint32, NON-PACKED for proto2 compat)
            for &id in &self.product_ids {
                buf.push(0x08); // (1 << 3) | 0
                encode_varint(id as u64, &mut buf);
            }
            // field 2: signature (bytes, required)
            buf.push(0x12); // (2 << 3) | 2
            encode_varint(self.signature.len() as u64, &mut buf);
            buf.extend_from_slice(&self.signature);
            // field 3: visible_or_installable_product_ids (repeated uint32, NON-PACKED)
            for &id in &self.visible_or_installable_product_ids {
                buf.push(0x18); // (3 << 3) | 0
                encode_varint(id as u64, &mut buf);
            }
            buf
        }
    }

    fn encode_varint(mut val: u64, buf: &mut Vec<u8>) {
        loop {
            let byte = (val & 0x7F) as u8;
            val >>= 7;
            if val == 0 {
                buf.push(byte);
                break;
            } else {
                buf.push(byte | 0x80);
            }
        }
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct SteamGameInfo {
        #[prost(uint32, optional, tag = "1")]
        pub steam_app_id: Option<u32>,
        #[prost(string, optional, tag = "2")]
        pub steam_web_ticket: Option<String>,
        #[prost(string, optional, tag = "3")]
        pub steam_account_id64: Option<String>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct ProductBranchData {
        #[prost(uint32, required, tag = "1")]
        pub product_id: u32,
        #[prost(uint32, optional, tag = "2")]
        pub active_branch_id: Option<u32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetUplayPcTicketReq {
        #[prost(uint32, required, tag = "1")]
        pub uplay_id: u32,
        #[prost(int32, optional, tag = "2")]
        pub platform: Option<i32>,
        #[prost(uint32, optional, tag = "3")]
        pub product_id: Option<u32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct InitializeReq {
        #[prost(bool, optional, tag = "1")]
        pub deprecated_test_config: Option<bool>,
        #[prost(bool, optional, tag = "2")]
        pub get_associations: Option<bool>,
        #[prost(uint32, optional, tag = "3")]
        pub proto_version: Option<u32>,
        #[prost(message, repeated, tag = "4")]
        pub branches: Vec<ProductBranchData>,
        #[prost(bool, optional, tag = "5")]
        pub use_staging: Option<bool>,
        #[prost(message, optional, tag = "7")]
        pub get_uplay_pc_ticket: Option<GetUplayPcTicketReq>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct InitializeRsp {
        #[prost(bool, required, tag = "1")]
        pub success: bool,
        #[prost(message, optional, tag = "2")]
        pub owned_games: Option<OwnedGames>,
        #[prost(bytes = "vec", optional, tag = "3")]
        pub owned_games_container: Option<Vec<u8>>,
        #[prost(uint32, optional, tag = "4")]
        pub key_spam_ban_seconds: Option<u32>,
        #[prost(int32, optional, tag = "5")]
        pub subscription_state: Option<i32>,
        #[prost(uint32, optional, tag = "6")]
        pub subscription_type: Option<u32>,
        #[prost(string, optional, tag = "8")]
        pub uplay_pc_ticket: Option<String>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct RegisterOwnershipSteamPopReq {
        #[prost(string, repeated, tag = "1")]
        pub ticket: Vec<String>,
        #[prost(bool, required, tag = "2")]
        pub confirm: bool,
        #[prost(message, optional, tag = "3")]
        pub steam_game_info: Option<SteamGameInfo>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct RegisterOwnershipSteamPopRsp {
        #[prost(int32, required, tag = "1")]
        pub result: i32,
        #[prost(string, repeated, tag = "2")]
        pub configuration: Vec<String>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetUplayPcTicketRsp {
        #[prost(bool, required, tag = "1")]
        pub success: bool,
        #[prost(string, optional, tag = "2")]
        pub uplay_pc_ticket: Option<String>,
        #[prost(int32, optional, tag = "3")]
        pub error_code: Option<i32>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetProductConfigReq {
        #[prost(uint32, required, tag = "1")]
        pub product_id: u32,
        #[prost(bool, optional, tag = "2")]
        pub deprecated_test_config: Option<bool>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct GetProductConfigRsp {
        #[prost(int32, required, tag = "1")]
        pub result: i32,
        #[prost(bool, optional, tag = "2")]
        pub deprecated_test_config: Option<bool>,
        #[prost(string, optional, tag = "3")]
        pub configuration: Option<String>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Req {
        #[prost(uint32, required, tag = "1")]
        pub request_id: u32,
        #[prost(message, optional, tag = "2")]
        pub initialize_req: Option<InitializeReq>,
        #[prost(message, optional, tag = "3")]
        pub register_ownership_req: Option<RegisterOwnershipReq>,
        #[prost(message, optional, tag = "9")]
        pub get_product_config_req: Option<GetProductConfigReq>,
        #[prost(message, optional, tag = "14")]
        pub get_uplay_pc_ticket_req: Option<GetUplayPcTicketReq>,
        #[prost(message, optional, tag = "19")]
        pub register_ownership_steam_pop_req: Option<RegisterOwnershipSteamPopReq>,
        #[prost(string, optional, tag = "30")]
        pub ubi_ticket: Option<String>,
        #[prost(string, optional, tag = "31")]
        pub ubi_session_id: Option<String>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct RegisterOwnershipReq {
        #[prost(uint32, required, tag = "1")]
        pub product_id: u32,
        #[prost(string, optional, tag = "2")]
        pub cd_key: Option<String>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct RegisterOwnershipRsp {
        #[prost(int32, required, tag = "1")]
        pub result: i32,
        #[prost(bytes = "vec", optional, tag = "4")]
        pub owned_games_container: Option<Vec<u8>>,
        #[prost(message, optional, tag = "5")]
        pub owned_games: Option<OwnedGames>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Rsp {
        #[prost(uint32, required, tag = "1")]
        pub request_id: u32,
        #[prost(message, optional, tag = "2")]
        pub initialize_rsp: Option<InitializeRsp>,
        #[prost(message, optional, tag = "3")]
        pub register_ownership_rsp: Option<RegisterOwnershipRsp>,
        #[prost(message, optional, tag = "9")]
        pub get_product_config_rsp: Option<GetProductConfigRsp>,
        #[prost(message, optional, tag = "13")]
        pub get_uplay_pc_ticket_rsp: Option<GetUplayPcTicketRsp>,
        #[prost(message, optional, tag = "18")]
        pub register_ownership_steam_pop_rsp: Option<RegisterOwnershipSteamPopRsp>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct OwnedGamePush {
        #[prost(bytes = "vec", optional, tag = "2")]
        pub owned_games_container: Option<Vec<u8>>,
        #[prost(message, optional, tag = "4")]
        pub owned_games: Option<OwnedGames>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Push {
        #[prost(message, optional, tag = "1")]
        pub owned_game_push: Option<OwnedGamePush>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Upstream {
        #[prost(message, optional, tag = "1")]
        pub request: Option<Req>,
    }

    #[derive(Clone, PartialEq, Message)]
    pub struct Downstream {
        #[prost(message, optional, tag = "1")]
        pub response: Option<Rsp>,
        #[prost(message, optional, tag = "2")]
        pub push: Option<Push>,
    }
}
