#include "versionlock.hpp"
#include <array>
#include <cctype>
namespace fc5mp::client {
static const std::array<const char*,1> kSupported = {
  "00833fae4d5d70213158a146ca98439b28a8e962b934885ecccd906204880af2"
};
bool build_supported(const std::string& sha){
  std::string s; s.reserve(sha.size());
  for(char c: sha) s.push_back((char)std::tolower((unsigned char)c));
  for(auto* k: kSupported) if(s==k) return true;
  return false;
}
}
