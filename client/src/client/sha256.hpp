#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
namespace fc5mp::client {
// SHA-256 of a byte buffer, returned as lowercase hex. Also a file helper.
std::string sha256_hex(const uint8_t* data, size_t n);
std::string sha256_file_hex(const char* path);   // "" if unreadable
}
