#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace fc5mp::targets {
struct Target { const char* name; const char* sig; uint8_t kind; };
// Capture-point signatures, each verified UNIQUE in the supported FC_m64.dll.
// kind codes: capture/kinds.hpp (RDV_CREATESESSION / COOP_LEASH / SPAWN_PLAYER,
// semantic entry markers verified in P0/P0.5). UDP traffic is hooked by export
// in net_hooks; the static-OpenSSL TLS points are identified in P1.5b.
const std::vector<Target>& table();
// Resolve each table signature to a file/module offset in a mapped image.
// Returns (target-index, offset) for every signature found exactly once.
std::vector<std::pair<size_t,size_t>> resolve(const uint8_t* image, size_t size);
// Table indices NOT present in a resolve() result — the live run logs these so a
// short/empty capture is self-diagnosing (signature absent in the mapped image).
std::vector<size_t> missing_indices(const std::vector<std::pair<size_t,size_t>>& resolved);
void install_all();   // Windows: resolve in the loaded FC_m64.dll + log addresses
// Windows fast path: install the TLS detours at the OpenSSL entry points using
// the RVAs recorded for the version-locked FC_m64.dll build, skipping the full
// signature scan (too slow under Wine). Safe only for that exact build; the
// caller is responsible for confirming the module identity (version lock).
void install_tls_known();
}
