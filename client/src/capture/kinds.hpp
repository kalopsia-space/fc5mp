#pragma once
#include <cstdint>
// Capture-frame kind codes (the u8 after the timestamp in every frame).
// Values are part of the file format — never renumber; the Python reader
// (analysis/fc5analyze/capture.py) mirrors this table.
namespace fc5mp::cap::kind {
// boot progress (payload: u8 stage — 0 boot thread start, 1 version-lock
// passed, 2 UDP hooks live, 3 TLS hooks live)
constexpr uint8_t BOOT              = 1;
// semantic entry markers (payload: u64 resolved VA)
constexpr uint8_t RDV_CREATESESSION = 10;
constexpr uint8_t COOP_LEASH        = 11;
constexpr uint8_t SPAWN_PLAYER      = 12;
constexpr uint8_t SSL_READ_ADDR     = 13;
constexpr uint8_t SSL_WRITE_ADDR    = 14;
constexpr uint8_t SSL_CONNECT_ADDR  = 15;
// datagram traffic (payload: udp_frame record)
constexpr uint8_t UDP_SEND          = 20;
constexpr uint8_t UDP_RECV          = 21;
// TLS plaintext (payload: [u64 SSL*][redacted bytes], see redact.hpp)
constexpr uint8_t TLS_WRITE         = 22;
constexpr uint8_t TLS_READ          = 23;
// net-hook diagnostics
constexpr uint8_t API_SEEN          = 30;   // payload: u8 api id, first call of each hooked API
constexpr uint8_t RECV_PENDING      = 31;   // payload: u64 socket, u32 running count;
                                            // overlapped WSARecvFrom, data NOT captured
// harness self-diagnosis
constexpr uint8_t MODULE_TIMEOUT    = 250;  // FC_m64.dll never mapped (len 0)
constexpr uint8_t SIG_MISSING       = 251;  // payload: u8 target-table index
constexpr uint8_t HOOK_FAILED       = 252;  // payload: u8 api id
}
