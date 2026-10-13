#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
namespace fc5mp::cap {
class Writer {
public:
  explicit Writer(std::string path);
  ~Writer();
  bool ok() const { return f_ != nullptr; }
  void write(uint8_t kind, const uint8_t* data, size_t n) noexcept;
private:
  std::FILE* f_ = nullptr;
};
}
