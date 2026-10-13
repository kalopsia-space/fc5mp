#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace fc5mp::sig {
struct Sig { std::vector<uint8_t> bytes; std::vector<bool> mask; }; // mask[i]=true => must-match
std::optional<Sig> parse(const std::string& pattern);
std::vector<size_t> find_all(const uint8_t* data, size_t size, const Sig& s);
std::optional<size_t> find_unique(const uint8_t* data, size_t size, const Sig& s);
}
