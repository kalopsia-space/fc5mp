#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
namespace fc5mp::pe {
std::vector<std::string> export_names(const uint8_t* data, size_t size);
}
