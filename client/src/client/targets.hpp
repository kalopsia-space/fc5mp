#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace fc5mp::targets {
struct Target { const char* name; const char* sig; uint8_t kind; };
// Capture-point signatures, each verified UNIQUE in the supported FC_m64.dll.
// kind codes: 10=rdv-createsession, 11=coop-leash, 12=spawn-player (semantic
// entry markers verified in P0/P0.5). The raw TLS byte points (SSL_write/read)
// and Storm UDP send/recv are identified during the live session (see plan T9),
// because confirming a detour targets the right function and matches its ABI
// needs the running game.
const std::vector<Target>& table();
// Resolve each table signature to a file/module offset in a mapped image.
// Returns (target-index, offset) for every signature found exactly once.
std::vector<std::pair<size_t,size_t>> resolve(const uint8_t* image, size_t size);
void install_all();   // Windows: resolve in the loaded FC_m64.dll + log addresses
}
